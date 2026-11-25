#include <sstream>
#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"
#include "auth/middleware.hpp"
#include "config.hpp"
#include "qrcodegen.hpp"

using njson = nlohmann::json;
using namespace crow;

static std::string secret() {
    return env("JWT_SECRET", "dev-only-secret-cambiar-en-prod");
}
static const int SESSION_TTL = 8 * 3600;
static const int TMP_TTL     = 5 * 60;

static std::string make_tmp(const std::string& user_id, const std::string& purpose) {
    njson p = {
        {"sub", user_id}, {"purpose", purpose},
        {"iat", (int64_t)std::time(nullptr)},
        {"exp", (int64_t)std::time(nullptr) + TMP_TTL}
    };
    return auth::crypto::jwt_sign(p, secret());
}
static std::string make_session(const std::string& id, const std::string& email,
                                const std::string& role, int session_version) {
    njson p = {
        {"sub", id}, {"email", email}, {"role", role}, {"sv", session_version},
        {"purpose", "session"},
        {"iat", (int64_t)std::time(nullptr)},
        {"exp", (int64_t)std::time(nullptr) + SESSION_TTL}
    };
    return auth::crypto::jwt_sign(p, secret());
}
static void set_cookie(crow::response& res, const std::string& token, int ttl) {
    std::string c = "session=" + token +
        "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + std::to_string(ttl);
    res.set_header("Set-Cookie", c);
}
static void clear_cookie(crow::response& res) {
    res.set_header("Set-Cookie",
        "session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
}


static std::string qr_to_svg(const qrcodegen::QrCode& qr, int border = 4) {
    int size = qr.getSize();
    int dim = size + border * 2;
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "viewBox=\"0 0 " << dim << " " << dim << "\" "
        << "width=\"" << dim * 8 << "\" height=\"" << dim * 8 << "\" "
        << "shape-rendering=\"crispEdges\">"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>"
        << "<path fill=\"#000000\" d=\"";
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (qr.getModule(x, y))
                oss << "M" << (x + border) << " " << (y + border) << "h1v1h-1z";
        }
    }
    oss << "\"/></svg>";
    return oss.str();
}

static const char* FAIL_SQL =
    // Un bloqueo vencido reinicia la cuenta: vuelven a ser cinco intentos.
    "UPDATE users SET "
    "failed_attempts = CASE WHEN locked_until <= now() THEN 1 ELSE failed_attempts + 1 END, "
    "locked_until = CASE WHEN locked_until <= now() THEN NULL "
    "  WHEN failed_attempts + 1 >= 5 THEN now() + interval '15 minutes' "
    "  ELSE locked_until END "
    "WHERE id = $1";

static bool is_locked(pqxx::work& tx, const std::string& id) {
    return db::exec(tx, "SELECT coalesce(locked_until > now(), false) FROM users WHERE id = $1",
                    id)[0][0].as<bool>();
}
static crow::response locked_resp() {
    return crow::response{429, R"({"error":"demasiados intentos, probá en 15 minutos"})"};
}

void register_auth_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response{400};

        auto email = body.value("email", "");
        auto pass  = body.value("password", "");
        if (email.empty() || pass.empty())
            return crow::response{400, R"({"error":"email y password requeridos"})"};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto u = auth::find_by_email(tx, email);
        if (!u) return crow::response{401, R"({"error":"credenciales inválidas"})"};
        if (!u->active) return crow::response{403, R"({"error":"usuario deshabilitado"})"};
        if (is_locked(tx, u->id)) return locked_resp();

        if (!auth::crypto::verify_password(pass, u->password_hash)) {
            db::exec(tx, FAIL_SQL, u->id);
            tx.commit();
            return crow::response{401, R"({"error":"credenciales inválidas"})"};
        }

        if (u->totp_enabled) {
            auto tmp = make_tmp(u->id, "login");
            tx.commit();
            return crow::response{200,
                njson({{"needs","totp"},{"tmp",tmp}}).dump()};
        } else {
            auto tmp = make_tmp(u->id, "setup");
            tx.commit();
            return crow::response{200,
                njson({{"needs","setup"},{"tmp",tmp}}).dump()};
        }
    });

    CROW_ROUTE(app, "/api/auth/totp/setup").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response{400};
        auto payload = auth::crypto::jwt_verify(body.value("tmp",""), secret());
        if (!payload || payload->value("purpose","") != "setup")
            return crow::response{401};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto u = auth::find_by_id(tx, payload->value("sub",""));
        if (!u) return crow::response{404};
        if (u->totp_enabled)
            return crow::response{409, R"({"error":"2FA ya configurado"})"};

        auto sec = auth::crypto::base32_random_secret(20);
        db::exec(tx, "UPDATE users SET totp_secret = $1 WHERE id = $2", sec, u->id);
        tx.commit();

        auto url = "otpauth://totp/Regalitos:" + u->email +
                   "?secret=" + sec + "&issuer=Regalitos";
        using qrcodegen::QrCode;
        QrCode qr = QrCode::encodeText(url.c_str(), QrCode::Ecc::MEDIUM);
        std::string svg = qr_to_svg(qr, 4);
        return crow::response{200,
            njson({{"secret",sec},{"otpauth_url",url},{"qr_svg",svg}}).dump()};
    });

    auto _enable_or_verify = [&pool](const crow::request& req, bool enable) {
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response{400};
        auto payload = auth::crypto::jwt_verify(body.value("tmp",""), secret());
        if (!payload) return crow::response{401};
        auto purpose = payload->value("purpose","");
        if (enable && purpose != "setup") return crow::response{401};
        if (!enable && purpose != "login") return crow::response{401};

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto u = auth::find_by_id(tx, payload->value("sub",""));
        if (!u || !u->totp_secret) return crow::response{404};
        if (!u->active) return crow::response{403, R"({"error":"usuario deshabilitado"})"};
        if (is_locked(tx, u->id)) return locked_resp();

        auto code = body.value("code","");
        if (!auth::crypto::totp_verify(*u->totp_secret, code)) {
            db::exec(tx, FAIL_SQL, u->id);
            tx.commit();
            return crow::response{401, R"({"error":"código inválido"})"};
        }

        if (enable) {
            db::exec(tx,
                "UPDATE users SET totp_enabled = true, "
                "failed_attempts = 0, locked_until = NULL, last_login_at = now() WHERE id = $1",
                u->id);
        } else {
            db::exec(tx,
                "UPDATE users SET failed_attempts = 0, locked_until = NULL, last_login_at = now() "
                "WHERE id = $1", u->id);
        }
        tx.commit();

        auto token = make_session(u->id, u->email, u->role, u->session_version);
        crow::response res{200,
            njson({{"user",{{"id",u->id},{"email",u->email},
                           {"name",u->name},{"role",u->role}}}}).dump()};
        set_cookie(res, token, SESSION_TTL);
        return res;
    };

    CROW_ROUTE(app, "/api/auth/totp/enable").methods("POST"_method)
    ([_enable_or_verify](const crow::request& req){ return _enable_or_verify(req, true); });

    CROW_ROUTE(app, "/api/auth/totp/verify").methods("POST"_method)
    ([_enable_or_verify](const crow::request& req){ return _enable_or_verify(req, false); });

    CROW_ROUTE(app, "/api/auth/logout").methods("POST"_method)
    ([&pool](const crow::request& req) {
        // Invalida las sesiones abiertas de la cuenta, no solo la cookie de este navegador.
        if (auto me = auth::get_user(req, pool)) {
            auto conn = pool.acquire();
            pqxx::work tx{*conn};
            db::exec(tx, "UPDATE users SET session_version = session_version + 1 WHERE id = $1", me->id);
            tx.commit();
        }
        crow::response res{200, R"({"ok":true})"};
        clear_cookie(res);
        return res;
    });

    CROW_ROUTE(app, "/api/auth/me").methods("GET"_method)
    ([&pool](const crow::request& req) {
        auto u = auth::get_user(req, pool);
        if (!u) return auth::unauthorized();
        return crow::response{200,
            njson({{"id",u->id},{"email",u->email},{"role",u->role}}).dump()};
    });
}
