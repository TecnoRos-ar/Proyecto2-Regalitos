#include "db/pool.hpp"
#include "domain/production_repo.hpp"
#include "domain/calc.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <unordered_map>

namespace production_repo {

ProduceResult produce(pqxx::work& tx,
                      const std::string& product_id,
                      int quantity,
                      const std::string& user_id) {
    if (quantity <= 0) return {0, "Cantidad inválida"};

    auto prow = db::exec(tx,
        "SELECT id, name, labor_cost, other_cost, profit_type, profit_value "
        "FROM products WHERE id = $1 FOR UPDATE", product_id);
    if (prow.empty()) return {0, "Producto inexistente"};

    auto rrows = db::exec(tx,
        "SELECT material_id, quantity FROM recipe_items WHERE product_id = $1",
        product_id);
    if (rrows.empty()) return {0, "La receta está vacía"};

    domain::ProductInput p;
    p.id           = prow[0][0].as<std::string>();
    p.name         = prow[0][1].as<std::string>();
    p.labor_cost   = prow[0][2].as<double>();
    p.other_cost   = prow[0][3].as<double>();
    p.profit_type  = prow[0][4].as<std::string>();
    p.profit_value = prow[0][5].as<double>();
    for (const auto& r : rrows)
        p.recipe.push_back({ r[0].as<std::string>(), r[1].as<double>() });

    auto need = domain::aggregate_recipe(p.recipe, quantity);

    // Literal de array Postgres: los ids son UUIDs leídos de la DB.
    std::string ids = "{";
    for (auto& [k, _] : need) { if (ids.size() > 1) ids += ','; ids += k; }
    ids += '}';

    auto mrows = db::exec(tx,
        "SELECT id, name, unit, current_stock, last_purchase_price, last_purchase_qty "
        "FROM materials WHERE id = ANY($1::uuid[]) FOR UPDATE",
        ids);

    std::unordered_map<std::string, domain::MaterialInput> mats;
    for (const auto& r : mrows) {
        domain::MaterialInput m;
        m.id                  = r[0].as<std::string>();
        m.name                = r[1].as<std::string>();
        m.unit                = r[2].as<std::string>();
        m.stock               = r[3].as<double>();
        m.last_purchase_price = r[4].as<double>();
        m.last_purchase_qty   = r[5].as<double>();
        mats[m.id] = m;
    }

    for (auto& [mid, q] : need) {
        auto it = mats.find(mid);
        if (it == mats.end()) return {0, "Material inexistente en la receta"};
        if (it->second.stock + 1e-9 < q)
            return {0, "Stock insuficiente de " + it->second.name};
    }

    for (auto& [mid, q] : need) {
        auto& m = mats[mid];
        db::exec(tx,
            "UPDATE materials SET current_stock = current_stock - $1 WHERE id = $2",
            q, mid);
        db::exec(tx,
            "INSERT INTO stock_movements "
            "(material_id, kind, delta, unit_cost, reference_type, note) "
            "VALUES ($1, 'production', $2, $3, 'production_order', $4)",
            mid, -q, domain::unit_cost(m),
            "Prod. " + std::to_string(quantity) + "x " + p.name);
    }

    auto cost = domain::calculate(p, mats, 100);
    nlohmann::json snap = {
        {"materials_cost", cost.materials},
        {"extra_cost",     cost.extra},
        {"total_cost",     cost.cost},
        {"unit_price",     cost.price},
        {"quantity",       quantity}
    };

    db::exec(tx,
        "INSERT INTO production_orders "
        "(product_id, quantity_requested, quantity_produced, status, "
        " cost_snapshot, created_by, completed_at) "
        "VALUES ($1, $2, $2, 'available', $3, "
        "        NULLIF($4, '')::uuid, now())",
        product_id, quantity, snap.dump(), user_id);

    db::exec(tx,
        "UPDATE products SET finished_stock = finished_stock + $1 WHERE id = $2",
        quantity, product_id);

    return {quantity, ""};
}

} // namespace production_repo
