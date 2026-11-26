#!/bin/sh
# Aplica las migraciones pendientes (registradas en schema_migrations).
# Uso local:  DATABASE_URL=... scripts/migrate.sh   (lo usa también la imagen al arrancar)
set -e
: "${DATABASE_URL:?falta DATABASE_URL}"
MIGRATIONS_DIR="${MIGRATIONS_DIR:-$(dirname "$0")/../migrations}"
export PGOPTIONS="--client-min-messages=warning"

psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -q -c \
  "CREATE TABLE IF NOT EXISTS schema_migrations (
     filename   TEXT PRIMARY KEY,
     applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
   );"

# Base creada con docker-compose: initdb ya aplicó 001 y 002; se registran sin repetirlas.
registradas=$(psql "$DATABASE_URL" -tAc "SELECT count(*) FROM schema_migrations")
con_esquema=$(psql "$DATABASE_URL" -tAc "SELECT to_regclass('public.audit_log') IS NOT NULL")
if [ "$registradas" = "0" ] && [ "$con_esquema" = "t" ]; then
  psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -q -c \
    "INSERT INTO schema_migrations (filename) VALUES ('001_init.sql'), ('002_auth.sql')"
fi

for f in "$MIGRATIONS_DIR"/*.sql; do
  name=$(basename "$f")
  done_already=$(psql "$DATABASE_URL" -tAc \
    "SELECT 1 FROM schema_migrations WHERE filename = '$name'")
  if [ "$done_already" != "1" ]; then
    echo "Aplicando migración $name"
    psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -q -f "$f"
    psql "$DATABASE_URL" -q -c \
      "INSERT INTO schema_migrations (filename) VALUES ('$name')"
  fi
done

echo "Migraciones al día."
