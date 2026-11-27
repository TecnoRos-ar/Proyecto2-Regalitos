#!/bin/sh
# Aplica las migraciones pendientes y arranca el backend.
set -e
export PGOPTIONS="--client-min-messages=warning"

if [ -z "$DATABASE_URL" ]; then
  echo "FATAL: falta la variable DATABASE_URL" >&2
  exit 1
fi

echo "Esperando a la base de datos..."
until pg_isready -d "$DATABASE_URL" -q; do sleep 2; done

MIGRATIONS_DIR=/app/migrations /app/migrate.sh

echo "Arrancando backend..."
exec /app/regalitos
