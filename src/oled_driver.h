#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include "mbed.h"
#include <array>
#include <cctype>
#include <cstdint>

namespace {

constexpr int OLED_ADDR_8BIT = 0x78;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_BUFFER_SIZE = (OLED_WIDTH * OLED_HEIGHT) / 8;

int clamp_int(const int value, const int lo, const int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

const uint8_t *glyph_for(char c);

} // namespace

class Ssd1306 {
public:
    Ssd1306(PinName sda, PinName scl) : i2c_(sda, scl) {}

    bool init() {
        i2c_.frequency(400000);

        static const uint8_t init_sequence[] = {
            0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
            0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
            0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
        };

        for (const uint8_t cmd : init_sequence) {
            if (!write_command(cmd)) return false;
        }

        clear();
        return flush();
    }

    void clear() { framebuffer_.fill(0); }

    bool flush() {
        if (!set_window()) return false;

        size_t offset = 0;
        while (offset < framebuffer_.size()) {
            char packet[17];
            packet[0] = 0x40;

            size_t payload_len = 16;
            if (framebuffer_.size() - offset < payload_len)
                payload_len = framebuffer_.size() - offset;

            for (size_t i = 0; i < payload_len; ++i)
                packet[i + 1] = static_cast<char>(framebuffer_[offset + i]);

            if (i2c_.write(OLED_ADDR_8BIT, packet, static_cast<int>(payload_len + 1)) != 0)
                return false;
            offset += payload_len;
        }
        return true;
    }

    void draw_text(int x, int y, const char *text) {
        if (text == nullptr) return;
        while (*text != '\0' && x <= (OLED_WIDTH - 6)) {
            draw_char(x, y, *text);
            x += 6;
            ++text;
        }
    }

    void draw_progress_bar(int x, int y, int width, int height, int percent) {
        percent = clamp_int(percent, 0, 100);
        if (width < 4 || height < 4) return;

        const int inner_width = width - 2;
        const int inner_height = height - 2;
        const int filled_width = (inner_width * percent) / 100;

        for (int dx = 0; dx < width; ++dx) {
            draw_pixel(x + dx, y, true);
            draw_pixel(x + dx, y + height - 1, true);
        }
        for (int dy = 0; dy < height; ++dy) {
            draw_pixel(x, y + dy, true);
            draw_pixel(x + width - 1, y + dy, true);
        }
        for (int dx = 0; dx < filled_width; ++dx)
            for (int dy = 0; dy < inner_height; ++dy)
                draw_pixel(x + 1 + dx, y + 1 + dy, true);
    }

private:
    bool set_window() {
        return write_command(0x21) && write_command(0x00) && write_command(0x7F)
            && write_command(0x22) && write_command(0x00) && write_command(0x07);
    }

    bool write_command(const uint8_t command) {
        char packet[2];
        packet[0] = 0x00;
        packet[1] = static_cast<char>(command);
        return i2c_.write(OLED_ADDR_8BIT, packet, 2) == 0;
    }

    void draw_char(const int x, const int y, const char c) {
        const uint8_t *glyph = glyph_for(c);
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = glyph[col];
            for (int row = 0; row < 8; ++row) {
                draw_pixel(x + col, y + row, (bits & (1U << row)) != 0);
            }
        }
        for (int row = 0; row < 8; ++row)
            draw_pixel(x + 5, y + row, false);
    }

    void draw_pixel(const int x, const int y, const bool on) {
        if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
        const int page = y / 8;
        const int bit = y % 8;
        const size_t index = static_cast<size_t>(page * OLED_WIDTH + x);
        const uint8_t mask = static_cast<uint8_t>(1U << bit);
        if (on)
            framebuffer_[index] |= mask;
        else
            framebuffer_[index] &= static_cast<uint8_t>(~mask);
    }

    I2C i2c_;
    std::array<uint8_t, OLED_BUFFER_SIZE> framebuffer_{};
};

// --- 5x8 font glyphs (A-Z, 0-9, symbols) ---

namespace {

const uint8_t *glyph_for(char c) {
    static const uint8_t blank[5]     = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t colon[5]     = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t dash[5]      = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t l_bracket[5] = {0x7F, 0x41, 0x41, 0x00, 0x00};
    static const uint8_t r_bracket[5] = {0x00, 0x00, 0x41, 0x41, 0x7F};
    static const uint8_t hash[5]      = {0x14, 0x7F, 0x14, 0x7F, 0x14};

    static const uint8_t n0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t n1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t n2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t n3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t n4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t n5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t n6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t n7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t n8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t n9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};

    static const uint8_t A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    static const uint8_t E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    static const uint8_t H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
    static const uint8_t K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t Q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
    static const uint8_t R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t W[5] = {0x7F, 0x20, 0x18, 0x20, 0x7F};
    static const uint8_t X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
    static const uint8_t Y[5] = {0x03, 0x04, 0x78, 0x04, 0x03};
    static const uint8_t Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    switch (c) {
    case ' ': return blank;
    case ':': return colon;
    case '-': return dash;
    case '[': return l_bracket;
    case ']': return r_bracket;
    case '#': return hash;
    case '0': return n0;
    case '1': return n1;
    case '2': return n2;
    case '3': return n3;
    case '4': return n4;
    case '5': return n5;
    case '6': return n6;
    case '7': return n7;
    case '8': return n8;
    case '9': return n9;
    case 'A': return A;
    case 'B': return B;
    case 'C': return C;
    case 'D': return D;
    case 'E': return E;
    case 'F': return F;
    case 'G': return G;
    case 'H': return H;
    case 'I': return I;
    case 'J': return J;
    case 'K': return K;
    case 'L': return L;
    case 'M': return M;
    case 'N': return N;
    case 'O': return O;
    case 'P': return P;
    case 'Q': return Q;
    case 'R': return R;
    case 'S': return S;
    case 'T': return T;
    case 'U': return U;
    case 'V': return V;
    case 'W': return W;
    case 'X': return X;
    case 'Y': return Y;
    case 'Z': return Z;
    default: return blank;
    }
}

} // namespace

#endif
