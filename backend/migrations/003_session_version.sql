BEGIN;

-- Cerrar sesión aumenta este número; las sesiones emitidas con otro valor dejan de valer.
ALTER TABLE users ADD COLUMN IF NOT EXISTS session_version INT NOT NULL DEFAULT 0;

COMMIT;
