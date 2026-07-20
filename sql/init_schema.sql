-- NOTE: TimescaleDB extension removed for Neon compatibility.
-- When telemetry ingestion needs hypertables, migrate to Timescale Cloud
-- or self-hosted TimescaleDB and re-add: CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;

-- Users table
CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL,
    organization TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS refresh_tokens (
    id SERIAL PRIMARY KEY,
    jti TEXT UNIQUE NOT NULL,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    revoked BOOLEAN NOT NULL DEFAULT FALSE,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- provisioning tokens
CREATE TABLE IF NOT EXISTS provisioning_tokens(
    id SERIAL PRIMARY KEY,
    slpt_value TEXT UNIQUE NOT NULL,  -- the generated secret hex
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    enrollment_id TEXT NOT NULL,  -- The MAC address / Enrollment ID
    expires_at TIMESTAMPTZ,  -- When the token becomes invalid
    is_used BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- assets
CREATE TABLE IF NOT EXISTS assets (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    max_rpm INTEGER NOT NULL DEFAULT 0,
    power FLOAT NOT NULL DEFAULT 0,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    organization TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- devices
CREATE TABLE IF NOT EXISTS devices (
    id SERIAL PRIMARY KEY,
    device_mac TEXT UNIQUE NOT NULL,      -- The permanent physical ID
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_name TEXT,
    os_version TEXT,
    mqtt_password TEXT NOT NULL,
    asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- pieces: one row per completed (or abandoned) sewing cycle.
-- `segments` is JSONB so schema tweaks don't require a migration.
CREATE TABLE IF NOT EXISTS pieces (
    id                    BIGSERIAL PRIMARY KEY,
    device_mac            TEXT      NOT NULL,
    piece_seq             INTEGER,
    piece_started_at_ms   BIGINT,
    piece_completed_at_ms BIGINT,
    idle_before_ms        INTEGER,
    total_cycle_time_ms   INTEGER,
    total_stitching_ms    INTEGER,
    total_adjustment_ms   INTEGER,
    total_stitches        INTEGER,
    avg_stitch_hz         REAL,
    adjustment_count      INTEGER,
    trim_and_wipe_time_ms INTEGER,
    status                TEXT,
    segments              JSONB,
    received_at           TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_pieces_device_recv ON pieces (device_mac, received_at DESC);


