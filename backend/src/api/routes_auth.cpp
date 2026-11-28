#include <sstream>
#include <crow.h>
#include <nlohmann/json.hpp>
#include "db/pool.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"
#include "auth/middleware.hpp"
#include "config.hpp"
#include "qrcodegen.hpp"
#include "mail/mailer.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <thread>

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
// REG-HAL-41: con HTTPS la cookie lleva Secure y no viaja por HTTP.
// COOKIE_SECURE=1/0 lo fuerza; si no está, se deduce de APP_URL.
static bool cookie_secure() {
    auto v = env("COOKIE_SECURE", "");
    if (v == "1" || v == "true")  return true;
    if (v == "0" || v == "false") return false;
    return env("APP_URL", "").rfind("https://", 0) == 0;
}
static std::string cookie_attrs() {
    return std::string("; Path=/; HttpOnly; SameSite=Lax") +
           (cookie_secure() ? "; Secure" : "");
}
static void set_cookie(crow::response& res, const std::string& token, int ttl) {
    res.set_header("Set-Cookie",
        "session=" + token + cookie_attrs() + "; Max-Age=" + std::to_string(ttl));
}
static void clear_cookie(crow::response& res) {
    res.set_header("Set-Cookie", "session=" + cookie_attrs() + "; Max-Age=0");
}
static std::string client_ip(const crow::request& req) {
    auto xff = req.get_header_value("X-Forwarded-For");
    if (!xff.empty()) return xff.substr(0, xff.find(','));
    return req.remote_ip_address;
}
static std::string str_field(const njson& b, const char* k) {
    if (!b.contains(k) || !b[k].is_string()) return {};
    return b[k].get<std::string>();
}
static std::string lower_trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto z = s.find_last_not_of(" \t\r\n");
    s = a == std::string::npos ? "" : s.substr(a, z - a + 1);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
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

static std::string url_encode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
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
        if (!u->active)
            return crow::response{403, u->pending
                ? R"({"error":"tu cuenta está pendiente de aprobación de un administrador"})"
                : R"({"error":"usuario deshabilitado"})"};
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

        // Si ya hay una clave sin confirmar, se reutiliza: volver a entrar no
        // invalida el QR que la persona ya escaneó.
        std::string sec;
        if (u->totp_secret && !u->totp_secret->empty()) {
            sec = *u->totp_secret;
        } else {
            sec = auth::crypto::base32_random_secret(20);
            db::exec(tx, "UPDATE users SET totp_secret = $1 WHERE id = $2", sec, u->id);
        }
        tx.commit();

        // Formato estándar de Google Authenticator, Microsoft Authenticator, Authy, etc.
        auto url = "otpauth://totp/Regalitos%3A" + url_encode(u->email) +
                   "?secret=" + sec + "&issuer=Regalitos&algorithm=SHA1&digits=6&period=30";
        using qrcodegen::QrCode;
        // Corrección LOW: menos módulos, más grandes y fáciles de leer con la cámara.
        QrCode qr = QrCode::encodeText(url.c_str(), QrCode::Ecc::LOW);
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

        // REG-HAL-32: cada código sirve una sola vez. Se guarda el último paso
        // aceptado y solo se aceptan pasos posteriores (UPDATE atómico).
        auto code = str_field(body, "code");
        auto step = auth::crypto::totp_match_step(*u->totp_secret, code);
        bool ok = step && !db::exec(tx,
            "UPDATE users SET totp_last_step = $2 WHERE id = $1 "
            "AND (totp_last_step IS NULL OR totp_last_step < $2) RETURNING id",
            u->id, *step).empty();
        if (!ok) {
            db::exec(tx, FAIL_SQL, u->id);
            tx.commit();
            return crow::response{401, R"({"error":"código inválido o ya usado"})"};
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

    // ---- Registro: la cuenta queda pendiente hasta que un admin la active ----
    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
    ([&pool](const crow::request& req) {
        if (env("ALLOW_REGISTRATION", "true") == "false")
            return crow::response{403, R"({"error":"el registro está deshabilitado"})"};
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) return crow::response{400};
        auto email = lower_trim(str_field(body, "email"));
        auto pass  = str_field(body, "password");
        auto name  = str_field(body, "name");
        if (!db::is_email(email))
            return crow::response{400, R"({"error":"correo inválido"})"};
        if (pass.size() < 8 || pass.size() > 256 || db::is_blank(pass))
            return crow::response{400, R"({"error":"la contraseña tiene que tener al menos 8 caracteres"})"};
        if (name.size() > 120)
            return crow::response{400, R"({"error":"nombre demasiado largo"})"};

        auto ip   = client_ip(req);
        auto hash = auth::crypto::hash_password(pass);
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto recent = db::exec(tx,
            "SELECT count(*) FROM audit_log WHERE action = 'user.register' "
            "AND metadata->>'ip' = $1 AND created_at > now() - interval '1 hour'",
            ip)[0][0].as<int>();
        if (recent >= 5)
            return crow::response{429, R"({"error":"demasiados registros desde esta conexión, probá más tarde"})"};

        auto r = db::exec(tx,
            "INSERT INTO users (email, password_hash, name, role, active, pending) "
            "SELECT $1, $2, NULLIF($3, ''), 'employee', false, true "
            "WHERE NOT EXISTS (SELECT 1 FROM users WHERE lower(email) = $1) "
            "ON CONFLICT (email) DO NOTHING RETURNING id",
            email, hash, name);
        std::string uid = r.empty() ? "" : r[0][0].as<std::string>();
        db::exec(tx,
            "INSERT INTO audit_log (user_id, action, metadata) "
            "VALUES (NULLIF($1, '')::uuid, 'user.register', $2::jsonb)",
            uid, njson({{"ip", ip}, {"created", !uid.empty()}}).dump());
        std::vector<std::string> admins;
        if (!uid.empty())
            for (auto row : db::exec(tx,
                    "SELECT email FROM users WHERE role = 'admin' AND active"))
                admins.push_back(row[0].as<std::string>());
        tx.commit();

        // Aviso a los admins para que aprueben la cuenta (si hay correo configurado).
        if (!admins.empty() && mail::configured()) {
            std::thread([admins, email] {
                for (auto& to : admins) {
                    mail::Message m;
                    m.to = to;
                    m.subject = "Regalitos: nueva cuenta pendiente de aprobación";
                    m.text = "Se registró " + email + ". Activala desde " +
                             env("APP_URL", "") + "/admin si corresponde.\n";
                    m.html = "<p>Se registró <b>" + email + "</b>.</p><p>Activala desde "
                             "el panel de usuarios si corresponde.</p>";
                    std::string err;
                    if (!mail::send(m, err))
                        std::cerr << "[registro] No se pudo avisar a un admin: " << err << "\n";
                }
            }).detach();
        }
        // Misma respuesta exista o no la cuenta: no revela correos registrados.
        return crow::response{202,
            R"({"ok":true,"message":"Si el correo no estaba registrado, tu cuenta quedó creada. Un administrador tiene que aprobarla antes de que puedas entrar."})"};
    });

    // ---- Cambio de contraseña con sesión iniciada ----
    CROW_ROUTE(app, "/api/auth/password/change").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto me = auth::get_user(req, pool);
        if (!me) return auth::unauthorized();
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) return crow::response{400};
        auto current = str_field(body, "current_password");
        auto next    = str_field(body, "new_password");
        if (current.empty())
            return crow::response{400, R"({"error":"falta la contraseña actual"})"};
        if (next.size() < 8 || next.size() > 256 || db::is_blank(next))
            return crow::response{400, R"({"error":"la contraseña nueva tiene que tener al menos 8 caracteres"})"};
        if (next == current)
            return crow::response{400, R"({"error":"la contraseña nueva tiene que ser distinta de la actual"})"};

        auto new_hash = auth::crypto::hash_password(next);
        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto u = auth::find_by_id(tx, me->id);
        if (!u) return auth::unauthorized();
        if (is_locked(tx, u->id)) return locked_resp();
        if (!auth::crypto::verify_password(current, u->password_hash)) {
            db::exec(tx, FAIL_SQL, u->id);
            tx.commit();
            return crow::response{401, R"({"error":"la contraseña actual no es correcta"})"};
        }
        // Cierra las demás sesiones (session_version) y emite una nueva para esta.
        auto sv = db::exec(tx,
            "UPDATE users SET password_hash = $1, password_changed_at = now(), "
            "session_version = session_version + 1, failed_attempts = 0, locked_until = NULL "
            "WHERE id = $2 RETURNING session_version",
            new_hash, u->id)[0][0].as<int>();
        auth::log_audit(tx, u->id, "password.change", {{"ip", client_ip(req)}});
        tx.commit();

        crow::response res{200, R"({"ok":true})"};
        set_cookie(res, make_session(u->id, u->email, u->role, sv), SESSION_TTL);
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
