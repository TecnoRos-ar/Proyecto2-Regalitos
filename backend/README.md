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
- Registro: POST /api/auth/register { email, password, name? } → la cuenta queda pendiente
  hasta que un admin la aprueba en /admin. Página /register. `ALLOW_REGISTRATION=false` lo apaga.
- Cambio de contraseña: POST /api/auth/password/change { current_password, new_password }
  (con sesión). Cierra las demás sesiones. Página /account.
- Historial del estado (admin): GET /api/state/history, POST /api/state/history/:id/restore

## Permisos
| Acción | admin | employee |
|---|---|---|
| Ver y editar materiales/productos, producir | sí | sí |
| Borrar materiales o productos (en /api/state o DELETE /api/materials/:id) | sí | no (403) |
| Usuarios, aprobar registros, historial y restaurar | sí | no |

## Producción
- `APP_ENV=production`: el servidor no arranca si `JWT_SECRET` es de ejemplo o corto, si la
  base usa la clave de ejemplo, si `APP_URL` no es `https://`, o si al crear el admin
  `ADMIN_PASSWORD` es la de ejemplo o tiene menos de 12 caracteres.
- HTTPS: lo da la plataforma (Render) o un proxy delante (Caddy/Nginx). Con `APP_URL=https://…`
  la cookie de sesión lleva `Secure`.
- Correo: `MAIL_PROVIDER` (resend|brevo), `MAIL_API_KEY`, `MAIL_FROM` con dominio verificado.
  `MAIL_PROVIDER=log` escribe el enlace de recuperación en la salida (solo pruebas locales).
- Migraciones: la imagen las aplica al arrancar. Sin imagen: `DATABASE_URL=… scripts/migrate.sh`.
- Respaldo: `scripts/backup.sh` (pg_dump, guarda 14 días). Ejemplo de cron en el script.
  Además, cada guardado del estado deja la versión anterior en `state_history` (últimas 500).

## Pruebas
- Unitarias: `./build/tests`
- De punta a punta (backend levantado sobre una base vacía, con `COOKIE_SECURE=1`):
  `BASE=http://127.0.0.1:8080 ADMIN_EMAIL=… ADMIN_PASSWORD=… python3 -I tests/e2e_cuentas.py`

Todas las rutas /api/* (salvo /api/auth/login, totp y password) requieren sesión.
La recuperación de contraseña usa APP_URL y, para enviar el correo, MAIL_PROVIDER (resend o brevo), MAIL_API_KEY y MAIL_FROM. Sin esas tres no se envía correo:
el enlace se escribe en la salida del servidor (solo para pruebas).
Tras 5 intentos fallidos (password o 2FA) la cuenta se bloquea 15 minutos.
