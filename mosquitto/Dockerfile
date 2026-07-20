# Mosquitto broker for Fly.io deployment.
# Auth via Dynamic Security plugin; bootstrapped on first boot from env vars.
# Build context: ./mosquitto/  (run `fly deploy` from this directory)

FROM eclipse-mosquitto:2.0

# Install Fly-specific config.
COPY mosquitto.fly.conf /mosquitto/config/mosquitto.conf

# Bootstrap script: creates dynamic-security.json on the persistent volume on first boot.
COPY entrypoint.sh /docker-entrypoint.sh
RUN chmod +x /docker-entrypoint.sh \
    && chown mosquitto:mosquitto /mosquitto/config/mosquitto.conf

EXPOSE 1883

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["/usr/sbin/mosquitto", "-c", "/mosquitto/config/mosquitto.conf"]
