#ifndef QR_DISPLAY_H
#define QR_DISPLAY_H

#include <Arduino.h>
#include "qrcode.h"
#include "tft_driver.h"

class QRDisplay {
public:
    QRDisplay(TFTDriver& tft) : _tft(tft) {}

    // 在螢幕中央繪製 QR Code
    void draw(const char* text, int16_t offsetY = 0) {
        QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(3)];
        qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, text);

        // 計算每個模組的像素大小
        uint8_t moduleSize = 6;
        uint16_t qrSize = qrcode.size * moduleSize;

        // 置中位置
        int16_t startX = (TFT_WIDTH - qrSize) / 2;
        int16_t startY = (TFT_HEIGHT - qrSize) / 2 + offsetY;

        // 繪製白色背景 (含邊框)
        int16_t padding = moduleSize * 2;
        _tft.fillRect(startX - padding, startY - padding,
                      qrSize + padding * 2, qrSize + padding * 2, COLOR_WHITE);

        // 繪製 QR Code
        for (uint8_t y = 0; y < qrcode.size; y++) {
            for (uint8_t x = 0; x < qrcode.size; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    _tft.fillRect(startX + x * moduleSize,
                                  startY + y * moduleSize,
                                  moduleSize, moduleSize, COLOR_BLACK);
                }
            }
        }
    }

    // 繪製 WiFi 連線用 QR Code
    void drawWiFiQR(const char* ssid, const char* password = nullptr, int16_t offsetY = 0) {
        char qrText[128];
        if (password && strlen(password) > 0) {
            snprintf(qrText, sizeof(qrText), "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
        } else {
            snprintf(qrText, sizeof(qrText), "WIFI:T:nopass;S:%s;;", ssid);
        }
        draw(qrText, offsetY);
    }

    // 繪製 URL QR Code
    void drawURLQR(const char* url, int16_t offsetY = 0) {
        draw(url, offsetY);
    }

private:
    TFTDriver& _tft;
};

#endif
