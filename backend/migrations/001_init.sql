BEGIN;

CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  email TEXT UNIQUE NOT NULL,
  password_hash TEXT NOT NULL,
  name TEXT,
  role TEXT NOT NULL DEFAULT 'owner' CHECK (role IN ('owner','operator','viewer')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE materials (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
  unit TEXT NOT NULL CHECK (unit IN ('m','un','g')),
  current_stock NUMERIC(14,3) NOT NULL DEFAULT 0 CHECK (current_stock >= 0),
  last_purchase_price NUMERIC(14,4) NOT NULL DEFAULT 0 CHECK (last_purchase_price >= 0),
  last_purchase_qty   NUMERIC(14,3) NOT NULL DEFAULT 1 CHECK (last_purchase_qty > 0),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE stock_movements (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  material_id UUID NOT NULL REFERENCES materials(id),
  kind TEXT NOT NULL CHECK (kind IN
    ('purchase','production','adjustment','loss','return')),
  delta NUMERIC(14,3) NOT NULL CHECK (delta <> 0),
  unit_cost NUMERIC(14,4),
  reference_type TEXT,
  reference_id UUID,
  note TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX idx_movements_material ON stock_movements(material_id, created_at DESC);

CREATE TABLE products (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
  labor_cost NUMERIC(14,2) NOT NULL DEFAULT 0 CHECK (labor_cost >= 0),
  other_cost NUMERIC(14,2) NOT NULL DEFAULT 0 CHECK (other_cost >= 0),
  profit_type TEXT NOT NULL DEFAULT 'pct' CHECK (profit_type IN ('pct','fixed')),
  profit_value NUMERIC(14,4) NOT NULL DEFAULT 100 CHECK (profit_value >= 0),
  finished_stock INTEGER NOT NULL DEFAULT 0 CHECK (finished_stock >= 0),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE recipe_items (
  product_id UUID NOT NULL REFERENCES products(id) ON DELETE CASCADE,
  material_id UUID NOT NULL REFERENCES materials(id),
  quantity NUMERIC(14,3) NOT NULL CHECK (quantity > 0),
  PRIMARY KEY (product_id, material_id)
);

CREATE TABLE production_orders (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  product_id UUID NOT NULL REFERENCES products(id),
  quantity_requested INTEGER NOT NULL CHECK (quantity_requested > 0),
  quantity_produced  INTEGER NOT NULL DEFAULT 0 CHECK (quantity_produced >= 0),
  quantity_lost      INTEGER NOT NULL DEFAULT 0 CHECK (quantity_lost >= 0),
  status TEXT NOT NULL DEFAULT 'available' CHECK (status IN
    ('pending','reserved','in_progress','done','qc','available','cancelled')),
  cost_snapshot JSONB NOT NULL,
  created_by UUID REFERENCES users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  completed_at TIMESTAMPTZ
);

CREATE TABLE config (
  key TEXT PRIMARY KEY,
  value JSONB NOT NULL
);
INSERT INTO config(key, value) VALUES
  ('rounding', '100'::jsonb),
  ('default_profit_pct', '100'::jsonb);

CREATE OR REPLACE FUNCTION set_updated_at() RETURNS trigger AS $$
BEGIN NEW.updated_at = now(); RETURN NEW; END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_materials_updated BEFORE UPDATE ON materials
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();
CREATE TRIGGER trg_products_updated BEFORE UPDATE ON products
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();

COMMIT;
