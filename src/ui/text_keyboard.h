#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

namespace TextKeyboard {

    enum class Result : uint8_t {
        Editing,
        Accepted,
        Cancelled
    };

    void begin(
        Adafruit_SSD1306& display,
        const char* title,
        const String& initialText = "",
        size_t maxLength = 63
    );

    Result update();
    void draw();

    const String& value();
    void setValue(const String& text);
    bool isOpen();

}