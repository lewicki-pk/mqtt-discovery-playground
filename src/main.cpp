#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mqtt/client.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

volatile std::sig_atomic_t g_should_stop = 0;

void handle_signal(int) {
    g_should_stop = 1;
}

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::optional<std::string> read_text_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return trim(buffer.str());
}

std::optional<long> parse_long(const std::string& value) {
    try {
        std::size_t consumed = 0;
        long parsed = std::stol(value, &consumed);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::string json_escape(std::string_view input) {
    std::string output;
    output.reserve(input.size() + 8);

    for (char ch : input) {
        switch (ch) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output += ch;
                break;
        }
    }

    return output;
}

std::string sanitize_topic_token(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    for (char ch : input) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else {
            out.push_back('_');
        }
    }

    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }

    if (out.empty()) {
        return "sensor";
    }
    return out;
}

struct Config {
    std::string host = "127.0.0.1";
    int port = 1883;
    std::string username;
    std::string password;
    std::string client_id = "cpu-temp-mqtt";
    std::string device_id = "linux_cpu_temp";
    std::string device_name = "Linux CPU Temperature";
    std::string sensor_name = "CPU Temperature";
    std::string discovery_prefix = "homeassistant";
    std::string state_topic;
    std::string thermal_zone;
    int interval_seconds = 30;
};

std::string sensor_object_id(const Config& config) {
    return sanitize_topic_token(config.device_id) + "_" + sanitize_topic_token(config.sensor_name);
}

std::optional<std::string> getenv_string(const char* key) {
    const char* value = std::getenv(key);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

void apply_env_overrides(Config& config) {
    if (auto v = getenv_string("MQTT_HOST")) config.host = *v;
    if (auto v = getenv_string("MQTT_PORT")) config.port = std::stoi(*v);
    if (auto v = getenv_string("MQTT_USERNAME")) config.username = *v;
    if (auto v = getenv_string("MQTT_PASSWORD")) config.password = *v;
    if (auto v = getenv_string("MQTT_CLIENT_ID")) config.client_id = *v;
    if (auto v = getenv_string("MQTT_DEVICE_ID")) config.device_id = *v;
    if (auto v = getenv_string("MQTT_DEVICE_NAME")) config.device_name = *v;
    if (auto v = getenv_string("MQTT_SENSOR_NAME")) config.sensor_name = *v;
    if (auto v = getenv_string("MQTT_DISCOVERY_PREFIX")) config.discovery_prefix = *v;
    if (auto v = getenv_string("MQTT_STATE_TOPIC")) config.state_topic = *v;
    if (auto v = getenv_string("MQTT_THERMAL_ZONE")) config.thermal_zone = *v;
    if (auto v = getenv_string("MQTT_INTERVAL_SECONDS")) config.interval_seconds = std::stoi(*v);
}

Config parse_args(int argc, char** argv) {
    Config config;
    apply_env_overrides(config);

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        auto require_value = [&](std::string_view flag) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for argument: " + std::string(flag));
            }
            return argv[++i];
        };

        if (arg == "--host") {
            config.host = require_value(arg);
        } else if (arg == "--port") {
            config.port = std::stoi(require_value(arg));
        } else if (arg == "--username") {
            config.username = require_value(arg);
        } else if (arg == "--password") {
            config.password = require_value(arg);
        } else if (arg == "--client-id") {
            config.client_id = require_value(arg);
        } else if (arg == "--device-id") {
            config.device_id = require_value(arg);
        } else if (arg == "--device-name") {
            config.device_name = require_value(arg);
        } else if (arg == "--sensor-name") {
            config.sensor_name = require_value(arg);
        } else if (arg == "--discovery-prefix") {
            config.discovery_prefix = require_value(arg);
        } else if (arg == "--state-topic") {
            config.state_topic = require_value(arg);
        } else if (arg == "--interval-seconds") {
            config.interval_seconds = std::stoi(require_value(arg));
        } else if (arg == "--thermal-zone") {
            config.thermal_zone = require_value(arg);
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: cpu_temp_mqtt [options]\n"
                << "  --host <host>\n"
                << "  --port <port>\n"
                << "  --username <user>\n"
                << "  --password <pass>\n"
                << "  --client-id <id>\n"
                << "  --device-id <id>\n"
                << "  --device-name <name>\n"
                << "  --sensor-name <name>\n"
                << "  --discovery-prefix <prefix>\n"
                << "  --state-topic <topic>\n"
                << "  --interval-seconds <seconds>\n"
                << "  --thermal-zone <index-or-type>\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (config.port <= 0 || config.port > 65535) {
        throw std::runtime_error("MQTT port must be between 1 and 65535");
    }
    if (config.interval_seconds <= 0) {
        throw std::runtime_error("Interval must be greater than 0 seconds");
    }

    return config;
}

struct ThermalReading {
    std::string zone_name;
    std::string zone_type;
    double celsius = 0.0;
};

bool looks_like_cpu_sensor(std::string_view type) {
    static const std::vector<std::string> preferred = {
        "x86_pkg_temp",
        "cpu",
        "cpu_thermal",
        "k10temp",
        "pkg_temp",
        "soc_thermal",
    };

    for (const auto& token : preferred) {
        if (type.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::optional<ThermalReading> read_zone(const fs::path& zone_path) {
    auto temp_text = read_text_file(zone_path / "temp");
    if (!temp_text) {
        return std::nullopt;
    }

    auto milli_c = parse_long(*temp_text);
    if (!milli_c) {
        return std::nullopt;
    }

    if (*milli_c < 1000 || *milli_c > 150000) {
        return std::nullopt;
    }

    ThermalReading reading;
    reading.zone_name = zone_path.filename().string();
    reading.zone_type = read_text_file(zone_path / "type").value_or("unknown");
    reading.celsius = static_cast<double>(*milli_c) / 1000.0;
    return reading;
}

std::optional<ThermalReading> find_cpu_temperature(const std::string& requested_zone) {
    const fs::path thermal_root("/sys/class/thermal");

    std::vector<ThermalReading> all_readings;
    for (const auto& entry : fs::directory_iterator(thermal_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (entry.path().filename().string().rfind("thermal_zone", 0) != 0) {
            continue;
        }
        if (auto reading = read_zone(entry.path())) {
            all_readings.push_back(*reading);
        }
    }

    if (all_readings.empty()) {
        return std::nullopt;
    }

    if (!requested_zone.empty()) {
        for (const auto& reading : all_readings) {
            std::string zone_index = reading.zone_name.substr(std::string("thermal_zone").size());
            if (reading.zone_name == requested_zone || zone_index == requested_zone || reading.zone_type == requested_zone) {
                return reading;
            }
        }
        throw std::runtime_error("Requested thermal zone not found: " + requested_zone);
    }

    for (const auto& reading : all_readings) {
        if (looks_like_cpu_sensor(reading.zone_type)) {
            return reading;
        }
    }

    auto hottest = all_readings.front();
    for (const auto& reading : all_readings) {
        if (reading.celsius > hottest.celsius) {
            hottest = reading;
        }
    }
    return hottest;
}

std::string mqtt_server_uri(const Config& config) {
    return "tcp://" + config.host + ":" + std::to_string(config.port);
}

class MqttPublisher {
public:
    explicit MqttPublisher(const Config& config)
        : client_(mqtt_server_uri(config), config.client_id),
          connect_options_(build_connect_options(config)) {}

    void connect() {
        if (!client_.is_connected()) {
            client_.connect(connect_options_);
        }
    }

    void publish(const std::string& topic, const std::string& payload, bool retain) {
        client_.publish(topic, payload.data(), payload.size(), kQos, retain);
    }

    void disconnect() {
        try {
            if (client_.is_connected()) {
                client_.disconnect();
            }
        } catch (...) {
        }
    }

private:
    static mqtt::connect_options build_connect_options(const Config& config) {
        mqtt::connect_options options;
        options.set_clean_session(true);
        options.set_keep_alive_interval(std::chrono::seconds(std::max(60, config.interval_seconds * 3)));
        options.set_connect_timeout(std::chrono::seconds(10));

        if (!config.username.empty()) {
            options.set_user_name(config.username);
        }
        if (!config.password.empty()) {
            options.set_password(config.password);
        }

        return options;
    }

    static constexpr int kQos = 1;

    mqtt::client client_;
    mqtt::connect_options connect_options_;
};

std::string default_state_topic(const Config& config) {
    return config.discovery_prefix + "/sensor/" + sensor_object_id(config) + "/state";
}

std::string discovery_topic(const Config& config) {
    return config.discovery_prefix + "/sensor/" + sensor_object_id(config) + "/config";
}

std::string discovery_payload(const Config& config, const std::string& state_topic) {
    const std::string unique_id = sensor_object_id(config);

    std::ostringstream json;
    json
        << "{"
        << "\"name\":\"" << json_escape(config.sensor_name) << "\","
        << "\"unique_id\":\"" << json_escape(unique_id) << "\","
        << "\"state_topic\":\"" << json_escape(state_topic) << "\","
        << "\"device_class\":\"temperature\","
        << "\"unit_of_measurement\":\"°C\","
        << "\"state_class\":\"measurement\","
        << "\"object_id\":\"" << json_escape(unique_id) << "\","
        << "\"suggested_display_precision\":1,"
        << "\"device\":{"
        << "\"identifiers\":[\"" << json_escape(config.device_id) << "\"],"
        << "\"name\":\"" << json_escape(config.device_name) << "\","
        << "\"manufacturer\":\"Custom\","
        << "\"model\":\"Linux sysfs CPU temperature publisher\""
        << "}"
        << "}";
    return json.str();
}

std::string format_temperature(double celsius) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(1) << celsius;
    return value.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        Config config = parse_args(argc, argv);
        if (config.state_topic.empty()) {
            config.state_topic = default_state_topic(config);
        }

        const std::string config_topic = discovery_topic(config);
        const std::string config_payload = discovery_payload(config, config.state_topic);
        MqttPublisher mqtt(config);

        bool published_discovery = false;

        while (!g_should_stop) {
            try {
                auto reading = find_cpu_temperature(config.thermal_zone);
                if (!reading) {
                    throw std::runtime_error("No usable thermal zone found under /sys/class/thermal");
                }

                mqtt.connect();

                if (!published_discovery) {
                    mqtt.publish(config_topic, config_payload, true);
                    published_discovery = true;
                    std::cerr << "Published discovery to " << config_topic << "\n";
                }

                const std::string state_payload = format_temperature(reading->celsius);
                mqtt.publish(config.state_topic, state_payload, true);
                std::cerr
                    << "Published " << state_payload << " C from "
                    << reading->zone_name << " (" << reading->zone_type << ")"
                    << " to " << config.state_topic << "\n";
            } catch (const std::exception& ex) {
                mqtt.disconnect();
                published_discovery = false;
                std::cerr << "Publish attempt failed: " << ex.what() << "\n";
            }

            for (int slept = 0; slept < config.interval_seconds && !g_should_stop; ++slept) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        mqtt.disconnect();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
