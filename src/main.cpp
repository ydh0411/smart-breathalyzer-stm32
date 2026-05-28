#include "mbed.h"
#include "oled_driver.h"
#include "config.h"
#include "filter.h"
#include "outputs.h"
#include "display.h"
#include "ble_handler.h"

#include <cstdio>

int main() {
    // ---- Hardware initialisation ----
    AnalogIn mq3_ao(PIN_MQ3_AO);
    DigitalOut led_green(PIN_LED_GREEN, 0);
    DigitalOut led_red(PIN_LED_RED, 0);
    DigitalOut buzzer(PIN_BUZZER, 0);
    BleHandler ble(PIN_BLE_TX, PIN_BLE_RX);

    // ---- Output self-test on startup ----（上电自检）
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
    // ---- OLED init ----
    Ssd1306 oled(PIN_I2C_SDA, PIN_I2C_SCL);
    const bool oled_ok = oled.init();

    printf("# time_ms,raw_adc,filtered_adc,state,baseline,warning_th,danger_th\r\n");
    if (!oled_ok) {
        printf("OLED init failed. Please check wiring and I2C address.\r\n");
    }
    printf("BLE CMD: W/WAKE, S/SLEEP, C/CAL, T/STAT, L/LOG, H/HELP\r\n");
    // ---- State variables & filter ----
    MovingTrimmedAverage filter;//滤波器
    Timer uptime;
    uptime.start();

    SystemState state = SystemState::Preheating;
    SystemState last_telemetry_state = state;

    int raw_adc = 0, filtered_adc = 0, baseline_adc = 0;
    int warning_threshold_adc = WARNING_THRESHOLD_MIN;
    int danger_threshold_adc = WARNING_THRESHOLD_MIN + 120;
    int level_percent = 0, baseline_sum = 0, baseline_count = 0;
    // Debounce counters for state transitions
    int trigger_counter = 0, release_counter = 0, sleep_wake_counter = 0;
    int sensor_rail_counter = 0, sensor_recover_counter = 0;
    int cooldown_ref_adc = 0;

    // --- Event log book (circular buffer) ---
    //循环缓冲区32条日志
    struct EventLogEntry {
        int64_t timestamp_ms;
        SystemState state;
        int filtered_adc;
        int level_percent;
    };
    EventLogEntry event_log[EVENT_LOG_SIZE] = {};
    size_t event_log_write = 0;
    size_t event_log_count = 0;

    auto log_event = [&](const SystemState st) {
        const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(uptime.elapsed_time()).count();
        const size_t idx = event_log_write % EVENT_LOG_SIZE;
        event_log[idx].timestamp_ms = ts;
        event_log[idx].state = st;
        event_log[idx].filtered_adc = filtered_adc;
        event_log[idx].level_percent = level_percent;
        event_log_write = (event_log_write + 1) % EVENT_LOG_SIZE;
        if (event_log_count < EVENT_LOG_SIZE) ++event_log_count;
        printf("LOG,%lld,%s,%d,%d%%\r\n", ts, state_name(st), filtered_adc, level_percent);
    };
    // ---- Scheduler time points ----
    //这些时间点用于控制传感器采样、OLED刷新、事件日志记录等操作的时间间隔，确保系统按照预定的节奏运行
    auto next_sample = Kernel::Clock::now();
    auto next_display = Kernel::Clock::now();
    auto next_event_snapshot = Kernel::Clock::now();
    auto preheat_start = Kernel::Clock::now();
    auto cooldown_start = Kernel::Clock::now();
    auto safe_idle_start = Kernel::Clock::now();
    // ---- State-transition helpers ----
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
        log_event(state);
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
        ble.send_text(line);
    };

    // ---- BLE command handler ----
    auto process_ble_command = [&](const char *command, const auto &current_now) {//根据接收到的命令字符串，执行相应的操作，如发送状态、进入睡眠、重新校准等，并通过BLE发送确认或错误消息
        if (command == nullptr || command[0] == '\0') return;

        if (std::strcmp(command, "H") == 0 || std::strcmp(command, "HELP") == 0) {
            ble.send_help();
            return;
        }
        if (std::strcmp(command, "T") == 0 || std::strcmp(command, "STAT") == 0) {
            send_status(current_now);
            return;
        }
        if (std::strcmp(command, "S") == 0 || std::strcmp(command, "SLEEP") == 0) {
            enter_sleep(current_now);
            ble.send_ack("SLEEP");
            printf("CMD => SLEEP\r\n");
            return;
        }
        if (std::strcmp(command, "W") == 0 || std::strcmp(command, "WAKE") == 0) {
            if (state == SystemState::Sleep) {
                state = SystemState::Safe;
                reset_counters();
                safe_idle_start = current_now;
                log_event(state);
                ble.send_ack("WAKE");
                printf("CMD => WAKE\r\n");
            } else {
                ble.send_ack("WAKE (NOOP)");
            }
            return;
        }
        if (std::strcmp(command, "C") == 0 || std::strcmp(command, "CAL") == 0 || std::strcmp(command, "RECAL") == 0) {
            reset_to_preheating(current_now);
            log_event(SystemState::Preheating);
            ble.send_ack("CAL");
            printf("CMD => CAL\r\n");
            return;
        }
        if (std::strcmp(command, "L") == 0 || std::strcmp(command, "LOG") == 0) {
            ble.send_log_header(event_log_count, EVENT_LOG_SIZE);
            for (size_t i = 0; i < event_log_count; ++i) {
                const size_t idx = (event_log_count < EVENT_LOG_SIZE)
                    ? i
                    : (event_log_write + i) % EVENT_LOG_SIZE;
                const auto &e = event_log[idx];
                ble.send_log_entry(e.timestamp_ms, state_name(e.state), e.filtered_adc, e.level_percent);
            }
            ble.send_log_footer();
            return;
        }
        ble.send_text("ERR UNKNOWN\r\n");
    };

    // ===================== Main loop =====================
    while (true) {
        const auto now = Kernel::Clock::now();

        // ---- BLE command polling ----
        if (ble.poll(now)) {
            process_ble_command(ble.command(), now);
            ble.clear_command();
        }

        // ---- Sensor sampling & state machine ----
        //这部分就是通过一些if-else逻辑来实现状态机，根据当前状态和传感器读数的关系，决定是否进行状态转换，并在必要时重置计数器、记录事件日志等
        if (now >= next_sample) {
            next_sample += SAMPLE_PERIOD;

            raw_adc = static_cast<int>(mq3_ao.read() * ADC_MAX_VALUE + 0.5f);
            raw_adc = clamp_int(raw_adc, 0, ADC_MAX_VALUE);

            filter.push(raw_adc);
            filtered_adc = filter.value();

            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(uptime.elapsed_time()).count();

            if (DATA_LOGGING) {
                printf("DATA,%lld,%d,%d,%s,%d,%d,%d\r\n",
                       elapsed_ms, raw_adc, filtered_adc, state_name(state),
                       baseline_adc, warning_threshold_adc, danger_threshold_adc);
            }

            const bool sensor_rail_fault = (raw_adc <= SENSOR_FAULT_LOW_ADC || raw_adc >= SENSOR_FAULT_HIGH_ADC);

            if (state != SystemState::SensorFault) {
                sensor_rail_counter = sensor_rail_fault ? (sensor_rail_counter + 1) : 0;
                if (sensor_rail_counter >= SENSOR_FAULT_RAIL_COUNT) {
                    state = SystemState::SensorFault;
                    reset_counters();
                    sensor_recover_counter = 0;
                    log_event(state);
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
                    log_event(state);
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
                        log_event(state);
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
                                if (filtered_adc >= danger_threshold_adc)      { state = SystemState::Danger;  log_event(state); printf("STATE => DANGER, wake raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                                else if (filtered_adc >= warning_threshold_adc) { state = SystemState::Warning; log_event(state); printf("STATE => WARNING, wake raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                                else                                           { state = SystemState::Safe;    log_event(state); printf("STATE => SAFE, wake raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                            }
                        } else {
                            sleep_wake_counter = 0;
                        }
                        //SAFE
                    } else if (state == SystemState::Safe) {
                        if (filtered_adc >= warning_threshold_adc) {
                            ++trigger_counter;
                            if (trigger_counter >= WARNING_ENTER_COUNT) {
                                state = SystemState::Warning;
                                reset_counters();
                                log_event(state);
                                printf("STATE => WARNING, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else {
                            //SLEEP
                            trigger_counter = 0;
                            if (filtered_adc < sleep_wake_threshold && (now - safe_idle_start) >= AUTO_SLEEP_AFTER) {
                                state = SystemState::Sleep;
                                reset_counters();
                                log_event(state);
                                printf("STATE => SLEEP, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            } else if (filtered_adc >= sleep_wake_threshold) {
                                safe_idle_start = now;
                            }
                        }
                        //WARNING
                    } else if (state == SystemState::Warning) {
                        if (filtered_adc >= danger_threshold_adc) {
                            ++trigger_counter;
                            release_counter = 0;
                            if (trigger_counter >= DANGER_ENTER_COUNT) {
                                state = SystemState::Danger;
                                reset_counters();
                                log_event(state);
                                printf("STATE => DANGER, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else if (filtered_adc < warning_release_threshold) {
                            ++release_counter;
                            trigger_counter = 0;
                            if (release_counter >= WARNING_RELEASE_COUNT) {
                                state = SystemState::Safe;
                                reset_counters();
                                safe_idle_start = now;
                                log_event(state);
                                printf("STATE => SAFE, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else {
                            trigger_counter = release_counter = 0;
                        }//DANGER
                    } else if (state == SystemState::Danger) {
                        if (filtered_adc < danger_release_threshold) {
                            ++release_counter;
                            if (release_counter >= DANGER_RELEASE_COUNT) {
                                state = SystemState::Cooldown;
                                cooldown_start = now;
                                cooldown_ref_adc = filtered_adc;
                                reset_counters();
                                log_event(state);
                                printf("STATE => COOLDOWN, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            }
                        } else {
                            release_counter = 0;
                        }//COOLDOWN
                    } else if (state == SystemState::Cooldown) {
                        if ((now - cooldown_start) >= COOLDOWN_TIME) {
                            if (filtered_adc <= warning_release_threshold) {
                                state = SystemState::Safe;
                                safe_idle_start = now;
                                log_event(state);
                                printf("STATE => SAFE, raw=%d avg=%d\r\n", raw_adc, filtered_adc);
                            } else if (filtered_adc > cooldown_ref_adc) {
                                if (filtered_adc >= danger_threshold_adc)      { state = SystemState::Danger;  log_event(state); printf("STATE => DANGER, raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                                else if (filtered_adc >= warning_threshold_adc) { state = SystemState::Warning; log_event(state); printf("STATE => WARNING, raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                                else                                           { state = SystemState::Safe;    log_event(state); safe_idle_start = now; printf("STATE => SAFE, raw=%d avg=%d\r\n", raw_adc, filtered_adc); }
                            }
                        }//COOLDOWN状态最复杂：在冷却状态下，一共有几种情况。1.如果冷却时间到了，且读数已经降到安全阈值以下，直接转安全；
                        //2.如果读数回升了，则根据当前读数判断是转回危险、警告还是安全；
                        //3.如果读数没有回升，则继续保持冷却状态，直到冷却时间结束后再进行判断。这种设计允许系统在冷却期间对环境变化做出响应，而不是完全锁死在冷却状态，提供了一定的灵活性和适应性。
                    }
                }
            }

            set_outputs(state, elapsed_ms, led_green, led_red, buzzer);
        }

        // ---- Display update ----
        //更新OLED显示屏的内容，显示当前状态、传感器读数、剩余时间等信息，以便用户可以直观地了解系统的运行情况和环境状态
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

        // ---- BLE telemetry (state-change trigger) ----
        //当系统状态发生变化时，如果启用了BLE遥测功能，就会通过BLE发送当前状态和相关数据，以便远程监控和记录系统的运行情况。这种设计允许用户或管理员在不直接接触设备的情况下，实时了解系统状态和环境变化，提供了便利性和灵活性。
        if (ENABLE_BLE_TELEMETRY && state != last_telemetry_state) {
            last_telemetry_state = state;
            ble.send_telemetry(state, raw_adc, filtered_adc, baseline_adc,
                               warning_threshold_adc, danger_threshold_adc, level_percent);
        }

        // ---- Periodic event log snapshot during alerts ----
        //记录每次状态变化，为未来做好记录
        if (state == SystemState::Warning || state == SystemState::Danger) {
            if (now >= next_event_snapshot) {
                next_event_snapshot += EVENT_LOG_SNAPSHOT_PERIOD;
                log_event(state);
            }
        } else {
            next_event_snapshot = now;
        }

        ThisThread::sleep_for(20ms);
    }
}
