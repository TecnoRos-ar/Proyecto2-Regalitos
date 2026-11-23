#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/middleware.hpp"

using njson = nlohmann::json;
using namespace crow;

static njson row_to_material(pqxx::row_ref const& r) {
    double price = r["last_purchase_price"].as<double>();
    double qty   = r["last_purchase_qty"].as<double>();
    return {
        {"id",                  r["id"].as<std::string>()},
        {"name",                r["name"].as<std::string>()},
        {"unit",                r["unit"].as<std::string>()},
        {"current_stock",       r["current_stock"].as<double>()},
        {"last_purchase_price", price},
        {"last_purchase_qty",   qty},
        {"unit_cost",           qty > 0 ? price / qty : 0.0}
    };
}

void register_material_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    CROW_ROUTE(app, "/api/materials").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto rs = tx.exec("SELECT * FROM materials ORDER BY name");
        njson out = njson::array();
        for (const auto& r : rs) out.push_back(row_to_material(r));
        tx.commit();
        return crow::response{200, out.dump()};
    });

    CROW_ROUTE(app, "/api/materials").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded())
            return crow::response{400, R"({"error":"JSON inválido"})"};

        auto name = body.value("name", std::string{});
        auto unit = body.value("unit", std::string{"un"});
        if (name.empty())
            return crow::response{400, R"({"error":"name es obligatorio"})"};
        if (unit != "m" && unit != "un" && unit != "g")
            return crow::response{400, R"({"error":"unit inválida"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto r = db::exec(tx,
            "INSERT INTO materials "
            "(name, unit, current_stock, last_purchase_price, last_purchase_qty) "
            "VALUES ($1, $2, $3, $4, $5) RETURNING *",
            name, unit,
            body.value("current_stock", 0.0),
            body.value("last_purchase_price", 0.0),
            std::max(1e-6, body.value("last_purchase_qty", 1.0)));
        auto out = row_to_material(r[0]);
        tx.commit();
        return crow::response{201, out.dump()};
    });

    CROW_ROUTE(app, "/api/materials/<string>").methods("PUT"_method)
    ([&pool](const crow::request& req, std::string id) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (!db::is_uuid(id)) return crow::response{400, R"({"error":"id inválido"})"};
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded())
            return crow::response{400, R"({"error":"JSON inválido"})"};
        if (!body.is_object())
            return crow::response{400, R"({"error":"JSON inválido"})"};
        for (auto k : {"name", "unit"})
            if (body.contains(k) && !body[k].is_string())
                return crow::response{400, R"({"error":"tipo inválido"})"};
        for (auto k : {"current_stock", "last_purchase_price", "last_purchase_qty"})
            if (body.contains(k) && (!body[k].is_number() || body[k].get<double>() < 0))
                return crow::response{400, R"({"error":"número inválido"})"};
        if (body.contains("unit")) {
            auto u = body["unit"].get<std::string>();
            if (u != "m" && u != "un" && u != "g")
                return crow::response{400, R"({"error":"unit inválida"})"};
        }
        if (body.contains("last_purchase_qty") && body["last_purchase_qty"].get<double>() <= 0)
            return crow::response{400, R"({"error":"last_purchase_qty debe ser > 0"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto r = db::exec(tx,
            "UPDATE materials SET "
            "name = COALESCE($2, name), "
            "unit = COALESCE($3, unit), "
            "current_stock = COALESCE($4, current_stock), "
            "last_purchase_price = COALESCE($5, last_purchase_price), "
            "last_purchase_qty = COALESCE($6, last_purchase_qty) "
            "WHERE id = $1 RETURNING *",
            id,
            body.contains("name")  ? std::optional<std::string>(body["name"])  : std::nullopt,
            body.contains("unit")  ? std::optional<std::string>(body["unit"])  : std::nullopt,
            body.contains("current_stock")       ? std::optional<double>(body["current_stock"])       : std::nullopt,
            body.contains("last_purchase_price") ? std::optional<double>(body["last_purchase_price"]) : std::nullopt,
            body.contains("last_purchase_qty")   ? std::optional<double>(body["last_purchase_qty"])   : std::nullopt);
        if (r.empty()) return crow::response{404, R"({"error":"no existe"})"};
        auto out = row_to_material(r[0]);
        tx.commit();
        return crow::response{200, out.dump()};
    });

    CROW_ROUTE(app, "/api/materials/<string>").methods("DELETE"_method)
    ([&pool](const crow::request& req, std::string id) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (!db::is_uuid(id)) return crow::response{400, R"({"error":"id inválido"})"};
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        try {
            auto r = db::exec(tx,
                "DELETE FROM materials WHERE id = $1 RETURNING id", id);
            if (r.empty()) return crow::response{404, R"({"error":"no existe"})"};
            tx.commit();
        } catch (const pqxx::foreign_key_violation&) {
            return crow::response{409, R"({"error":"el material está en uso en recetas o movimientos"})"};
        }
        return crow::response{204};
    });
}
