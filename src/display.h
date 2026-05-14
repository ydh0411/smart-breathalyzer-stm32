#ifndef DISPLAY_H
#define DISPLAY_H

#include "oled_driver.h"
#include "outputs.h"
#include <cstdio>

static void update_display(Ssd1306 &oled, const SystemState state, const int raw, const int filtered,
                           const int baseline, const int warning_threshold, const int danger_threshold,
                           const int level_percent, const int seconds_left, const int cooldown_left) {
    oled.clear();

    char line[24];
    oled.draw_text(0, 0, "BREATHALYZER");

    std::snprintf(line, sizeof(line), "STATE: %s", state_name(state));
    oled.draw_text(0, 16, line);

    std::snprintf(line, sizeof(line), "LVL:%3d%%", clamp_int(level_percent, 0, 100));
    oled.draw_text(0, 26, line);
    oled.draw_progress_bar(42, 26, 84, 10, level_percent);

    if (state == SystemState::Preheating) {
        std::snprintf(line, sizeof(line), "WAIT:%02d SEC", seconds_left);
        oled.draw_text(0, 38, line);
    } else if (state == SystemState::Cooldown) {
        std::snprintf(line, sizeof(line), "COOLWAIT:%02dS", cooldown_left);
        oled.draw_text(0, 38, line);
    } else if (state == SystemState::Sleep) {
        oled.draw_text(0, 38, "BLE: W TO WAKE");
    } else if (state == SystemState::SensorFault) {
        oled.draw_text(0, 38, "[CHECK SENSOR]");
    } else {
        std::snprintf(line, sizeof(line), "VAL : %4d", filtered);
        oled.draw_text(0, 38, line);
    }

    std::snprintf(line, sizeof(line), "W:%4d D:%4d", warning_threshold, danger_threshold);
    oled.draw_text(0, 50, line);

    oled.flush();
}

static void send_ble_telemetry(BufferedSerial &ble_uart, const SystemState state, const int raw,
                               const int filtered, const int baseline, const int warning_threshold,
                               const int danger_threshold, const int level_percent) {
    char frame[128];
    const int len = std::snprintf(frame, sizeof(frame),
        "STATE=%s,RAW=%d,AVG=%d,BASE=%d,W=%d,D=%d,LVL=%d\r\n",
        state_name(state), raw, filtered, baseline, warning_threshold, danger_threshold, level_percent);
    if (len > 0) ble_uart.write(frame, len);
}

#endif
