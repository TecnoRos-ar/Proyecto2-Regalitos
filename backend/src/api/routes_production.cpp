#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/middleware.hpp"
#include "domain/production_repo.hpp"

using njson = nlohmann::json;
using namespace crow;

void register_production_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    CROW_ROUTE(app, "/api/production").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded())
            return crow::response{400, R"({"error":"JSON inválido"})"};

        auto product_id = body.value("product_id", std::string{});
        if (!body["quantity"].is_number_integer() || body["quantity"].get<long long>() <= 0)
            return crow::response{400, R"({"error":"quantity debe ser un entero mayor que 0"})"};
        if (body["quantity"].get<long long>() > 1000000)
            return crow::response{400, R"({"error":"quantity demasiado grande"})"};
        int quantity = body["quantity"].get<int>();
        const auto& user_id = me->id;

        if (!db::is_uuid(product_id))
            return crow::response{400, R"({"error":"product_id inválido"})"};

        auto conn = pool.acquire();
        try {
            pqxx::work tx{*conn};
            auto res = production_repo::produce(tx, product_id, quantity, user_id);
            if (!res.error.empty()) {
                tx.abort();
                int status = res.error == "Producto inexistente" ? 404 : 409;
                return crow::response{status, njson{{"error", res.error}}.dump()};
            }
            tx.commit();
            return crow::response{201, njson{{"produced", res.produced}}.dump()};
        } catch (const pqxx::sql_error& e) {
            CROW_LOG_ERROR << "produce: " << e.what();
            return crow::response{500, R"({"error":"error interno"})"};
        }
    });

    CROW_ROUTE(app, "/api/movements").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto rs = tx.exec(
            "SELECT sm.*, m.name AS material_name "
            "FROM stock_movements sm JOIN materials m ON m.id = sm.material_id "
            "ORDER BY sm.created_at DESC LIMIT 200");
        njson out = njson::array();
        for (const auto& r : rs) {
            out.push_back({
                {"id",            r["id"].as<std::string>()},
                {"material_id",   r["material_id"].as<std::string>()},
                {"material_name", r["material_name"].as<std::string>()},
                {"kind",          r["kind"].as<std::string>()},
                {"delta",         r["delta"].as<double>()},
                {"note",          r["note"].is_null() ? "" : r["note"].as<std::string>()},
                {"created_at",    r["created_at"].as<std::string>()}
            });
        }
        tx.commit();
        return crow::response{200, out.dump()};
    });
}
