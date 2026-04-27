import os
import time
import paho.mqtt.client as mqtt

MQTT_BROKER = os.getenv("MQTT_BROKER", "mqtt_broker")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "#")

def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT broker at {MQTT_BROKER} with result code {rc}", flush=True)
    client.subscribe(MQTT_TOPIC)
    print(f"Subscribed to topic: {MQTT_TOPIC}", flush=True)

def on_message(client, userdata, msg):
    print(f"[{msg.topic}] {msg.payload.decode()}", flush=True)

if __name__ == "__main__":
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    connected = False
    while not connected:
        try:
            print(f"Attempting to connect to MQTT broker {MQTT_BROKER}:{MQTT_PORT}...", flush=True)
            client.connect(MQTT_BROKER, MQTT_PORT, 60)
            connected = True
        except Exception as e:
            print(f"Connection failed: {e}. Retrying in 5 seconds...", flush=True)
            time.sleep(5)

    client.loop_forever()
