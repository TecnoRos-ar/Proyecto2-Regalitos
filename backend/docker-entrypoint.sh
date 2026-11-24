#!/bin/sh
# Aplica las migraciones pendientes y arranca el backend.
set -e

if [ -z "$DATABASE_URL" ]; then
  echo "FATAL: falta la variable DATABASE_URL" >&2
  exit 1
fi

echo "Esperando a la base de datos..."
until pg_isready -d "$DATABASE_URL" -q; do sleep 2; done

psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -q -c \
  "CREATE TABLE IF NOT EXISTS schema_migrations (
     filename   TEXT PRIMARY KEY,
     applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
   );"

for f in /app/migrations/*.sql; do
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

echo "Migraciones al día. Arrancando backend..."
exec /app/regalitos
