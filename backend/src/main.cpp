#include <crow.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "config.hpp"
#include "db/pool.hpp"
#include "auth/crypto.hpp"
#include "auth/user_repo.hpp"
#include "mail/mailer.hpp"
#include <vector>

// Forward decls de las rutas
void register_material_routes(crow::SimpleApp&, ConnectionPool&);
void register_product_routes(crow::SimpleApp&, ConnectionPool&);
void register_production_routes(crow::SimpleApp&, ConnectionPool&);
void register_auth_routes(crow::SimpleApp&, ConnectionPool&);
void register_admin_routes(crow::SimpleApp&, ConnectionPool&);
void register_state_routes(crow::SimpleApp&, ConnectionPool&);
void register_password_routes(crow::SimpleApp&, ConnectionPool&);

// ---------- helpers ----------
static crow::response serve_file(const std::string& rel_path,
                                 const std::string& content_type) {
    auto base = env("PUBLIC_DIR", "public");
    std::ifstream f(base + "/" + rel_path);
    if (!f.is_open())
        return crow::response{404, "Archivo no encontrado: " + rel_path};
    std::stringstream ss;
    ss << f.rdbuf();
    auto res = crow::response{200, ss.str()};
    res.set_header("Content-Type", content_type);
    return res;
}

static void ensure_bootstrap_admin(ConnectionPool& pool) {
    auto conn = pool.acquire();
    pqxx::work tx{*conn};
    auto r = tx.exec("SELECT count(*) FROM users");
    if (r[0][0].as<int>() > 0) { tx.commit(); return; }

    auto email = env("ADMIN_EMAIL", "admin@regalitos.local");
    auto pass  = env("ADMIN_PASSWORD", "cambiar123");
    if (env("APP_ENV", "") == "production" && (pass == "cambiar123" || pass.size() < 12))
        throw std::runtime_error(
            "ADMIN_PASSWORD es la de ejemplo o tiene menos de 12 caracteres (APP_ENV=production)");
    auto hash  = auth::crypto::hash_password(pass);
    auth::create_user(tx, email, hash, "Admin", "admin");
    tx.commit();

    std::cout << "\n=============================================\n"
              << "[BOOTSTRAP] Usuario admin creado:\n"
              << "  Email:    " << email << "\n"
              << "  Password: (la definida en ADMIN_PASSWORD)\n"
              << "  Cambiala apenas puedas.\n"
              << "=============================================\n\n";
}

// Secretos y HTTPS: en producción no se arranca con valores de ejemplo.
static void check_production_config() {
    bool prod = env("APP_ENV", "") == "production";
    std::vector<std::string> errs, warns;
    auto jwt = env("JWT_SECRET", "");
    if (jwt.size() < 32 || jwt.find("gener") != std::string::npos)
        errs.push_back("JWT_SECRET ausente, corto (<32) o de ejemplo. Generalo con: openssl rand -hex 32");
    if (env("DATABASE_URL", "").find("regalitos:regalitos@") != std::string::npos)
        errs.push_back("DATABASE_URL usa la clave de ejemplo de la base");
    auto url = env("APP_URL", "");
    if (url.rfind("https://", 0) != 0 && env("COOKIE_SECURE", "") != "1")
        errs.push_back("APP_URL no es https:// (la cookie de sesión viajaría por HTTP)");
    if (!mail::configured())
        warns.push_back("correo sin configurar: no se envían enlaces de recuperación ni avisos de registro");
    for (auto& w : warns) CROW_LOG_WARNING << w;
    for (auto& e : errs) {
        if (prod) std::cerr << "CONFIG: " << e << "\n";
        else CROW_LOG_WARNING << e << " (se ignora fuera de APP_ENV=production)";
    }
    if (prod && !errs.empty())
        throw std::runtime_error("configuración insegura para producción");
}

// ---------- main ----------
int main() {
    try {
        auth::crypto::init();
        check_production_config();

        auto conn_str  = env("DATABASE_URL",
            "postgresql://regalitos:regalitos@localhost:5532/regalitos");
        auto pool_size = std::stoul(env("DB_POOL_SIZE", "10"));
        ConnectionPool pool{conn_str, pool_size};

        ensure_bootstrap_admin(pool);

        crow::SimpleApp app;

        // Datos con tipo o valor inválido: 400 en lugar de 500.
        app.exception_handler([](crow::response& res) {
            try {
                throw;
            } catch (const nlohmann::json::exception&) {
                res = crow::response{400, R"({"error":"tipo de dato inválido"})"};
            } catch (const pqxx::check_violation&) {
                res = crow::response{400, R"({"error":"valor fuera de rango"})"};
            } catch (const pqxx::data_exception&) {
                res = crow::response{400, R"({"error":"valor fuera de rango"})"};
            } catch (const std::exception& e) {
                CROW_LOG_ERROR << "Excepción sin atrapar: " << e.what();
                res = crow::response{500, R"({"error":"error interno"})"};
            }
        });

        // ---- Health ----
        CROW_ROUTE(app, "/health")([&pool] {
            auto conn = pool.acquire();
            pqxx::work tx{*conn};
            tx.exec("SELECT 1");
            tx.commit();
            return crow::response{200, R"({"status":"ok"})"};
        });

        // ---- Páginas ----
        CROW_ROUTE(app, "/")      ([] { return serve_file("index.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/login") ([] { return serve_file("login.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/admin") ([] { return serve_file("admin.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/forgot")([] { return serve_file("forgot.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/reset") ([] { return serve_file("reset.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/register")([] { return serve_file("register.html", "text/html; charset=utf-8"); });
        CROW_ROUTE(app, "/account") ([] { return serve_file("account.html", "text/html; charset=utf-8"); });

        // ---- APIs ----
        register_auth_routes      (app, pool);
        register_admin_routes     (app, pool);
        register_material_routes  (app, pool);
        register_product_routes   (app, pool);
        register_production_routes(app, pool);
        register_state_routes     (app, pool);
        register_password_routes  (app, pool);

        auto port = std::stoi(env("PORT", "8080"));
        CROW_LOG_INFO << "regalitos-backend escuchando en :" << port;
        app.port(port).multithreaded().run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << "\n\n"
                  << "Checklist:\n"
                  << "  1) docker compose up -d          (DB arriba)\n"
                  << "  2) docker compose ps             (dice healthy)\n"
                  << "  3) source .env                   (DATABASE_URL, JWT_SECRET)\n"
                  << "  4) ./build/regalitos\n\n";
        return 1;
    }
}
