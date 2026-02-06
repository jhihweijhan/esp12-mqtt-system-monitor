#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include "font_8x16.h"

// 腳位定義
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   16
#define TFT_DC   0
#define TFT_RST  4
#define TFT_BL   5

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// 顏色定義 (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410

class TFTDriver {
public:
    void begin() {
        pinMode(TFT_CS, OUTPUT);
        pinMode(TFT_DC, OUTPUT);
        pinMode(TFT_RST, OUTPUT);
        pinMode(TFT_BL, OUTPUT);

        digitalWrite(TFT_CS, HIGH);
        digitalWrite(TFT_BL, LOW);  // 背光 ON

        SPI.begin();
        SPI.setFrequency(10000000);
        SPI.setDataMode(SPI_MODE0);
        SPI.setBitOrder(MSBFIRST);

        init();
    }

    void init() {
        digitalWrite(TFT_RST, LOW);
        delay(50);
        digitalWrite(TFT_RST, HIGH);
        delay(150);

        writeCommand(0x11);  // Sleep out
        delay(150);

        writeCommand(0x3A);  // Color mode
        writeData(0x05);     // 16-bit RGB565

        writeCommand(0x36);  // MADCTL
        writeData(0x00);

        writeCommand(0x21);  // Inversion ON

        writeCommand(0x29);  // Display ON
        delay(50);
    }

    void fillScreen(uint16_t color) {
        setAddrWindow(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
        digitalWrite(TFT_DC, HIGH);
        digitalWrite(TFT_CS, LOW);

        uint8_t hi = color >> 8;
        uint8_t lo = color & 0xFF;
        for (uint32_t i = 0; i < (uint32_t)TFT_WIDTH * TFT_HEIGHT; i++) {
            SPI.transfer(hi);
            SPI.transfer(lo);
        }
        digitalWrite(TFT_CS, HIGH);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (x >= TFT_WIDTH || y >= TFT_HEIGHT || w <= 0 || h <= 0) return;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
        if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;

        setAddrWindow(x, y, x + w - 1, y + h - 1);
        digitalWrite(TFT_DC, HIGH);
        digitalWrite(TFT_CS, LOW);

        uint8_t hi = color >> 8;
        uint8_t lo = color & 0xFF;
        uint32_t total = (uint32_t)w * h;
        for (uint32_t i = 0; i < total; i++) {
            SPI.transfer(hi);
            SPI.transfer(lo);
        }
        digitalWrite(TFT_CS, HIGH);
        if (total > 512) yield();  // 大面積填充後讓出 CPU
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) {
        if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) return;

        setAddrWindow(x, y, x, y);
        digitalWrite(TFT_DC, HIGH);
        digitalWrite(TFT_CS, LOW);
        SPI.transfer(color >> 8);
        SPI.transfer(color & 0xFF);
        digitalWrite(TFT_CS, HIGH);
    }

    void drawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size = 1) {
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) return;

        uint16_t index = (c - FONT_FIRST_CHAR) * FONT_HEIGHT;

        for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
            uint8_t line = pgm_read_byte(&font_8x16[index + row]);
            for (uint8_t col = 0; col < FONT_WIDTH; col++) {
                uint16_t px_color = (line & (0x80 >> col)) ? color : bg;
                if (size == 1) {
                    drawPixel(x + col, y + row, px_color);
                } else {
                    fillRect(x + col * size, y + row * size, size, size, px_color);
                }
            }
        }
    }

    void drawString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size = 1) {
        while (*str) {
            drawChar(x, y, *str, color, bg, size);
            x += FONT_WIDTH * size;
            str++;
            yield();  // 讓 WiFi/TCP stack 處理封包
        }
    }

    void drawStringCentered(int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size = 1) {
        int16_t len = strlen(str);
        int16_t x = (TFT_WIDTH - len * FONT_WIDTH * size) / 2;
        drawString(x, y, str, color, bg, size);
    }

    void setBacklight(bool on) {
        digitalWrite(TFT_BL, on ? LOW : HIGH);  // LOW = ON
    }

private:
    void writeCommand(uint8_t cmd) {
        digitalWrite(TFT_DC, LOW);
        digitalWrite(TFT_CS, LOW);
        SPI.transfer(cmd);
        digitalWrite(TFT_CS, HIGH);
    }

    void writeData(uint8_t data) {
        digitalWrite(TFT_DC, HIGH);
        digitalWrite(TFT_CS, LOW);
        SPI.transfer(data);
        digitalWrite(TFT_CS, HIGH);
    }

    void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        writeCommand(0x2A);
        writeData(x0 >> 8);
        writeData(x0 & 0xFF);
        writeData(x1 >> 8);
        writeData(x1 & 0xFF);

        writeCommand(0x2B);
        writeData(y0 >> 8);
        writeData(y0 & 0xFF);
        writeData(y1 >> 8);
        writeData(y1 & 0xFF);

        writeCommand(0x2C);
    }
};

#endif
