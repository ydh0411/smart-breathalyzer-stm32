// High-level OLED display logic — renders system status, readings, and progress bar
//这是具体的显示逻辑，负责将系统状态、传感器读数和进度条等信息渲染到OLED屏幕上
#ifndef DISPLAY_H
#define DISPLAY_H

#include "mbed.h"
#include "oled_driver.h"
#include "outputs.h"
#include <cstdio>
//整体布局设计
static void update_display(Ssd1306 &oled, const SystemState state, const int raw, const int filtered,
                           const int baseline, const int warning_threshold, const int danger_threshold,
                           const int level_percent, const int seconds_left, const int cooldown_left) {
    oled.clear();

    char line[24];
    oled.draw_text(0, 0, "BREATHALYZER");

    // Current state
    std::snprintf(line, sizeof(line), "STATE: %s", state_name(state));
    oled.draw_text(0, 16, line);

    // Level percentage + visual progress bar
    std::snprintf(line, sizeof(line), "LVL:%3d%%", clamp_int(level_percent, 0, 100));//进度条百分比
    oled.draw_text(0, 26, line);
    oled.draw_progress_bar(42, 26, 84, 10, level_percent);

    // Context-sensitive status line
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
#endif
