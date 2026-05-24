#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include "mbed.h"
#include "outputs.h"
#include <cctype>
#include <cstdio>
#include <cstring>

class BleHandler {
public:
    BleHandler(PinName tx, PinName rx, int baud = 9600)
        : uart_(tx, rx, baud) {
        uart_.set_blocking(false);
    }

    void send_text(const char *text) {
        if (text == nullptr) return;
        size_t remaining = std::strlen(text);
        while (remaining > 0) {
            const ssize_t written = uart_.write(text, remaining);
            if (written <= 0) break;
            text += written;
            remaining -= static_cast<size_t>(written);
        }
    }

    void send_telemetry(const SystemState state, int raw, int filtered,
                        int baseline, int warning_th, int danger_th, int level_pct) {
        char frame[128];
        const int len = std::snprintf(frame, sizeof(frame),
            "STATE=%s,RAW=%d,AVG=%d,BASE=%d,W=%d,D=%d,LVL=%d\r\n",
            state_name(state), raw, filtered, baseline, warning_th, danger_th, level_pct);
        if (len > 0) send_text(frame);
    }

    void send_help() {
        send_text("CMDS: W/WAKE,S/SLEEP,C/CAL,T/STAT,L/LOG,H/HELP\r\n");
    }

    void send_ack(const char *msg) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ACK %s\r\n", msg);
        send_text(buf);
    }

    void send_log_header(size_t entries, int max_entries) {
        char header[64];
        std::snprintf(header, sizeof(header), "--- LOG BOOK (%zu/%d entries) ---\r\n", entries, max_entries);
        send_text(header);
        printf("%s", header);
    }

    void send_log_entry(int64_t timestamp_ms, const char *state_str, int adc, int lvl_pct) {
        char line[100];
        std::snprintf(line, sizeof(line),
            "[%5lld.%01lld s] %-8s  ADC=%4d  LVL=%3d%%\r\n",
            timestamp_ms / 1000, (timestamp_ms / 100) % 10, state_str, adc, lvl_pct);
        send_text(line);
        printf("%s", line);
    }

    void send_log_footer() {
        send_text("--- END LOG ---\r\n");
        printf("--- END LOG ---\r\n");
    }

    bool poll(Kernel::Clock::time_point now) {
        char rx = 0;
        while (uart_.read(&rx, 1) == 1) {
            const unsigned char byte = static_cast<unsigned char>(rx);
            if (std::isalpha(byte)) {
                if (cmd_len_ < sizeof(cmd_buf_) - 1) {
                    cmd_buf_[cmd_len_++] = static_cast<char>(std::toupper(byte));
                    cmd_buf_[cmd_len_] = '\0';
                    last_rx_ = now;
                }
                // else: silently ignore — buffer at capacity
            } else if (rx == '\r' || rx == '\n' || rx == ' ' || rx == '\t') {
                // separators ignored; command boundary = 60ms idle gap
            } else {
                cmd_len_ = 0;
                cmd_buf_[0] = '\0';
            }
        }
        return (cmd_len_ > 0 && (now - last_rx_) >= 60ms);
    }

    const char *command() const { return cmd_buf_; }

    void clear_command() {
        cmd_len_ = 0;
        cmd_buf_[0] = '\0';
    }

private:
    BufferedSerial uart_;
    char cmd_buf_[32] = {};
    size_t cmd_len_ = 0;
    Kernel::Clock::time_point last_rx_{};
};

#endif
