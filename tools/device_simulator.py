"""Sewing-machine telemetry simulator — stand-in for ESP32 firmware until the real thing ships.

Connects to the broker with a device's provisioned MQTT credentials and publishes
JSON telemetry on `stitch/device/<MAC>/telemetry` at a configurable cadence,
modelling rough but recognizably-real sewing behaviour: pedal-pressed cycles
during which the stitch counter ticks up, brief idle gaps between cycles where
the operator handles the next piece, an occasional `trim` event at cycle end.

Run multiple instances in parallel to simulate a production line.

Usage:
    pip install paho-mqtt
    python tools/device_simulator.py \\
        --mac AA:BB:CC:DD:EE:03 \\
        --password <mqtt_pass from /api/devices/activate> \\
        --broker 213.188.218.253 \\
        --interval 0.5

Press Ctrl+C to stop.
"""

import argparse
import json
import random
import signal
import sys
import time

import paho.mqtt.client as mqtt


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Simulate a sewing-machine MQTT device.")
    p.add_argument("--mac", required=True, help="MAC of the provisioned device (becomes the MQTT username).")
    p.add_argument("--password", required=True, help="mqtt_pass returned by /api/devices/activate.")
    p.add_argument("--broker", default="213.188.218.253", help="Broker host or IP.")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--interval", type=float, default=0.5, help="Seconds between published messages.")
    p.add_argument("--initial-count", type=int, default=0, help="Starting stitch count.")
    p.add_argument(
        "--rate-per-sec", type=float, default=20.0,
        help="Stitches per second while pedal is engaged (real machines: 30-80).")
    p.add_argument("--cycle-len-sec", type=float, default=8.0,
                   help="Average duration of one sewing cycle (pedal-pressed phase).")
    p.add_argument("--handling-len-sec", type=float, default=4.0,
                   help="Average duration of handling gap between cycles.")
    return p.parse_args()


class SewingSession:
    """Tiny state machine: alternating SEWING and HANDLING phases, trim at end of SEWING."""

    SEWING = "sewing"
    HANDLING = "handling"

    def __init__(self, initial_count: int, rate_per_sec: float, cycle_len: float, handling_len: float):
        self.stitch_count = initial_count
        self.rate_per_sec = rate_per_sec
        self.mean_cycle = cycle_len
        self.mean_handling = handling_len
        self.phase = self.HANDLING  # start idle
        self.phase_started_at = time.monotonic()
        self.phase_duration = random.uniform(0.5, self.mean_handling)
        self.trim_pending = False  # fires once at the moment of cycle end

    def _maybe_switch_phase(self) -> None:
        elapsed = time.monotonic() - self.phase_started_at
        if elapsed < self.phase_duration:
            return
        if self.phase == self.SEWING:
            # End of cycle — trim event, then idle/handling phase.
            self.phase = self.HANDLING
            self.phase_duration = random.uniform(
                self.mean_handling * 0.5, self.mean_handling * 1.5)
            self.trim_pending = True  # signaled on the next sample
        else:
            self.phase = self.SEWING
            self.phase_duration = random.uniform(
                self.mean_cycle * 0.6, self.mean_cycle * 1.4)
        self.phase_started_at = time.monotonic()

    def sample(self, dt: float) -> dict:
        """Advance state by `dt` seconds and return one telemetry payload."""
        self._maybe_switch_phase()

        if self.phase == self.SEWING:
            # ~Poisson-ish increment with a bit of jitter so the line isn't perfectly straight
            stitches_this_tick = max(0, int(random.gauss(self.rate_per_sec * dt, 1.5)))
            self.stitch_count += stitches_this_tick
            foot = 1
        else:
            foot = 0

        trim = self.trim_pending
        self.trim_pending = False  # one-shot

        return {
            "foot": foot,
            "stitch_count": self.stitch_count,
            "trim": trim,
        }


def main() -> None:
    args = parse_args()
    topic = f"stitch/device/{args.mac}/telemetry"

    session = SewingSession(
        initial_count=args.initial_count,
        rate_per_sec=args.rate_per_sec,
        cycle_len=args.cycle_len_sec,
        handling_len=args.handling_len_sec,
    )

    client = mqtt.Client(client_id=f"simulator-{args.mac}")
    client.username_pw_set(args.mac, args.password)

    connected = {"ok": False}

    def on_connect(c, _u, _f, rc):
        if rc == 0:
            connected["ok"] = True
            print(f"[sim] connected to {args.broker}:{args.port} as {args.mac}", flush=True)
        else:
            print(f"[sim] connect failed rc={rc} (4=bad creds, 5=not authorised)", flush=True)
            sys.exit(2)

    def on_disconnect(_c, _u, rc):
        print(f"[sim] disconnected rc={rc}", flush=True)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

    # Graceful Ctrl+C
    def handle_sigint(_signum, _frame):
        print("\n[sim] stopping", flush=True)
        client.loop_stop()
        client.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_sigint)

    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()

    # Wait briefly for the connection ack.
    deadline = time.monotonic() + 5.0
    while not connected["ok"] and time.monotonic() < deadline:
        time.sleep(0.05)
    if not connected["ok"]:
        print("[sim] no CONNACK within 5s; bailing.", flush=True)
        sys.exit(2)

    print(f"[sim] publishing to {topic} every {args.interval}s — Ctrl+C to stop\n", flush=True)
    while True:
        payload = session.sample(args.interval)
        client.publish(topic, json.dumps(payload), qos=0)
        marker = "↗" if payload["foot"] else "·"
        trim_marker = "  ✂" if payload["trim"] else ""
        print(f"{marker} stitch_count={payload['stitch_count']:>6}  foot={payload['foot']}{trim_marker}",
              flush=True)
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
