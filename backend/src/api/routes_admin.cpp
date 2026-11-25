#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"
#include "auth/middleware.hpp"

using njson = nlohmann::json;
using namespace crow;

void register_admin_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    CROW_ROUTE(app, "/api/admin/users").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (me->role != "admin") return auth::forbidden();

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto r = tx.exec(
            "SELECT id, email, name, role, active, totp_enabled, last_login_at "
            "FROM users ORDER BY created_at");
        njson out = njson::array();
        for (const auto& row : r) {
            out.push_back({
                {"id", row["id"].as<std::string>()},
                {"email", row["email"].as<std::string>()},
                {"name", row["name"].is_null()?"":row["name"].as<std::string>()},
                {"role", row["role"].as<std::string>()},
                {"active", row["active"].as<bool>()},
                {"totp_enabled", row["totp_enabled"].as<bool>()},
                {"last_login_at", row["last_login_at"].is_null()?"":
                                  row["last_login_at"].as<std::string>()}
            });
        }
        tx.commit();
        return crow::response{200, out.dump()};
    });

    CROW_ROUTE(app, "/api/admin/users").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (me->role != "admin") return auth::forbidden();

        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response{400};
        auto email = body.value("email","");
        auto pass  = body.value("password","");
        auto name  = body.value("name","");
        auto role  = body.value("role","employee");
        if (email.empty() || pass.size() < 8)
            return crow::response{400,
                R"({"error":"email y password (>=8) requeridos"})"};
        if (role != "admin" && role != "employee")
            return crow::response{400, R"({"error":"role inválido"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        try {
            auto u = auth::create_user(
                tx, email, auth::crypto::hash_password(pass), name, role);
            auth::log_audit(tx, me->id, "user.create",
                {{"target", u.id}, {"email", email}, {"role", role}});
            tx.commit();
            return crow::response{201,
                njson({{"id",u.id},{"email",u.email},{"role",u.role}}).dump()};
        } catch (const pqxx::unique_violation&) {
            return crow::response{409, R"({"error":"email ya existe"})"};
        }
    });

    // Cambiar rol / activar / desactivar / resetear 2FA
    CROW_ROUTE(app, "/api/admin/users/<string>").methods("PUT"_method)
    ([&pool](const crow::request& req, std::string id) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (!db::is_uuid(id)) return crow::response{400, R"({"error":"id inválido"})"};
        if (me->role != "admin") return auth::forbidden();

        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) return crow::response{400};
        if (me->id == id && (body.contains("role") || body.contains("active")))
            return crow::response{400, R"({"error":"no podés cambiar tu propio rol o estado"})"};
        if ((body.contains("role") && !body["role"].is_string()) ||
            (body.contains("active") && !body["active"].is_boolean()))
            return crow::response{400};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        if (!auth::find_by_id(tx, id)) return crow::response{404, R"({"error":"no existe"})"};
        if (body.contains("role")) {
            auto role = body["role"].get<std::string>();
            if (role != "admin" && role != "employee") return crow::response{400};
            db::exec(tx, "UPDATE users SET role=$1 WHERE id=$2", role, id);
        }
        if (body.contains("active")) {
            db::exec(tx, "UPDATE users SET active=$1 WHERE id=$2",
                body["active"].get<bool>(), id);
        }
        if (body.value("reset_2fa", false)) {
            db::exec(tx,
                "UPDATE users SET totp_secret=NULL, totp_enabled=false "
                "WHERE id=$1", id);
        }
        auth::log_audit(tx, me->id, "user.update",
            {{"target", id}, {"changes", body}});
        tx.commit();
        return crow::response{200, R"({"ok":true})"};
    });

    CROW_ROUTE(app, "/api/admin/users/<string>").methods("DELETE"_method)
    ([&pool](const crow::request& req, std::string id) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        if (!db::is_uuid(id)) return crow::response{400, R"({"error":"id inválido"})"};
        if (me->role != "admin") return auth::forbidden();
        if (me->id == id)
            return crow::response{400, R"({"error":"no te podés borrar a vos mismo"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        try {
            db::exec(tx, "DELETE FROM users WHERE id=$1", id);
            auth::log_audit(tx, me->id, "user.delete", {{"target", id}});
            tx.commit();
        } catch (const pqxx::foreign_key_violation&) {
            return crow::response{409,
                R"({"error":"el usuario tiene historial; desactivalo en vez de borrarlo"})"};
        }
        return crow::response{204};
    });
}
