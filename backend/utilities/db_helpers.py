import os
from sqlalchemy import create_engine, text

DATABASE_URL = str(os.getenv("DATABASE_URL"))

engine = create_engine(DATABASE_URL, echo=False, future=True, pool_pre_ping=True)

# --- User Related DB Operations ---
def insert_user(username, email, password_hash, role, organization):
    with engine.connect() as conn:
        stmt = text(
            '''
            INSERT INTO users (username, email, password_hash, role, organization)
            VALUES (:username, :email, :password_hash, :role, :organization)
            '''
        )
        conn.execute(stmt, {
            "username": username,
            "email": email,
            "password_hash": password_hash,
            "role": role,
            "organization": organization
        })
        conn.commit()

def get_user_by_email(email) -> dict | None:
    with engine.connect() as conn:
        result = conn.execute(text(
            '''
            SELECT id, username, email, password_hash, role, organization
            FROM users
            WHERE email = :email
            '''
        ), {"email": email}).fetchone()
        if result:
            return {
                "id": result.id,
                "username": result.username,
                "email": result.email,
                "password_hash": result.password_hash,
                "role": result.role,
                "organization": result.organization
            }
        return None

def get_user_by_id(user_id) -> dict | None:
    with engine.connect() as conn:
        result = conn.execute(text(
            '''
            SELECT id, username, email, password_hash, role, organization
            FROM users
            WHERE id = :user_id
            '''
        ), {"user_id": user_id}).fetchone()
        if result:
            return {
                "id": result.id,
                "username": result.username,
                "email": result.email,
                "password_hash": result.password_hash,
                "role": result.role,
                "organization": result.organization
            }
        return None    

# --- Token Related Operations ---
def insert_refresh_token(jti, user_id, expires_at):
    with engine.connect() as conn:
        stmt = text(
            '''
            INSERT INTO refresh_tokens (jti, user_id, expires_at)
            VALUES (:jti, :user_id, :expires_at)
            '''
        )
        conn.execute(stmt, {
            "jti": jti,
            "user_id": user_id,
            "expires_at": expires_at
        })
        conn.commit()

def revoke_refresh_token(jti):
    with engine.connect() as conn:
        stmt = text(
            '''
            UPDATE refresh_tokens
            SET revoked = TRUE
            WHERE jti = :jti
            '''
        )
        conn.execute(stmt, {"jti": jti})
        conn.commit()

def is_refresh_token_revoked(jti) -> bool:
    with engine.connect() as conn:
        result = conn.execute(text(
            '''
            SELECT revoked
            FROM refresh_tokens
            WHERE jti = :jti
            '''
        ), {"jti": jti}).fetchone()
        #  result is None or a row object with a 'revoked' attribute
        if result is None:
            return True  # treat non-existent token as revoked
        return result.revoked

# --- Device Provisioning Token (SLPT) Related Operations ---
def insert_provisioning_token(slpt_value, user_id, enrollment_id, expires_at):
    with engine.begin() as conn:
        conn.execute(text(
            '''
            INSERT INTO provisioning_tokens (slpt_value, user_id, enrollment_id, expires_at)
            VALUES (:slpt_value, :user_id, :enrollment_id, :expires_at)
            '''
        ), {
            "slpt_value": slpt_value,
            "user_id": user_id,
            "enrollment_id": enrollment_id,
            "expires_at": expires_at
        })

def get_provisioning_token(slpt_value) -> dict | None:
    with engine.connect() as conn:
        # fetch token, associated user, enrollment_id, and expiry
        r = conn.execute(text(
            "SELECT user_id, enrollment_id, expires_at, is_used FROM provisioning_tokens WHERE slpt_value = :slpt"
        ), {"slpt": slpt_value}).mappings().first()
        if r is None:
            return None
        return dict(r)

def activate_device_in_db(mac, user_id, mqtt_pass, os_version):
    with engine.begin() as conn:
        # Creating permanent device record
        conn.execute(text(
            '''
            INSERT INTO devices (device_mac, user_id, mqtt_password, os_version) 
            VALUES (:mac, :user_id, :mqtt_pass, :os_version)
            ON CONFLICT (device_mac) DO UPDATE SET mqtt_password = :pass
            '''
        ), {
            "mac": mac,
            "user_id": user_id,
            "mqtt_pass": mqtt_pass,
            "pass": mqtt_pass,
            "os_version": os_version
        })
        conn.execute(text(
            "UPDATE provisioning_tokens SET is_used = TRUE WHERE enrollment_id = :mac"
        ), {"mac": mac})

def get_devices_with_pieces_for_org(user_id: int) -> list:
    """
    List every device belonging to any user in the requesting user's organization.
    Each device row is enriched with:
      - `latest_piece`: the most recent piece row (or None)
      - `rollup`:       today's piece aggregates for that device
    """
    with engine.connect() as conn:
        result = conn.execute(text(
            '''
            WITH user_org AS (
                SELECT organization FROM users WHERE id = :user_id
            ),
            org_devices AS (
                SELECT d.id, d.device_mac, d.device_name, d.os_version, d.asset_id, d.created_at
                FROM devices d
                JOIN users u   ON u.id = d.user_id
                JOIN user_org o ON o.organization = u.organization
            ),
            piece_rollup AS (
                SELECT
                    device_mac,
                    COUNT(*)                       AS pieces_today,
                    AVG(total_cycle_time_ms)       AS avg_cycle_time_ms,
                    AVG(total_stitching_ms::float / NULLIF(total_cycle_time_ms, 0)) AS avg_stitching_ratio,
                    AVG(trim_and_wipe_time_ms)     AS avg_trim_wipe_ms,
                    SUM(adjustment_count)          AS total_adjustments_today,
                    MAX(received_at)               AS last_piece_at
                FROM pieces
                WHERE received_at::date = CURRENT_DATE
                GROUP BY device_mac
            )
            SELECT
                od.id, od.device_mac, od.device_name, od.os_version, od.asset_id, od.created_at,
                pr.pieces_today, pr.avg_cycle_time_ms, pr.avg_stitching_ratio,
                pr.avg_trim_wipe_ms, pr.total_adjustments_today, pr.last_piece_at,
                lp.piece_seq              AS latest_piece_seq,
                lp.total_cycle_time_ms    AS latest_cycle_time_ms,
                lp.total_stitching_ms     AS latest_stitching_ms,
                lp.total_adjustment_ms    AS latest_adjustment_ms,
                lp.adjustment_count       AS latest_adjustment_count,
                lp.trim_and_wipe_time_ms  AS latest_trim_wipe_ms,
                lp.status                 AS latest_status,
                lp.received_at            AS latest_received_at
            FROM org_devices od
            LEFT JOIN piece_rollup pr ON pr.device_mac = od.device_mac
            LEFT JOIN LATERAL (
                SELECT piece_seq, total_cycle_time_ms, total_stitching_ms,
                       total_adjustment_ms, adjustment_count, trim_and_wipe_time_ms,
                       status, received_at
                FROM pieces
                WHERE device_mac = od.device_mac
                ORDER BY received_at DESC
                LIMIT 1
            ) lp ON TRUE
            ORDER BY od.created_at DESC
            '''
        ), {"user_id": user_id}).fetchall()

        devices = []
        for r in result:
            devices.append({
                "id":            r.id,
                "enrollment_id": r.device_mac,
                "device_name":   r.device_name,
                "os_version":    r.os_version,
                "asset_id":      r.asset_id,
                "created_at":    r.created_at.isoformat() if r.created_at else None,
                "rollup": {
                    "pieces_today":            int(r.pieces_today or 0),
                    "avg_cycle_time_ms":       float(r.avg_cycle_time_ms) if r.avg_cycle_time_ms is not None else None,
                    "avg_stitching_ratio":     float(r.avg_stitching_ratio) if r.avg_stitching_ratio is not None else None,
                    "avg_trim_wipe_ms":        float(r.avg_trim_wipe_ms) if r.avg_trim_wipe_ms is not None else None,
                    "total_adjustments_today": int(r.total_adjustments_today or 0),
                    "last_piece_at":           r.last_piece_at.isoformat() if r.last_piece_at else None,
                },
                "latest_piece": {
                    "piece_seq":             r.latest_piece_seq,
                    "total_cycle_time_ms":   r.latest_cycle_time_ms,
                    "total_stitching_ms":    r.latest_stitching_ms,
                    "total_adjustment_ms":   r.latest_adjustment_ms,
                    "adjustment_count":      r.latest_adjustment_count,
                    "trim_and_wipe_time_ms": r.latest_trim_wipe_ms,
                    "status":                r.latest_status,
                    "received_at":           r.latest_received_at.isoformat() if r.latest_received_at else None,
                } if r.latest_received_at else None,
            })
        return devices


def get_device_in_org(device_id: int, user_id: int) -> dict | None:
    """Fetch one device by id, scoped to the requester's organization (not just owner)."""
    with engine.connect() as conn:
        row = conn.execute(text(
            '''
            SELECT d.id, d.device_mac, d.device_name
            FROM devices d
            JOIN users u ON u.id = d.user_id
            WHERE d.id = :device_id
              AND u.organization = (SELECT organization FROM users WHERE id = :user_id)
            '''
        ), {"device_id": device_id, "user_id": user_id}).fetchone()
        if row is None:
            return None
        return {"id": row.id, "device_mac": row.device_mac, "device_name": row.device_name}


def get_pieces_for_device(device_mac: str, limit: int = 20) -> list:
    """Return the last `limit` pieces for a device, oldest-first (chart-friendly)."""
    with engine.connect() as conn:
        result = conn.execute(text(
            '''
            SELECT piece_seq, piece_started_at_ms, piece_completed_at_ms,
                   idle_before_ms, total_cycle_time_ms, total_stitching_ms,
                   total_adjustment_ms, total_stitches, avg_stitch_hz,
                   adjustment_count, trim_and_wipe_time_ms, status,
                   segments, received_at
            FROM pieces
            WHERE device_mac = :device_mac
            ORDER BY received_at DESC
            LIMIT :limit
            '''
        ), {"device_mac": device_mac, "limit": limit}).fetchall()
        rows = list(reversed(result))
        return [{
            "piece_seq":             r.piece_seq,
            "piece_started_at_ms":   r.piece_started_at_ms,
            "piece_completed_at_ms": r.piece_completed_at_ms,
            "idle_before_ms":        r.idle_before_ms,
            "total_cycle_time_ms":   r.total_cycle_time_ms,
            "total_stitching_ms":    r.total_stitching_ms,
            "total_adjustment_ms":   r.total_adjustment_ms,
            "total_stitches":        r.total_stitches,
            "avg_stitch_hz":         float(r.avg_stitch_hz) if r.avg_stitch_hz is not None else None,
            "adjustment_count":      r.adjustment_count,
            "trim_and_wipe_time_ms": r.trim_and_wipe_time_ms,
            "status":                r.status,
            "segments":              r.segments,   # JSONB → list of dicts already
            "received_at":           r.received_at.isoformat() if r.received_at else None,
        } for r in rows]


def get_org_summary(user_id: int) -> dict:
    """Org-wide roll-up for the header strip: devices, activity, throughput, stitching ratio."""
    with engine.connect() as conn:
        row = conn.execute(text(
            '''
            WITH user_org AS (
                SELECT organization FROM users WHERE id = :user_id
            ),
            org_devices AS (
                SELECT d.device_mac
                FROM devices d
                JOIN users u   ON u.id = d.user_id
                JOIN user_org o ON o.organization = u.organization
            ),
            today AS (
                SELECT device_mac, total_stitching_ms, total_cycle_time_ms,
                       total_adjustment_ms, trim_and_wipe_time_ms
                FROM pieces
                WHERE device_mac IN (SELECT device_mac FROM org_devices)
                  AND received_at::date = CURRENT_DATE
            ),
            active AS (
                SELECT DISTINCT device_mac
                FROM pieces
                WHERE device_mac IN (SELECT device_mac FROM org_devices)
                  AND received_at > NOW() - INTERVAL '5 minutes'
            )
            SELECT
                (SELECT COUNT(*) FROM org_devices)                              AS total_devices,
                (SELECT COUNT(*) FROM active)                                   AS active_devices,
                (SELECT COUNT(*) FROM today)                                    AS pieces_today,
                (SELECT COALESCE(SUM(total_stitching_ms), 0) FROM today)        AS sum_stitching_ms,
                (SELECT COALESCE(SUM(total_cycle_time_ms), 0) FROM today)       AS sum_cycle_ms,
                (SELECT COALESCE(SUM(total_adjustment_ms), 0) FROM today)       AS sum_adjustment_ms,
                (SELECT COALESCE(AVG(trim_and_wipe_time_ms), 0) FROM today)     AS avg_trim_wipe_ms,
                (SELECT organization FROM user_org)                             AS organization
            '''
        ), {"user_id": user_id}).fetchone()

        total   = int(row.total_devices or 0)
        active  = int(row.active_devices or 0)
        cycle   = int(row.sum_cycle_ms or 0)
        stitch  = int(row.sum_stitching_ms or 0)
        return {
            "organization":         row.organization,
            "total_devices":        total,
            "active_devices":       active,
            "idle_devices":         max(total - active, 0),
            "pieces_today":         int(row.pieces_today or 0),
            "stitching_ratio_pct":  (stitch * 100.0 / cycle) if cycle > 0 else 0.0,
            "sum_stitching_ms":     stitch,
            "sum_cycle_ms":         cycle,
            "sum_adjustment_ms":    int(row.sum_adjustment_ms or 0),
            "avg_trim_wipe_ms":     float(row.avg_trim_wipe_ms) if row.avg_trim_wipe_ms is not None else 0.0,
        }
