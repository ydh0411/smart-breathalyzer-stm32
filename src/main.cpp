#include "mbed.h"
#include "oled_driver.h"
#include "config.h"
#include "filter.h"
#include "outputs.h"
#include "display.h"

#include <cctype>
#include <cstdio>
#include <cstring>

int main() {
    AnalogIn mq3_ao(PIN_MQ3_AO);
    DigitalOut led_green(PIN_LED_GREEN, 0);
    DigitalOut led_red(PIN_LED_RED, 0);
    DigitalOut buzzer(PIN_BUZZER, 0);
    BufferedSerial ble_uart(PIN_BLE_TX, PIN_BLE_RX, 9600);
    ble_uart.set_blocking(false);

    if (RUN_OUTPUT_SELF_TEST) {
        printf("Output self-test start...\r\n");
        auto test_step = [&](bool g, bool r, bool b, auto t) {
            write_channel(led_green, g, LED_ACTIVE_LOW);
            write_channel(led_red, r, LED_ACTIVE_LOW);
            write_channel(buzzer, b, BUZZER_ACTIVE_LOW);
            ThisThread::sleep_for(t);
        };
        test_step(true, false, false, 500ms);
        test_step(false, true, false, 500ms);
        test_step(true, true, true, 250ms);
        write_channel(led_green, false, LED_ACTIVE_LOW);
        write_channel(led_red, false, LED_ACTIVE_LOW);
        write_channel(buzzer, false, BUZZER_ACTIVE_LOW);
        printf("Output self-test end.\r\n");
    }

    Ssd1306 oled(PIN_I2C_SDA, PIN_I2C_SCL);
    const bool oled_ok = oled.init();

    printf("Smart Breathalyzer booting...\r\n");
    if (!oled_ok) {
        printf("OLED init failed. Please check wiring and I2C address.\r\n");
    }
    printf("BLE CMD: W/WAKE, S/SLEEP, C/CAL, T/STAT, H/HELP\r\n");

    MovingTrimmedAverage filter;
    Timer uptime;
    uptime.start();

    SystemState state = SystemState::Preheating;

    int raw_adc = 0, filtered_adc = 0, baseline_adc = 0;
    int warning_threshold_adc = WARNING_THRESHOLD_MIN;
    int danger_threshold_adc = WARNING_THRESHOLD_MIN + 120;
    int level_percent = 0, baseline_sum = 0, baseline_count = 0;
    int trigger_counter = 0, release_counter = 0, sleep_wake_counter = 0;
    int sensor_rail_counter = 0, sensor_recover_counter = 0;
    char ble_command[32] = {};
    size_t ble_command_len = 0;

    auto next_sample = Kernel::Clock::now();
    auto next_display = Kernel::Clock::now();
    auto next_ble = Kernel::Clock::now();
    auto preheat_start = Kernel::Clock::now();
    auto cooldown_start = Kernel::Clock::now();
    auto safe_idle_start = Kernel::Clock::now();
    auto ble_rx_last = Kernel::Clock::now();

    auto ble_send_text = [&](const char *text) {
        if (text == nullptr) {
            return;
        }

        size_t remaining = std::strlen(text);
        while (remaining > 0) {
            const ssize_t written = ble_uart.write(text, remaining);
            if (written <= 0) {
                break;
            }
            text += written;
            remaining -= static_cast<size_t>(written);
        }
    };

    auto reset_counters = [&]() {
        trigger_counter = release_counter = sleep_wake_counter = 0;
    };

    auto reset_to_preheating = [&](const auto &now) {
        state = SystemState::Preheating;
        preheat_start = safe_idle_start = now;
        baseline_sum = baseline_count = level_percent = 0;
        reset_counters();
        sensor_rail_counter = sensor_recover_counter = 0;
    };

    auto enter_sleep = [&](const auto &now) {
        state = SystemState::Sleep;
        reset_counters();
        safe_idle_start = now;
    };

    auto send_status = [&](const auto &now) {
        auto sec_left = [&](const auto &start, auto duration) {
            int s = static_cast<int>(duration.count()) -
                    static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
            return clamp_int(s, 0, static_cast<int>(duration.count()));
        };
        int pre = (state == SystemState::Preheating) ? sec_left(preheat_start, PREHEAT_TIME) : 0;
        int cool = (state == SystemState::Cooldown) ? sec_left(cooldown_start, COOLDOWN_TIME) : 0;
        char line[160];
        std::snprintf(line, sizeof(line),
            "STATUS=%s,RAW=%d,AVG=%d,BASE=%d,W=%d,D=%d,LVL=%d,SLEEP=%d,PRE=%d,COOL=%d\r\n",
            state_name(state), raw_adc, filtered_adc, baseline_adc, warning_threshold_adc,
            danger_threshold_adc, level_percent, state == SystemState::Sleep ? 1 : 0, pre, cool);
        ble_send_text(line);
    };

    auto process_ble_command = [&](const char *command, const auto &current_now) {
        if (command == nullptr || command[0] == '\0') {
            return;
        }

        if (std::strcmp(command, "H") == 0 || std::strcmp(command, "HELP") == 0) {
            ble_send_text("CMDS: W/WAKE,S/SLEEP,C/CAL,T/STAT,H/HELP\r\n");
            return;
        }

        if (std::strcmp(command, "T") == 0 || std::strcmp(command, "STAT") == 0) {
            send_status(current_now);
            return;
        }

        if (std::strcmp(command, "S") == 0 || std::strcmp(command, "SLEEP") == 0) {
            enter_sleep(current_now);
            ble_send_text("ACK SLEEP\r\n");
            printf("CMD => SLEEP\r\n");
            return;
        }

        if (std::strcmp(command, "W") == 0 || std::strcmp(command, "WAKE") == 0) {
            if (state == SystemState::Sleep) {
                state = SystemState::Safe;
                reset_counters();
                safe_idle_start = current_now;
                ble_send_text("ACK WAKE\r\n");
                printf("CMD => WAKE\r\n");
            } else {
                ble_send_text("ACK WAKE (NOOP)\r\n");
            }
            return;
        }

        if (std::strcmp(command, "C") == 0 || std::strcmp(command, "CAL") == 0 || std::strcmp(command, "RECAL") == 0) {
            reset_to_preheating(current_now);
            ble_send_text("ACK CAL\r\n");
            printf("CMD => CAL\r\n");
            return;
        }

        ble_send_text("ERR UNKNOWN\r\n");
    };

    while (true) {
        const auto now = Kernel::Clock::now();

        char rx = 0;
        while (ble_uart.read(&rx, 1) == 1) {
            const unsigned char byte = static_cast<unsigned char>(rx);
            if (std::isalpha(byte)) {
                if (ble_command_len < sizeof(ble_command) - 1) {
                    ble_command[ble_command_len++] = static_cast<char>(std::toupper(byte));
                    ble_command[ble_command_len] = '\0';
                    ble_rx_last = now;
                } else {
                    ble_command_len = 0;
                    ble_command[0] = '\0';
                }
            } else if (rx == '\r' || rx == '\n' || rx == ' ' || rx == '\t') {
                // Ignore separators; the command is processed after the input stream goes idle.
            } else {
                ble_command_len = 0;
                ble_command[0] = '\0';
            }
        }

        if (ble_command_len > 0 && (now - ble_rx_last) >= 60ms) {
            process_ble_command(ble_command, now);
            ble_command_len = 0;
            ble_command[0] = '\0';
        }

        if (now >= next_sample) {
            next_sample += SAMPLE_PERIOD;

            raw_adc = static_cast<int>(mq3_ao.read() * ADC_MAX_VALUE + 0.5f);
            raw_adc = clamp_int(raw_adc, 0, ADC_MAX_VALUE);

            filter.push(raw_adc);
            filtered_adc = filter.value();

            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(uptime.elapsed_time()).count();
            const bool sensor_rail_fault = (raw_adc <= SENSOR_FAULT_LOW_ADC || raw_adc >= SENSOR_FAULT_HIGH_ADC);

            if (state != SystemState::SensorFault) {
                sensor_rail_counter = sensor_rail_fault ? (sensor_rail_counter + 1) : 0;
                if (sensor_rail_counter >= SENSOR_FAULT_RAIL_COUNT) {
                    state = SystemState::SensorFault;
                    reset_counters();
                    sensor_recover_counter = 0;
                    printf("STATE => FAULT, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                }
            } else {
                sensor_recover_counter = sensor_rail_fault ? 0 : (sensor_recover_counter + 1);
                if (sensor_recover_counter >= SENSOR_FAULT_RECOVER_COUNT) {
                    state = SystemState::Preheating;
                    preheat_start = now;
                    baseline_sum = baseline_count = level_percent = 0;
                    reset_counters();
                    sensor_rail_counter = sensor_recover_counter = 0;
                    printf("FAULT cleared. Restart preheating.\r\n");
                }
            }

            if (state != SystemState::SensorFault) {
                if (state == SystemState::Preheating) {
                    baseline_sum += filtered_adc;
                    ++baseline_count;

                    if ((now - preheat_start) >= PREHEAT_TIME) {
                        baseline_adc = (baseline_count > 0) ? (baseline_sum / baseline_count) : filtered_adc;
                        warning_threshold_adc = clamp_int(baseline_adc + WARNING_OFFSET, WARNING_THRESHOLD_MIN, ADC_MAX_VALUE - 180);
                        danger_threshold_adc = clamp_int(baseline_adc + DANGER_OFFSET, warning_threshold_adc + 80, ADC_MAX_VALUE - 80);
                        state = SystemState::Safe;
                        reset_counters();
                        safe_idle_start = now;
                        level_percent = 0;
                        printf("Calibration done. baseline=%d warning=%d danger=%d\r\n", baseline_adc, warning_threshold_adc, danger_threshold_adc);
                    }
                } else {
                    const int warning_release_threshold = warning_threshold_adc - WARNING_HYSTERESIS;
                    const int danger_release_threshold = danger_threshold_adc - DANGER_HYSTERESIS;
                    const int sleep_wake_threshold = baseline_adc + SLEEP_WAKE_OFFSET;

                    const int level_span = (danger_threshold_adc > baseline_adc) ? (danger_threshold_adc - baseline_adc) : 1;
                    level_percent = clamp_int(((filtered_adc - baseline_adc) * 100) / level_span, 0, 180);

                    if (state == SystemState::Sleep) {
                        if (filtered_adc >= sleep_wake_threshold) {
                            ++sleep_wake_counter;
                            if (sleep_wake_counter >= SLEEP_WAKE_COUNT) {
                                sleep_wake_counter = 0;
                                safe_idle_start = now;
                                if (filtered_adc >= danger_threshold_adc)      { state = SystemState::Danger;  printf("STATE => DANGER, wake raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                                else if (filtered_adc >= warning_threshold_adc) { state = SystemState::Warning; printf("STATE => WARNING, wake raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                                else                                           { state = SystemState::Safe;    printf("STATE => SAFE, wake raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                            }
                        } else {
                            sleep_wake_counter = 0;
                        }
                    } else if (state == SystemState::Safe) {
                        if (filtered_adc >= warning_threshold_adc) {
                            ++trigger_counter;
                            if (trigger_counter >= WARNING_ENTER_COUNT) {
                                state = SystemState::Warning;
                                reset_counters();
                                printf("STATE => WARNING, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else {
                            trigger_counter = 0;
                            if (filtered_adc < sleep_wake_threshold && (now - safe_idle_start) >= AUTO_SLEEP_AFTER) {
                                state = SystemState::Sleep;
                                reset_counters();
                                printf("STATE => SLEEP, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            } else if (filtered_adc >= sleep_wake_threshold) {
                                safe_idle_start = now;
                            }
                        }
                    } else if (state == SystemState::Warning) {
                        if (filtered_adc >= danger_threshold_adc) {
                            ++trigger_counter;
                            release_counter = 0;
                            if (trigger_counter >= DANGER_ENTER_COUNT) {
                                state = SystemState::Danger;
                                reset_counters();
                                printf("STATE => DANGER, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else if (filtered_adc < warning_release_threshold) {
                            ++release_counter;
                            trigger_counter = 0;
                            if (release_counter >= WARNING_RELEASE_COUNT) {
                                state = SystemState::Safe;
                                reset_counters();
                                safe_idle_start = now;
                                printf("STATE => SAFE, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else {
                            trigger_counter = release_counter = 0;
                        }
                    } else if (state == SystemState::Danger) {
                        if (filtered_adc < danger_release_threshold) {
                            ++release_counter;
                            if (release_counter >= DANGER_RELEASE_COUNT) {
                                state = SystemState::Cooldown;
                                cooldown_start = now;
                                reset_counters();
                                printf("STATE => COOLDOWN, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else {
                            release_counter = 0;
                        }
                    } else if (state == SystemState::Cooldown) {
                        if ((now - cooldown_start) >= COOLDOWN_TIME) {
                            if (filtered_adc >= danger_threshold_adc)      { state = SystemState::Danger;  printf("STATE => DANGER, raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                            else if (filtered_adc >= warning_threshold_adc) { state = SystemState::Warning; printf("STATE => WARNING, raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                            else                                           { state = SystemState::Safe;    safe_idle_start = now; printf("STATE => SAFE, raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                        }
                    }
                }
            }

            set_outputs(state, elapsed_ms, led_green, led_red, buzzer);
        }

        if (oled_ok && now >= next_display) {
            next_display += DISPLAY_PERIOD;

            auto sec_left = [&](const auto &start, auto duration) {
                int s = static_cast<int>(duration.count()) -
                        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
                return clamp_int(s, 0, static_cast<int>(duration.count()));
            };
            int seconds_left = (state == SystemState::Preheating) ? sec_left(preheat_start, PREHEAT_TIME) : 0;
            int cooldown_left = (state == SystemState::Cooldown) ? sec_left(cooldown_start, COOLDOWN_TIME) : 0;

            update_display(oled, state, raw_adc, filtered_adc, baseline_adc, warning_threshold_adc, danger_threshold_adc,
                           level_percent, seconds_left, cooldown_left);
        }

        if (ENABLE_BLE_TELEMETRY && now >= next_ble) {
            next_ble += BLE_TELEMETRY_PERIOD;
            send_ble_telemetry(ble_uart, state, raw_adc, filtered_adc, baseline_adc, warning_threshold_adc,
                               danger_threshold_adc, level_percent);
        }

        ThisThread::sleep_for(20ms);
    }
}
