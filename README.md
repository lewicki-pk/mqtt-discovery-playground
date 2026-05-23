# CPU Temperature MQTT Discovery Publisher

Small Linux C++ application that reads the system CPU temperature from `sysfs` and publishes it to MQTT using Home Assistant MQTT discovery.

## What it does

- Reads temperature from `/sys/class/thermal/thermal_zone*/temp`
- Auto-detects a CPU-related thermal zone when possible
- Publishes Home Assistant discovery config as a retained MQTT message
- Publishes temperature state periodically

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/cpu_temp_mqtt --host 192.168.1.10 --username homeassistant --password secret
```

Useful options:

```text
--host <hostname>              MQTT broker host, default: 127.0.0.1
--port <port>                  MQTT broker port, default: 1883
--username <username>          MQTT username
--password <password>          MQTT password
--client-id <id>               MQTT client id
--device-id <id>               Home Assistant device id, default: linux_cpu_temp
--device-name <name>           Home Assistant device name
--sensor-name <name>           Sensor name shown in Home Assistant
--discovery-prefix <prefix>    Discovery prefix, default: homeassistant
--state-topic <topic>          Override state topic
--interval-seconds <seconds>   Publish interval, default: 30
--thermal-zone <zone>          Thermal zone name or index, e.g. 0 or x86_pkg_temp
```

Environment variables are also supported:

```text
MQTT_HOST
MQTT_PORT
MQTT_USERNAME
MQTT_PASSWORD
MQTT_CLIENT_ID
MQTT_DEVICE_ID
MQTT_DEVICE_NAME
MQTT_SENSOR_NAME
MQTT_DISCOVERY_PREFIX
MQTT_STATE_TOPIC
MQTT_INTERVAL_SECONDS
MQTT_THERMAL_ZONE
```

## Home Assistant

After startup, Home Assistant should auto-discover the entity from the retained discovery payload. The default entity topic layout is:

- Discovery topic: `homeassistant/sensor/linux_cpu_temp_cpu_temperature/config`
- State topic: `homeassistant/sensor/linux_cpu_temp_cpu_temperature/state`

## Notes

- Access to thermal zone files depends on your Linux system exposing them in `sysfs`.
- The app uses a built-in minimal MQTT 3.1.1 publisher over TCP, so no external MQTT library is required.
