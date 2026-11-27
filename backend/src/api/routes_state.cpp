// Estado completo de la app web (materiales, productos, config) guardado en
// Postgres para que se comparta entre navegadores/dispositivos.
#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/middleware.hpp"
#include "auth/user_repo.hpp"
#include <set>

using njson = nlohmann::json;
using namespace crow;

static constexpr std::size_t MAX_STATE_BYTES = 2 * 1024 * 1024;
static constexpr int HISTORY_KEEP = 500;

// Cuántos id de `before` no están en `after`.
static int missing_ids(const njson& before, const njson& after) {
    std::set<std::string> now;
    for (auto& x : after)
        if (x.is_object() && x.contains("id") && x["id"].is_string()) now.insert(x["id"].get<std::string>());
    int n = 0;
    if (!before.is_array()) return 0;
    for (auto& x : before)
        if (x.is_object() && x.contains("id") && x["id"].is_string() && !now.count(x["id"].get<std::string>())) ++n;
    return n;
}
static void snapshot(pqxx::work& tx, const std::string& value, const std::string& user,
                     const std::string& reason) {
    db::exec(tx, "INSERT INTO state_history (value, saved_by, reason) VALUES ($1::jsonb, $2, $3)",
             value, user, reason);
    db::exec(tx, "DELETE FROM state_history WHERE id <= "
                 "(SELECT id FROM state_history ORDER BY id DESC OFFSET $1 LIMIT 1)", HISTORY_KEEP);
}
static void put_state(pqxx::work& tx, const std::string& value) {
    db::exec(tx,
        "INSERT INTO config (key, value) VALUES ('app_state', $1::jsonb) "
        "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value", value);
}

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
        auto cur = tx.exec("SELECT value::text FROM config WHERE key = 'app_state' FOR UPDATE");
        if (!cur.empty()) {
            auto old = njson::parse(cur[0][0].as<std::string>(), nullptr, false);
            // REG-HAL-43: solo un admin puede borrar materiales o productos.
            if (me->role != "admin" && !old.is_discarded() && old.is_object()) {
                for (auto key : {"materiales", "productos"}) {
                    auto removed = missing_ids(old.value(key, njson::array()), body[key]);
                    if (removed > 0)
                        return crow::response{403,
                            R"({"error":"solo un administrador puede borrar materiales o productos"})"};
                }
            }
            if (cur[0][0].as<std::string>() != "null")
                snapshot(tx, cur[0][0].as<std::string>(), me->id, "save");
        }
        put_state(tx, body.dump());
        tx.commit();
        return crow::response{200, R"({"ok":true})"};
    });

    // ---- Historial de versiones (solo admin) ----
    CROW_ROUTE(app, "/api/state/history").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (me->role != "admin") return auth::forbidden();
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto rs = tx.exec(
            "SELECT h.id, h.created_at::text, coalesce(u.email, ''), h.reason, "
            "  coalesce(jsonb_array_length(h.value->'materiales'), 0), "
            "  coalesce(jsonb_array_length(h.value->'productos'), 0) "
            "FROM state_history h LEFT JOIN users u ON u.id = h.saved_by "
            "ORDER BY h.id DESC LIMIT 100");
        tx.commit();
        njson out = njson::array();
        for (auto r : rs)
            out.push_back({{"id", r[0].as<int64_t>()}, {"created_at", r[1].as<std::string>()},
                           {"saved_by", r[2].as<std::string>()}, {"reason", r[3].as<std::string>()},
                           {"materiales", r[4].as<int>()}, {"productos", r[5].as<int>()}});
        return crow::response{200, out.dump()};
    });

    CROW_ROUTE(app, "/api/state/history/<int>/restore").methods("POST"_method)
    ([&pool](const crow::request& req, int id) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (me->role != "admin") return auth::forbidden();
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto h = db::exec(tx, "SELECT value::text FROM state_history WHERE id = $1", id);
        if (h.empty()) return crow::response{404, R"({"error":"versión inexistente"})"};
        auto cur = tx.exec("SELECT value::text FROM config WHERE key = 'app_state' FOR UPDATE");
        if (!cur.empty()) snapshot(tx, cur[0][0].as<std::string>(), me->id, "before_restore");
        put_state(tx, h[0][0].as<std::string>());
        auth::log_audit(tx, me->id, "state.restore", {{"history_id", id}});
        tx.commit();
        return crow::response{200, R"({"ok":true})"};
    });
}
