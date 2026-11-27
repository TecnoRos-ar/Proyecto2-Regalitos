BEGIN;

-- REG-HAL-32: último paso TOTP aceptado; un código no se acepta dos veces.
ALTER TABLE users ADD COLUMN IF NOT EXISTS totp_last_step BIGINT;

-- Registro público: la cuenta queda pendiente hasta que un admin la active.
ALTER TABLE users ADD COLUMN IF NOT EXISTS pending BOOLEAN NOT NULL DEFAULT false;

-- REG-HAL-43 / REG-HAL-09: cada guardado deja la versión anterior para poder restaurarla.
CREATE TABLE IF NOT EXISTS state_history (
  id         BIGSERIAL PRIMARY KEY,
  value      JSONB NOT NULL,
  saved_by   UUID REFERENCES users(id) ON DELETE SET NULL,
  reason     TEXT NOT NULL DEFAULT 'save',
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_state_history_created ON state_history(created_at DESC);

COMMIT;
