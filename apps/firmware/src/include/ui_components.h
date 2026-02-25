#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <Arduino.h>
#include "tft_driver.h"

// 根據數值取得顏色（綠→黃→紅）
inline uint16_t getValueColor(int value, int warnThreshold = 70, int critThreshold = 90) {
    if (value >= critThreshold) return COLOR_RED;
    if (value >= warnThreshold) return COLOR_YELLOW;
    return COLOR_GREEN;
}

// 根據溫度取得顏色
inline uint16_t getTempColor(int temp, int warnThreshold = 60, int critThreshold = 80) {
    if (temp >= critThreshold) return COLOR_RED;
    if (temp >= warnThreshold) return COLOR_YELLOW;
    return COLOR_CYAN;
}

class UIComponents {
public:
    UIComponents(TFTDriver& tft) : _tft(tft) {}

    // 繪製水平進度條
    void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                         int percent, uint16_t color, uint16_t bgColor = COLOR_GRAY) {
        // 背景
        _tft.fillRect(x, y, w, h, bgColor);
        // 進度
        int16_t fillW = (w * percent) / 100;
        if (fillW > 0) {
            _tft.fillRect(x, y, fillW, h, color);
        }
    }

    // 繪製帶標籤的進度條
    void drawLabeledBar(int16_t x, int16_t y, const char* label,
                        int percent, int16_t barWidth = 100) {
        // 標籤 (4 字元寬)
        _tft.drawString(x, y, label, COLOR_WHITE, COLOR_BLACK, 1);

        // 進度條
        int16_t barX = x + 40;  // 標籤後
        uint16_t color = getValueColor(percent);
        drawProgressBar(barX, y + 2, barWidth, 12, percent, color);

        // 百分比數字
        char buf[8];
        snprintf(buf, sizeof(buf), "%3d%%", percent);
        _tft.drawString(barX + barWidth + 4, y, buf, color, COLOR_BLACK, 1);
    }

    // 繪製大數字（用於主要指標）
    void drawBigValue(int16_t x, int16_t y, const char* label,
                      int value, const char* unit, uint16_t color) {
        // 標籤
        _tft.drawString(x, y, label, COLOR_GRAY, COLOR_BLACK, 1);

        // 大數字
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%s", value, unit);
        _tft.drawString(x, y + 18, buf, color, COLOR_BLACK, 2);
    }

    // 繪製雙欄數值（如 CPU 87% 62C）
    void drawDualValue(int16_t x, int16_t y, const char* label,
                       int val1, const char* unit1, int val2, const char* unit2) {
        // 標籤
        _tft.drawString(x, y, label, COLOR_WHITE, COLOR_BLACK, 2);

        // 第一個值
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%s", val1, unit1);
        uint16_t color1 = getValueColor(val1);
        _tft.drawString(x + 64, y, buf, color1, COLOR_BLACK, 2);

        // 第二個值（溫度）
        snprintf(buf, sizeof(buf), "%d%s", val2, unit2);
        uint16_t color2 = getTempColor(val2);
        _tft.drawString(x + 140, y, buf, color2, COLOR_BLACK, 2);
    }

    // 繪製小型資訊行
    void drawInfoLine(int16_t x, int16_t y, const char* label, const char* value) {
        _tft.drawString(x, y, label, COLOR_GRAY, COLOR_BLACK, 1);
        _tft.drawString(x + 40, y, value, COLOR_WHITE, COLOR_BLACK, 1);
    }

    // 繪製網路流量
    void drawNetworkIO(int16_t x, int16_t y, float rxMbps, float txMbps) {
        char buf[32];

        // 繪製標籤和數值
        _tft.drawString(x, y, "NET", COLOR_GRAY, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "v%.1fM", rxMbps);
        _tft.drawString(x + 32, y, buf, COLOR_GREEN, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "^%.1fM", txMbps);
        _tft.drawString(x + 96, y, buf, COLOR_CYAN, COLOR_BLACK, 1);
    }

    // 繪製離線警告
    void drawOfflineAlert(int16_t y, const char* deviceName) {
        _tft.fillRect(0, y, TFT_WIDTH, 40, COLOR_RED);
        _tft.drawStringCentered(y + 4, "OFFLINE", COLOR_WHITE, COLOR_RED, 2);
        _tft.drawStringCentered(y + 24, deviceName, COLOR_WHITE, COLOR_RED, 1);
    }

    // 繪製設備標題列
    void drawDeviceHeader(const char* name, bool isOnline = true) {
        uint16_t bgColor = isOnline ? 0x1082 : COLOR_RED;  // 深藍或紅色
        _tft.fillRect(0, 0, TFT_WIDTH, 28, bgColor);
        _tft.drawStringCentered(6, name, COLOR_WHITE, bgColor, 2);
    }

private:
    TFTDriver& _tft;
};

#endif
