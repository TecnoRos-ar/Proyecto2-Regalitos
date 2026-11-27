#!/bin/sh
# Respaldo de la base con pg_dump (formato custom, comprimido).
# Uso:  DATABASE_URL=... BACKUP_DIR=./backups BACKUP_KEEP_DAYS=14 scripts/backup.sh
# Cron diario a las 3:00:
#   0 3 * * * cd /ruta/backend && . ./.env && scripts/backup.sh >> backups/backup.log 2>&1
# Restaurar:  pg_restore --clean --if-exists -d "$DATABASE_URL" backups/regalitos-AAAAMMDD-HHMM.dump
set -eu
: "${DATABASE_URL:?falta DATABASE_URL}"
DIR="${BACKUP_DIR:-./backups}"
KEEP="${BACKUP_KEEP_DAYS:-14}"
mkdir -p "$DIR"
FILE="$DIR/regalitos-$(date +%Y%m%d-%H%M).dump"
pg_dump --format=custom --no-owner --file="$FILE.tmp" "$DATABASE_URL"
mv "$FILE.tmp" "$FILE"
# Verifica que el archivo se pueda leer antes de borrar respaldos viejos.
pg_restore --list "$FILE" > /dev/null
find "$DIR" -name 'regalitos-*.dump' -mtime +"$KEEP" -delete
echo "Respaldo OK: $FILE ($(du -h "$FILE" | cut -f1))"
