// src/api/routes_password.cpp
// Recuperación de contraseña: "¿Olvidaste tu contraseña?" + enlace por mail.
#include <crow.h>
#include <sodium.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>

#include "db/pool.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"
#include "mail/mailer.hpp"
#include "config.hpp"

using njson = nlohmann::json;

static const int RESET_TTL_MIN      = 30;  // validez del enlace
static const int MAX_PER_HOUR       = 3;   // pedidos por usuario por hora
static const size_t MIN_PASSWORD_LEN = 8;  // igual que el alta de usuarios

static std::string random_token() {
    unsigned char buf[32];
    randombytes_buf(buf, sizeof buf);
    char hex[sizeof buf * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, buf, sizeof buf);
    return hex;
}

static std::string sha256_hex(const std::string& s) {
    unsigned char out[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(out, reinterpret_cast<const unsigned char*>(s.data()), s.size());
    char hex[sizeof out * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, out, sizeof out);
    return hex;
}

static bool is_hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// URL pública de la app. No se usa el header Host: se podría falsificar
// para mandar el enlace a otro dominio.
static std::string base_url() {
    auto u = env("APP_URL", "");
    if (u.empty()) u = env("RENDER_EXTERNAL_URL", "");
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

static std::string client_ip(const crow::request& req) {
    auto xff = req.get_header_value("X-Forwarded-For");
    if (!xff.empty()) return xff.substr(0, xff.find(','));
    return req.remote_ip_address;
}

static mail::Message reset_email(const std::string& to, const std::string& link) {
    mail::Message m;
    m.to = to;
    m.subject = "Recuperá tu contraseña de Regalitos";
    m.text =
        "Hola,\n\n"
        "Recibimos un pedido para cambiar la contraseña de tu cuenta de Regalitos.\n"
        "Para elegir una nueva, abrí este enlace (vence en " +
        std::to_string(RESET_TTL_MIN) + " minutos):\n\n" + link + "\n\n"
        "Si no lo pediste vos, ignorá este mail: tu contraseña no cambia.\n";
    m.html =
        "<div style=\"font-family:system-ui,sans-serif;font-size:15px;color:#182036;"
        "max-width:480px\">"
        "<h2 style=\"margin:0 0 12px\">Recuperá tu contraseña</h2>"
        "<p>Recibimos un pedido para cambiar la contraseña de tu cuenta de Regalitos.</p>"
        "<p><a href=\"" + link + "\" style=\"display:inline-block;background:#2B78C9;"
        "color:#fff;padding:12px 18px;border-radius:10px;text-decoration:none;"
        "font-weight:600\">Elegir una contraseña nueva</a></p>"
        "<p style=\"color:#5B6480;font-size:13px\">El enlace vence en " +
        std::to_string(RESET_TTL_MIN) + " minutos y sirve una sola vez.<br>"
        "Si no lo pediste vos, ignorá este mail: tu contraseña no cambia.</p></div>";
    return m;
}

void register_password_routes(crow::SimpleApp& app, ConnectionPool& pool) {

    // Paso 1: pedir el enlace. Siempre responde lo mismo, exista o no el
    // email, para no revelar qué cuentas están registradas.
    CROW_ROUTE(app, "/api/auth/password/forgot").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) return crow::response{400};
        auto email = body.value("email", "");
        if (email.empty())
            return crow::response{400, R"({"error":"email requerido"})"};

        auto generic = [] {
            return crow::response{200,
                R"({"ok":true,"message":"Si el email está registrado, te enviamos un enlace para cambiar la contraseña."})"};
        };

        std::string token;
        {
            auto conn = pool.acquire();
            pqxx::work tx{*conn};
            auto u = auth::find_by_email(tx, email);
            if (!u || !u->active) { tx.commit(); return generic(); }

            auto recent = db::exec(tx,
                "SELECT count(*) FROM password_resets "
                "WHERE user_id = $1 AND created_at > now() - interval '1 hour'",
                u->id)[0][0].as<int>();
            if (recent >= MAX_PER_HOUR) { tx.commit(); return generic(); }

            token = random_token();
            // Un enlace nuevo anula los anteriores que no se usaron.
            db::exec(tx,
                "UPDATE password_resets SET used_at = now() "
                "WHERE user_id = $1 AND used_at IS NULL", u->id);
            db::exec(tx,
                "INSERT INTO password_resets (user_id, token_hash, expires_at, requested_ip) "
                "VALUES ($1, $2, now() + make_interval(mins => $3), $4)",
                u->id, sha256_hex(token), RESET_TTL_MIN, client_ip(req));
            auth::log_audit(tx, u->id, "password_reset_requested",
                            {{"ip", client_ip(req)}});
            tx.commit();
        }

        auto base = base_url();
        if (base.empty()) {
            std::cerr << "[password-reset] Falta APP_URL: no se puede armar el enlace.\n";
            return generic();
        }
        auto link = base + "/reset?token=" + token;
        auto msg  = reset_email(email, link);

        // El envío va en segundo plano: así la respuesta tarda lo mismo
        // exista o no la cuenta.
        std::thread([msg, link] {
            if (!mail::configured()) {
                std::cout << "[password-reset] MAIL_PROVIDER sin configurar. "
                             "Enlace (solo para pruebas): " << link << std::endl;
                return;
            }
            std::string err;
            if (!mail::send(msg, err))
                std::cerr << "[password-reset] No se pudo enviar el mail: " << err << "\n";
        }).detach();

        return generic();
    });

    // Paso 2: elegir la contraseña nueva con el token del enlace.
    CROW_ROUTE(app, "/api/auth/password/reset").methods("POST"_method)
    ([&pool](const crow::request& req) {
        auto body = njson::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) return crow::response{400};
        auto token = body.value("token", "");
        auto pass  = body.value("password", "");

        auto invalid = [] {
            return crow::response{400,
                R"({"error":"El enlace no es válido o ya venció. Pedí uno nuevo."})"};
        };
        if (!is_hex64(token)) return invalid();
        if (pass.size() < MIN_PASSWORD_LEN)
            return crow::response{400,
                R"({"error":"La contraseña tiene que tener al menos 8 caracteres."})"};

        auto new_hash = auth::crypto::hash_password(pass);

        auto conn = pool.acquire();
        pqxx::work tx{*conn};
        auto r = db::exec(tx,
            "SELECT pr.user_id FROM password_resets pr "
            "JOIN users u ON u.id = pr.user_id "
            "WHERE pr.token_hash = $1 AND pr.used_at IS NULL "
            "  AND pr.expires_at > now() AND u.active "
            "FOR UPDATE OF pr",
            sha256_hex(token));
        if (r.empty()) return invalid();
        auto user_id = r[0][0].as<std::string>();

        // password_changed_at = now() cierra las sesiones abiertas antes del cambio.
        db::exec(tx,
            "UPDATE users SET password_hash = $1, password_changed_at = now(), "
            "failed_attempts = 0, locked_until = NULL WHERE id = $2",
            new_hash, user_id);
        db::exec(tx,
            "UPDATE password_resets SET used_at = now() "
            "WHERE user_id = $1 AND used_at IS NULL", user_id);
        auth::log_audit(tx, user_id, "password_reset_completed",
                        {{"ip", client_ip(req)}});
        tx.commit();

        return crow::response{200, R"({"ok":true})"};
    });
}
