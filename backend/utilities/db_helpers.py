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

def get_devices_by_user_id(user_id: int) -> list:
    with engine.connect() as conn:
        result = conn.execute(text(
            '''
            SELECT id, device_mac, device_name, os_version, asset_id, created_at
            FROM devices
            WHERE user_id = :user_id
            ORDER BY created_at DESC
            '''
        ), {"user_id": user_id}).fetchall()
        
        devices = []
        for row in result:
            devices.append({
                "id": row.id,
                "enrollment_id": row.device_mac,
                "device_name": row.device_name,
                "os_version": row.os_version,
                "asset_id": row.asset_id,
                "created_at": row.created_at.isoformat() if row.created_at else None
            })
        return devices
