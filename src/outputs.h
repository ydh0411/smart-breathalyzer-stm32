// System state enum and LED/buzzer output patterns per state
#ifndef OUTPUTS_H
#define OUTPUTS_H

#include "config.h"
#include <cstdint>

static void write_channel(DigitalOut &channel, const bool on, const bool active_low) {
    channel = on ? (active_low ? 0 : 1) : (active_low ? 1 : 0);
}

enum class SystemState {
    Preheating, Safe, Warning, Danger, Cooldown, Sleep, SensorFault,//一共7个状态
};

static const char *state_name(const SystemState state) {
    switch (state) {
    case SystemState::Preheating:  return "WARMING";
    case SystemState::Safe:        return "SAFE";
    case SystemState::Warning:     return "WARNING";
    case SystemState::Danger:      return "DANGER";
    case SystemState::Cooldown:    return "COOLDOWN";
    case SystemState::Sleep:       return "SLEEP";
    case SystemState::SensorFault: return "FAULT";
    default:                       return "UNKNOWN";
    }
}

// Blink/beep patterns driven by elapsed_ms for consistent timing
static void set_outputs(const SystemState state, const int64_t elapsed_ms,
                        DigitalOut &led_green, DigitalOut &led_red, DigitalOut &buzzer) {
    // Timing patterns
    const bool slow_blink = ((elapsed_ms / 500) % 2) == 0;
    const bool fast_blink = ((elapsed_ms / 250) % 2) == 0;
    const bool alt_blink  = ((elapsed_ms / 300) % 2) == 0;

    // Warning: double beep every 1.5s
    const int  mod_1500     = elapsed_ms % 1500;
    const bool warning_beep = (mod_1500 < 80) || (mod_1500 > 160 && mod_1500 < 240);

    // Danger: three short beeps per second
    const int  mod_1000    = elapsed_ms % 1000;
    const bool danger_beep = (mod_1000 < 40) || (mod_1000 > 100 && mod_1000 < 140) || (mod_1000 > 200 && mod_1000 < 240);

    // Fault: one short beep every 2s
    const bool fault_beep = (elapsed_ms % 2000) < 120;

    switch (state) {
    case SystemState::Preheating:
        write_channel(led_green, slow_blink, LED_ACTIVE_LOW);
        write_channel(led_red,   false,      LED_ACTIVE_LOW);
        write_channel(buzzer,    false,      BUZZER_ACTIVE_LOW);
        break;
    case SystemState::Safe:
        write_channel(led_green, true,       LED_ACTIVE_LOW);
        write_channel(led_red,   false,      LED_ACTIVE_LOW);
        write_channel(buzzer,    false,      BUZZER_ACTIVE_LOW);
        break;
    case SystemState::Warning:
        write_channel(led_green, true,       LED_ACTIVE_LOW);
        write_channel(led_red,   fast_blink, LED_ACTIVE_LOW);
        write_channel(buzzer,    warning_beep, BUZZER_ACTIVE_LOW);
        break;
    case SystemState::Danger:
        write_channel(led_green, false,      LED_ACTIVE_LOW);
        write_channel(led_red,   true,       LED_ACTIVE_LOW);
        write_channel(buzzer,    danger_beep, BUZZER_ACTIVE_LOW);
        break;
    case SystemState::Cooldown:
        write_channel(led_green, alt_blink,  LED_ACTIVE_LOW);
        write_channel(led_red,   !alt_blink, LED_ACTIVE_LOW);
        write_channel(buzzer,    false,      BUZZER_ACTIVE_LOW);
        break;
    case SystemState::Sleep:
        write_channel(led_green, false, LED_ACTIVE_LOW);
        write_channel(led_red,   false, LED_ACTIVE_LOW);
        write_channel(buzzer,    false, BUZZER_ACTIVE_LOW);
        break;
    case SystemState::SensorFault:
        write_channel(led_green, false,      LED_ACTIVE_LOW);
        write_channel(led_red,   fast_blink, LED_ACTIVE_LOW);
        write_channel(buzzer,    fault_beep, BUZZER_ACTIVE_LOW);
        break;
    }
}

#endif
