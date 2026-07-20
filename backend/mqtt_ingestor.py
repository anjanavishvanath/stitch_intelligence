"""MQTT subscriber that parses telemetry messages and persists them to Neon.

Topic convention: stitch/device/<MAC>/telemetry
Payload (wireframe-stage):    {"foot": 1, "stitch_count": 105, "trim": true}

Tolerant to malformed payloads and unknown topics — logs a warning and moves on.
A bad message must never crash the ingestor; live deployments depend on it.
"""

import json
import os
import re
import time

import paho.mqtt.client as mqtt
from sqlalchemy import create_engine, text

# --- MQTT config ---
MQTT_BROKER = os.getenv("MQTT_BROKER", "mqtt_broker")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "stitch/#")
MQTT_USER = os.getenv("MQTT_USER")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD")

# --- DB config ---
DATABASE_URL = os.getenv("DATABASE_URL")
engine = create_engine(DATABASE_URL, echo=False, future=True, pool_pre_ping=True) if DATABASE_URL else None

# stitch/device/<MAC>/pieces — capture the MAC
PIECES_TOPIC_RE = re.compile(r"^stitch/device/([^/]+)/pieces$")

INSERT_PIECE_SQL = text("""
    INSERT INTO pieces (
        device_mac, piece_seq,
        piece_started_at_ms, piece_completed_at_ms,
        idle_before_ms, total_cycle_time_ms,
        total_stitching_ms, total_adjustment_ms,
        total_stitches, avg_stitch_hz,
        adjustment_count, trim_and_wipe_time_ms,
        status, segments
    ) VALUES (
        :device_mac, :piece_seq,
        :piece_started_at_ms, :piece_completed_at_ms,
        :idle_before_ms, :total_cycle_time_ms,
        :total_stitching_ms, :total_adjustment_ms,
        :total_stitches, :avg_stitch_hz,
        :adjustment_count, :trim_and_wipe_time_ms,
        :status, CAST(:segments AS JSONB)
    )
""")


def persist_piece(device_mac: str, payload: dict) -> None:
    """Insert one completed sewing piece. Segments go in as JSONB."""
    if engine is None:
        print("WARNING: DATABASE_URL not set; skipping persistence.", flush=True)
        return
    try:
        with engine.begin() as conn:
            conn.execute(INSERT_PIECE_SQL, {
                "device_mac":            device_mac,
                "piece_seq":             payload.get("piece_seq"),
                "piece_started_at_ms":   payload.get("piece_started_at_ms"),
                "piece_completed_at_ms": payload.get("piece_completed_at_ms"),
                "idle_before_ms":        payload.get("idle_before_ms"),
                "total_cycle_time_ms":   payload.get("total_cycle_time_ms"),
                "total_stitching_ms":    payload.get("total_stitching_ms"),
                "total_adjustment_ms":   payload.get("total_adjustment_ms"),
                "total_stitches":        payload.get("total_stitches"),
                "avg_stitch_hz":         payload.get("avg_stitch_hz"),
                "adjustment_count":      payload.get("adjustment_count"),
                "trim_and_wipe_time_ms": payload.get("trim_and_wipe_time_ms"),
                "status":                payload.get("status"),
                "segments":              json.dumps(payload.get("segments", [])),
            })
    except Exception as e:  # noqa: BLE001
        print(f"DB insert (piece) failed for {device_mac}: {e}", flush=True)


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTT broker at {MQTT_BROKER}:{MQTT_PORT}", flush=True)
        client.subscribe(MQTT_TOPIC)
        print(f"Subscribed to topic: {MQTT_TOPIC}", flush=True)
    else:
        print(f"Connect failed: result code {rc}", flush=True)


def on_disconnect(client, userdata, rc):
    print(f"Disconnected (rc={rc}). Paho will auto-reconnect.", flush=True)


def on_message(client, userdata, msg):
    raw = msg.payload.decode(errors="replace")
    print(f"[{msg.topic}] {raw}", flush=True)

    # Route on topic. Ignore anything that doesn't match the pieces pattern
    # (e.g. $CONTROL/dynamic-security, heartbeats, etc.)
    piece_match = PIECES_TOPIC_RE.match(msg.topic)
    if not piece_match:
        return

    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"Skipping malformed JSON on {msg.topic}: {e}", flush=True)
        return

    if not isinstance(payload, dict):
        print(f"Skipping non-object payload on {msg.topic}", flush=True)
        return

    persist_piece(piece_match.group(1), payload)


if __name__ == "__main__":
    client = mqtt.Client()
    if MQTT_USER and MQTT_PASSWORD:
        client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    else:
        print("WARNING: MQTT_USER / MQTT_PASSWORD not set; attempting anonymous connect.", flush=True)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    while True:
        try:
            print(f"Connecting to MQTT broker {MQTT_BROKER}:{MQTT_PORT}...", flush=True)
            client.connect(MQTT_BROKER, MQTT_PORT, 60)
            break
        except Exception as e:
            print(f"Connect failed: {e}. Retrying in 5s...", flush=True)
            time.sleep(5)

    client.loop_forever()
