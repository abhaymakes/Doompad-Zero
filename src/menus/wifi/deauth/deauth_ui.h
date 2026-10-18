#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

namespace DeauthPage {

    void begin(Adafruit_SSD1306& display);
    void update();
    void draw();
    void stop();
    bool backRequested();

}