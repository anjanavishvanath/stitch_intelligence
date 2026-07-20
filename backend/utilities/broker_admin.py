"""Backend → Mosquitto Dynamic Security plugin control client.

Used by `device_helpers.activate_device` to register a device's MQTT credentials
at the broker the moment they're issued. Idempotent: re-running with the same
MAC updates the password and re-asserts the role.

Connects as the dynsec admin user (creds from Fly secrets) over Fly's private
6PN network (MQTT_BROKER=stitch-mqtt.internal). Connection is short-lived per
call — opens, sends the batch, waits for the response, disconnects.
"""

import json
import os
import threading
import uuid
import paho.mqtt.client as mqtt


MQTT_BROKER = os.getenv("MQTT_BROKER", "stitch-mqtt.internal")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
DYNSEC_ADMIN_USER = os.getenv("DYNSEC_ADMIN_USER")
DYNSEC_ADMIN_PASSWORD = os.getenv("DYNSEC_ADMIN_PASSWORD")
DEFAULT_DEVICE_ROLE = os.getenv("DYNSEC_DEVICE_ROLE", "device")

CONTROL_TOPIC = "$CONTROL/dynamic-security/v1"
RESPONSE_TOPIC = "$CONTROL/dynamic-security/v1/response"

CONNECT_TIMEOUT_SEC = 10
RESPONSE_TIMEOUT_SEC = 10


class BrokerAdminError(Exception):
    """Raised when a control-message round-trip cannot be completed."""


def _send_control(commands: list) -> dict:
    """Open an admin connection, publish a batch of dynsec commands, await the response, disconnect.

    Returns the parsed response payload (`{"responses": [...]}`).
    Raises BrokerAdminError on connect/timeout/parse failure.
    """
    if not DYNSEC_ADMIN_USER or not DYNSEC_ADMIN_PASSWORD:
        raise BrokerAdminError(
            "DYNSEC_ADMIN_USER / DYNSEC_ADMIN_PASSWORD not set; cannot reach dynsec plugin."
        )

    state = {}
    connected = threading.Event()
    subscribed = threading.Event()
    response_received = threading.Event()

    # Random correlation tag so a stale response from an earlier failed call can't be misread.
    correlation = uuid.uuid4().hex

    def on_connect(client, userdata, flags, rc):
        if rc != 0:
            state["error"] = f"Admin connect refused (rc={rc}); check DYNSEC_ADMIN_USER/PASSWORD."
            connected.set()
            response_received.set()
            return
        connected.set()
        client.subscribe(RESPONSE_TOPIC, qos=1)

    def on_subscribe(client, userdata, mid, granted_qos):
        if 128 in granted_qos:
            state["error"] = "Admin not authorised to subscribe to dynsec response topic."
            response_received.set()
            return
        subscribed.set()

    def on_message(client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            state["error"] = f"Could not parse dynsec response: {e}"
        else:
            state["response"] = payload
        response_received.set()

    client = mqtt.Client(client_id=f"stitch-backend-admin-{correlation}")
    client.username_pw_set(DYNSEC_ADMIN_USER, DYNSEC_ADMIN_PASSWORD)
    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message

    try:
        client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=30)
        client.loop_start()

        if not connected.wait(CONNECT_TIMEOUT_SEC):
            raise BrokerAdminError(f"Timed out connecting to {MQTT_BROKER}:{MQTT_PORT}")
        if "error" in state:
            raise BrokerAdminError(state["error"])
        if not subscribed.wait(CONNECT_TIMEOUT_SEC):
            raise BrokerAdminError("Timed out waiting for response-topic subscription")

        client.publish(CONTROL_TOPIC, json.dumps({"commands": commands}), qos=1)

        if not response_received.wait(RESPONSE_TIMEOUT_SEC):
            raise BrokerAdminError(f"Timed out waiting for dynsec response after {RESPONSE_TIMEOUT_SEC}s")
        if "error" in state:
            raise BrokerAdminError(state["error"])

        return state["response"]
    finally:
        try:
            client.loop_stop()
            client.disconnect()
        except Exception:  # noqa: BLE001 — cleanup best-effort
            pass


def _check_responses(responses: list, ignore_errors: dict | None = None) -> None:
    """Raise BrokerAdminError if any response carries an unexpected error.

    `ignore_errors` maps command-name → list of substrings that, if present in the error,
    are treated as acceptable (for idempotency).
    """
    ignore_errors = ignore_errors or {}
    for r in responses:
        cmd = r.get("command", "<unknown>")
        err = r.get("error")
        if not err:
            continue
        ignored = ignore_errors.get(cmd, [])
        if any(s.lower() in err.lower() for s in ignored):
            continue
        raise BrokerAdminError(f"dynsec {cmd} failed: {err}")


def register_device_client(mac: str, password: str, role: str | None = None) -> None:
    """Idempotently register a device with the broker.

    Uses the MAC as the MQTT username (matches what activate_device returns to the device).
    If the client already exists, only updates its password — the role assignment from
    the original createClient is preserved.

    Implementation note: we do createClient with inline `rolename` rather than a separate
    addClientRole call, because Mosquitto's dynsec plugin sometimes returns "Internal error"
    from addClientRole even when the role assignment actually succeeds, which makes that
    response untrustworthy as a success signal.
    """
    role = role or DEFAULT_DEVICE_ROLE

    # Single atomic command: create client AND assign role.
    # createClient takes a `roles` array of {rolename, priority} objects, not a flat `rolename` string.
    create_response = _send_control([
        {"command": "createClient", "username": mac, "password": password,
         "textname": f"Sewing device {mac}",
         "roles": [{"rolename": role, "priority": -1}]},
    ])

    create_responses = create_response.get("responses", [])
    create_resp = next((r for r in create_responses if r.get("command") == "createClient"), {})

    # Happy path — new device.
    if not create_resp.get("error"):
        return

    # Idempotent path — device already exists; just rotate its password.
    # Role was already assigned by the original createClient call, no need to touch it.
    if "exists" in create_resp["error"].lower():
        update_response = _send_control([
            {"command": "setClientPassword", "username": mac, "password": password},
        ])
        _check_responses(update_response.get("responses", []))
        return

    raise BrokerAdminError(f"dynsec createClient failed: {create_resp['error']}")


def revoke_device_client(mac: str) -> None:
    """Remove a device from the broker. Idempotent: silent if client is already gone."""
    response = _send_control([{"command": "deleteClient", "username": mac}])
    _check_responses(
        response.get("responses", []),
        ignore_errors={"deleteClient": ["not found", "does not exist"]},
    )
