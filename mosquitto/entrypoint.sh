#!/bin/sh
# Bootstrap the dynamic-security.json file with an admin user on first boot.
# After bootstrap, the JSON file on the persistent volume is the source of truth;
# changing DYNSEC_ADMIN_PASSWORD env var later does NOT rotate the admin password.
# To rotate, use `mosquitto_ctrl dynsec setClientPassword admin <new>`.

set -e

DYNSEC_FILE=/mosquitto/data/dynamic-security.json

# Fly volumes mount root-owned by default; mosquitto runs as uid 1883.
# Hand the data dir to it so persistence and dynsec autosave can write.
chown -R mosquitto:mosquitto /mosquitto/data
chmod 0755 /mosquitto/data

if [ ! -f "$DYNSEC_FILE" ]; then
    echo "[entrypoint] $DYNSEC_FILE missing; bootstrapping admin user..."

    : "${DYNSEC_ADMIN_USER:?DYNSEC_ADMIN_USER must be set on first boot}"
    : "${DYNSEC_ADMIN_PASSWORD:?DYNSEC_ADMIN_PASSWORD must be set on first boot}"

    mosquitto_ctrl dynsec init "$DYNSEC_FILE" "$DYNSEC_ADMIN_USER" "$DYNSEC_ADMIN_PASSWORD"

    chown mosquitto:mosquitto "$DYNSEC_FILE"
    chmod 0600 "$DYNSEC_FILE"

    echo "[entrypoint] Bootstrap complete. Admin user: $DYNSEC_ADMIN_USER"
else
    echo "[entrypoint] $DYNSEC_FILE already exists; skipping bootstrap."
fi

exec "$@"
