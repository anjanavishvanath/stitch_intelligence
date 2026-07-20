# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Stitch Intelligence** is an Industrial IoT platform for real-time sewing machine efficiency monitoring in garment production lines. ESP32-S3 hardware nodes capture telemetry (stitch counts, foot lifts, thread trims) from Juki/Hoshin industrial machines and publish via MQTT to a central gateway. A Flask REST API aggregates data into TimescaleDB, and a React dashboard provides production line visibility and bottleneck detection.

## Common Commands

### Full Stack (Docker)
```bash
docker compose up -d          # Start all services
docker compose down           # Stop all services
docker compose logs -f        # Stream logs
docker compose up --build     # Rebuild and restart
```

### Frontend (from `frontend/`)
```bash
npm install        # Install dependencies
npm run dev        # Vite dev server with HMR (port 3000)
npm run build      # Production build to /dist
npm run lint       # ESLint validation
npm run test       # Vitest unit tests
npm run test -- --reporter=verbose  # Verbose test output
```

### Backend (from `backend/`)
```bash
pip install -r requirements.txt
python main.py     # Flask dev server (port 5000)
```

## Architecture

### Services (Docker Compose)
| Service | Image | Port |
|---|---|---|
| `backend` | python:3.11-slim | 5000 |
| `frontend` | node:22-alpine → nginx:alpine | 3000 |
| `database` | timescale/timescaledb:latest-pg14 | 5432 |
| `mqtt_broker` | eclipse-mosquitto:latest | 1883 |
| `mqtt_ingestor` | same as backend | — |

All services share the `mqtt_network` bridge.

### Backend (`backend/`)
- **`main.py`** — Flask entry point, registers all routes, enables CORS
- **`utilities/auth_helpers.py`** — JWT issuance, bcrypt hashing, auth route handlers
- **`utilities/db_helpers.py`** — SQLAlchemy CRUD for users, tokens, devices, assets
- **`utilities/device_helpers.py`** — SLPT provisioning token generation, device activation, MQTT credential generation
- **`mqtt_ingestor.py`** — Separate process; subscribes to all MQTT topics (`#`), logs to stdout, auto-reconnects with 5s backoff

### Frontend (`frontend/`)
- **`src/api.js`** — Axios instance with JWT Bearer injection and automatic token refresh on 401 (queues concurrent requests during refresh)
- **`src/context/AuthProvider.jsx`** — React Context for auth state; parses JWT payload from localStorage
- **`src/components/ProtectedRoute.jsx`** — Route guard wrapping authenticated pages
- Pages: `/login`, `/signup`, `/dashboard`, `/sensor_registry` (device listing + provisioning)

### Database Schema (`sql/init_schema.sql`)
Five tables: `users`, `refresh_tokens` (JTI revocation tracking), `provisioning_tokens` (SLPT, one-use), `devices` (MAC address + MQTT credentials), `assets` (sewing machines).

## Key Flows

**Authentication:** Dual JWT (access: 15 min, refresh: 7 days). Refresh token JTIs are stored in DB and marked revoked on logout. Frontend auto-refreshes silently and queues pending requests.

**Device Provisioning (SLPT):**
1. User submits ESP32 MAC → backend generates 12-char hex SLPT (10 min expiry)
2. SLPT entered on ESP32 web interface → `POST /api/devices/activate` validates token, creates device record, issues MQTT credentials
3. ESP32 connects to Mosquitto broker

## Environment Variables

**Backend (`.env`):**
- `JWT_SECRET_KEY` — min 32 bytes
- `JWT_ACCESS_EXPIRES_SEC` / `JWT_REFRESH_EXPIRES_SEC`
- `DATABASE_URL` — PostgreSQL connection string
- `SLPT_EXPIRY_MINUTES` — default 10
- `MQTT_BROKER_URL`

**Frontend (`.env`):**
- `VITE_API_BASE_URL` — e.g. `http://localhost:5000`

## Frontend Build Notes

Vite uses the Rolldown bundler (configured in `vite.config.js`). The multi-stage Dockerfile builds the React app then serves the `/dist` output via Nginx with SPA fallback routing (`try_files $uri /index.html`).
