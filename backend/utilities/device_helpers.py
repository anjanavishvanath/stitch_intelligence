import secrets
import re
import os
from flask import request, jsonify
from flask_jwt_extended import get_jwt_identity
from datetime import datetime, timezone, timedelta
from .db_helpers import insert_provisioning_token, get_provisioning_token, activate_device_in_db, get_devices_by_user_id

SLPT_EXPIRY_MINUTES = int(os.getenv("SLPT_EXPIRY_MINUTES", 10))
BROKER_URL = os.getenv("MQTT_BROKER_URL", "192.168.1.2")

def generate_slpt(user_id: int, enrollment_id: str) -> dict:
    """Generates a SLPT and stores it in the DB linked to the user and MAC."""
    slpt_value = secrets.token_hex(6)
    expires_datetime = datetime.now(timezone.utc) + timedelta(minutes=SLPT_EXPIRY_MINUTES)
    expires_timestamp_unix = int(expires_datetime.timestamp()) #calculating unix timestamp (seconds since epoc) for js to understand
    insert_provisioning_token(
        slpt_value=slpt_value,
        user_id=user_id,
        enrollment_id=enrollment_id, #mac address
        expires_at=expires_datetime
    )
    return {
        "slpt": slpt_value,
        "expires_timestamp_unix": expires_timestamp_unix,
        "expires_in_seconds": SLPT_EXPIRY_MINUTES * 60
    }

def provision_device():
    user_identity = get_jwt_identity()
    user_id = int(user_identity)
    data = request.get_json() # parse request body
    enrollment_id_raw = data.get('enrollment_id')
    if not enrollment_id_raw:
        return jsonify({"msg":"Enrollment ID (MAC Address) is required"}), 400
    
    enrollment_id = enrollment_id_raw.upper().strip()
    mac_pattern = r"^([0-9A-F]{2}[:-]){5}([0-9A-F]{2})$"
    if not re.match(mac_pattern, enrollment_id):
        return jsonify({"msg":"Enrollment ID (MAC Address) is not valid"}), 400
    try:
        token_data = generate_slpt(user_id, enrollment_id)
        return jsonify ({
            "msg": "Token successfully generated",
            "slpt": token_data['slpt'],
            "expires_in_seconds": token_data['expires_in_seconds'],
            "expires_at_unix": token_data['expires_timestamp_unix']
        }), 201
    except Exception as e:
        print(f"Error generating SLPT: {e}")
        return jsonify({"msg": "Internal server error during token generation"}), 500

def activate_device():
    data = request.get_json()
    slpt_raw = data.get("slpt")
    mac_raw = data.get("mac")
    
    if not slpt_raw or not mac_raw:
        return jsonify({"msg":"Missing token or MAC"}), 400
        
    slpt = slpt_raw.strip()
    mac = mac_raw.upper().strip()
    
    print(f"DEBUG: Attempting activation for MAC: [{mac}] with SLPT: [{slpt}]")
    token_record = get_provisioning_token(slpt) # lookup token
    if not token_record:
        return jsonify({"msg": "Invalid provisioning token"}), 404
    #  security checks
    if token_record["is_used"]:
        return jsonify({"msg": "Token already used"}), 403
    if token_record["enrollment_id"] != mac:
        return jsonify({"msg": "MAC address mismatch"}), 403
    if token_record['expires_at'].replace(tzinfo=timezone.utc) < datetime.now(timezone.utc):
        return jsonify({"msg": "Token expired"}), 403
    try:
        # Success. Generate a random mqtt password
        mqtt_password = secrets.token_hex(16)
        activate_device_in_db(mac, token_record["user_id"], mqtt_password, data.get("os_version", "unknown"))
        return jsonify({
            "msg": "Device activated",
            "device_id": mac,        # fornow setting mac, later can have separate device ids
            "mqtt_user": mac,        # same for mqqt user
            "mqtt_pass": mqtt_password,
            "broker_url": BROKER_URL # laptop IP for the MQTT broker
        }), 200
    except Exception as e:
        print(f"Error activating device: {e}")
        return jsonify({"msg": "Internal server error during device activation"}), 500

def get_user_devices():
    user_identity = get_jwt_identity()
    user_id = int(user_identity)
    try:
        devices = get_devices_by_user_id(user_id)
        return jsonify({"devices": devices}), 200
    except Exception as e:
        print(f"Error fetching devices for user {user_id}: {e}")
        return jsonify({"msg": "Internal server error"}), 500