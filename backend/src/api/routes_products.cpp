#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/middleware.hpp"

using njson = nlohmann::json;
using namespace crow;

static njson row_to_product(pqxx::row_ref const& r) {
    return {
        {"id",             r["id"].as<std::string>()},
        {"name",           r["name"].as<std::string>()},
        {"labor_cost",     r["labor_cost"].as<double>()},
        {"other_cost",     r["other_cost"].as<double>()},
        {"profit_type",    r["profit_type"].as<std::string>()},
        {"profit_value",   r["profit_value"].as<double>()},
        {"finished_stock", r["finished_stock"].as<int>()}
    };
}

void register_product_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    CROW_ROUTE(app, "/api/products").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto rs = tx.exec("SELECT * FROM products ORDER BY name");
        njson out = njson::array();
        for (const auto& r : rs) {
            auto p = row_to_product(r);
            auto rec = db::exec(tx,
                "SELECT ri.material_id, ri.quantity, m.name, m.unit "
                "FROM recipe_items ri JOIN materials m ON m.id = ri.material_id "
                "WHERE ri.product_id = $1", p["id"].get<std::string>());
            p["recipe"] = njson::array();
            for (const auto& r2 : rec) {
                p["recipe"].push_back({
                    {"material_id", r2[0].c_str()},
                    {"quantity",    r2[1].as<double>()},
                    {"name",        r2[2].as<std::string>()},
                    {"unit",        r2[3].as<std::string>()}
                });
            }
            out.push_back(std::move(p));
        }
        tx.commit();
        return crow::response{200, out.dump()};
    });

    CROW_ROUTE(app, "/api/products").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded())
            return crow::response{400, R"({"error":"JSON inválido"})"};
        auto name = body.value("name", std::string{});
        if (db::is_blank(name))
            return crow::response{400, R"({"error":"name es obligatorio"})"};

        auto ptype = body.value("profit_type", std::string{"pct"});
        if (ptype != "pct" && ptype != "fixed")
            return crow::response{400, R"({"error":"profit_type inválido"})"};
        if (body.contains("recipe") && !body["recipe"].is_array())
            return crow::response{400, R"({"error":"receta inválida"})"};
        if (body.contains("recipe"))
            for (auto& item : body["recipe"])
                if (!item.is_object() || !db::is_uuid(item.value("material_id", std::string{})) ||
                    item.value("quantity", 0.0) <= 0)
                    return crow::response{400, R"({"error":"receta inválida"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto r = db::exec(tx,
            "INSERT INTO products "
            "(name, labor_cost, other_cost, profit_type, profit_value) "
            "VALUES ($1, $2, $3, $4, $5) RETURNING *",
            name,
            body.value("labor_cost", 0.0),
            body.value("other_cost", 0.0),
            ptype,
            body.value("profit_value", 100.0));
        auto pid = r[0]["id"].as<std::string>();

        if (body.contains("recipe") && body["recipe"].is_array()) {
            try {
                for (auto& item : body["recipe"]) {
                    db::exec(tx,
                        "INSERT INTO recipe_items (product_id, material_id, quantity) "
                        "VALUES ($1, $2, $3) ON CONFLICT (product_id, material_id) "
                        "DO UPDATE SET quantity = recipe_items.quantity + EXCLUDED.quantity",
                        pid,
                        item.value("material_id", std::string{}),
                        item.value("quantity", 0.0));
                }
            } catch (const pqxx::foreign_key_violation&) {
                return crow::response{400, R"({"error":"material de la receta inexistente"})"};
            }
        }

        auto out = row_to_product(r[0]);
        tx.commit();
        return crow::response{201, out.dump()};
    });
}
