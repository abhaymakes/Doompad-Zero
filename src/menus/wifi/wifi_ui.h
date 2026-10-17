
#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

namespace WifiUI {

    void begin(Adafruit_SSD1306& display);
    void update();
    void draw();

    // True when the Wi-Fi UI wants the main application
    // to return to its previous screen.
    bool backRequested();

}
