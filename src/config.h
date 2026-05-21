#ifndef CONFIG_H
#define CONFIG_H

#include "mbed.h"

using namespace std::chrono_literals;

namespace {

constexpr PinName PIN_MQ3_AO = A0;
constexpr PinName PIN_I2C_SDA = D4;
constexpr PinName PIN_I2C_SCL = D5;
constexpr PinName PIN_LED_GREEN = D9;
constexpr PinName PIN_LED_RED = D2;
constexpr PinName PIN_BUZZER = D3;
constexpr PinName PIN_BLE_TX = D1;
constexpr PinName PIN_BLE_RX = D0;

constexpr bool LED_ACTIVE_LOW = false;
constexpr bool BUZZER_ACTIVE_LOW = false;
constexpr bool RUN_OUTPUT_SELF_TEST = true;
constexpr bool ENABLE_BLE_TELEMETRY = true;
constexpr bool DATA_LOGGING = false;

constexpr int ADC_MAX_VALUE = 4095;
constexpr auto SAMPLE_PERIOD = 100ms;
constexpr auto DISPLAY_PERIOD = 250ms;
constexpr auto BLE_TELEMETRY_PERIOD = 2s;
constexpr auto PREHEAT_TIME = 30s;
constexpr auto COOLDOWN_TIME = 10s;
constexpr auto AUTO_SLEEP_AFTER = 5min;

constexpr int FILTER_WINDOW = 25;
constexpr int WARNING_ENTER_COUNT = 2;
constexpr int DANGER_ENTER_COUNT = 2;
constexpr int WARNING_RELEASE_COUNT = 2;
constexpr int DANGER_RELEASE_COUNT = 3;
constexpr int SLEEP_WAKE_OFFSET = 120;
constexpr int SLEEP_WAKE_COUNT = 3;

constexpr int WARNING_THRESHOLD_MIN = 850;
constexpr int WARNING_OFFSET = 250;
constexpr int DANGER_OFFSET = 1500;
constexpr int WARNING_HYSTERESIS = 100;
constexpr int DANGER_HYSTERESIS = 200;

constexpr int SENSOR_FAULT_LOW_ADC = 10;
constexpr int SENSOR_FAULT_HIGH_ADC = ADC_MAX_VALUE - 10;
constexpr int SENSOR_FAULT_RAIL_COUNT = 20;
constexpr int SENSOR_FAULT_RECOVER_COUNT = 20;

constexpr int EVENT_LOG_SIZE = 32;
constexpr auto EVENT_LOG_SNAPSHOT_PERIOD = 30s;

} // namespace

#endif
