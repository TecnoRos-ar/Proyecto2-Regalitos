// Estado completo de la app web (materiales, productos, config) guardado en
// Postgres para que se comparta entre navegadores/dispositivos.
#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/middleware.hpp"

using njson = nlohmann::json;
using namespace crow;

static constexpr std::size_t MAX_STATE_BYTES = 2 * 1024 * 1024;

void register_state_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    CROW_ROUTE(app, "/api/state").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto r = tx.exec("SELECT value::text FROM config WHERE key = 'app_state'");
        tx.commit();
        std::string data = r.empty() ? "null" : r[0][0].as<std::string>();
        crow::response res{200, R"({"data":)" + data + "}"};
        res.set_header("Content-Type", "application/json");
        return res;
    });

    CROW_ROUTE(app, "/api/state").methods("PUT"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (req.body.size() > MAX_STATE_BYTES)
            return crow::response{413, R"({"error":"datos demasiado grandes"})"};

        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object() ||
            !body.contains("materiales") || !body["materiales"].is_array() ||
            !body.contains("productos")  || !body["productos"].is_array())
            return crow::response{400, R"({"error":"estado inválido"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        db::exec(tx,
            "INSERT INTO config (key, value) VALUES ('app_state', $1::jsonb) "
            "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value",
            body.dump());
        tx.commit();
        return crow::response{200, R"({"ok":true})"};
    });
}
