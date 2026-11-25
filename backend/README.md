# Regalitos Argentinos — Backend

Backend C++20 (Crow + libpqxx) + PostgreSQL 16.

## Requisitos
- CMake >= 3.20
- vcpkg (exportá VCPKG_ROOT)
- Docker + Docker Compose
- g++ >= 11 (o clang >= 14)

## Arranque rápido

    docker compose up -d
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
    cmake --build build -j
    cp .env.example .env   # y completá JWT_SECRET, ADMIN_EMAIL, ADMIN_PASSWORD
    set -a; . ./.env; set +a
    ./build/regalitos

Prueba:   curl -s http://localhost:8080/health

`docker compose up` aplica las migraciones solo al crear la base. En una base ya creada,
las migraciones nuevas se aplican a mano, por ejemplo:

    psql "$DATABASE_URL" -f migrations/003_session_version.sql
    psql "$DATABASE_URL" -f migrations/004_password_reset.sql

La imagen (`Dockerfile`) las aplica sola al arrancar.

## Endpoints
- GET    /health
- GET    /api/materials
- POST   /api/materials
- PUT    /api/materials/:id
- GET    /api/products
- POST   /api/products
- POST   /api/production    { product_id, quantity }
- GET    /api/movements
- GET    /api/state, PUT /api/state   (estado de la app web)
- Auth: /api/auth/login, /api/auth/totp/{setup,enable,verify}, /api/auth/logout, /api/auth/me
- Recuperación de contraseña: POST /api/auth/password/forgot { email }, POST /api/auth/password/reset { token, password }; páginas /forgot y /reset
- Admin: /api/admin/users (GET/POST), /api/admin/users/:id (PUT/DELETE)

Todas las rutas /api/* (salvo /api/auth/login, totp y password) requieren sesión.
La recuperación de contraseña usa APP_URL y, para enviar el correo, MAIL_PROVIDER (resend o brevo), MAIL_API_KEY y MAIL_FROM. Sin esas tres no se envía correo:
el enlace se escribe en la salida del servidor (solo para pruebas).
Tras 5 intentos fallidos (password o 2FA) la cuenta se bloquea 15 minutos.
