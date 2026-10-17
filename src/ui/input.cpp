
#include "input.h"

namespace {

constexpr uint8_t PIN_DOWN   = 0;
constexpr uint8_t PIN_UP     = 1;
constexpr uint8_t PIN_SELECT = 2;
constexpr uint8_t PIN_LEFT   = 3;
constexpr uint8_t PIN_RIGHT  = 4;

constexpr uint32_t DEBOUNCE_MS = 35;
constexpr uint32_t LONG_PRESS_MS = 600;

struct Button {
    uint8_t pin;
    bool rawReading;
    bool stablePressed;
    uint32_t lastRawChange;
    uint32_t pressedAt;
    bool longFired;
    bool pressPending;
    bool shortPending;
    bool longPending;
};

Button buttons[] = {
    {PIN_DOWN,   false, false, 0, 0, false, false, false, false},
    {PIN_UP,     false, false, 0, 0, false, false, false, false},
    {PIN_SELECT, false, false, 0, 0, false, false, false, false},
    {PIN_LEFT,   false, false, 0, 0, false, false, false, false},
    {PIN_RIGHT,  false, false, 0, 0, false, false, false, false}
};

void updateButton(Button &button)
{
    uint32_t now = millis();
    bool reading = digitalRead(button.pin) == LOW;

    if (reading != button.rawReading) {
        button.rawReading = reading;
        button.lastRawChange = now;
    }

    if (now - button.lastRawChange >= DEBOUNCE_MS &&
        button.rawReading != button.stablePressed) {

        button.stablePressed = button.rawReading;

        if (button.stablePressed) {
            button.pressedAt = now;
            button.longFired = false;
            button.pressPending = true;
        } else {
            if (!button.longFired) {
                button.shortPending = true;
            }
        }
    }

    if (button.stablePressed &&
        !button.longFired &&
        now - button.pressedAt >= LONG_PRESS_MS) {

        button.longFired = true;
        button.longPending = true;
    }
}

bool consumePress(Button &button)
{
    updateButton(button);

    if (!button.pressPending) {
        return false;
    }

    button.pressPending = false;
    return true;
}

bool consumeShortPress(Button &button)
{
    updateButton(button);

    if (!button.shortPending) {
        return false;
    }

    button.shortPending = false;
    return true;
}

bool consumeLongPress(Button &button)
{
    updateButton(button);

    if (!button.longPending) {
        return false;
    }

    button.longPending = false;
    return true;
}

} // namespace

namespace Input {

void begin()
{
    for (Button &button : buttons) {
        pinMode(button.pin, INPUT_PULLUP);

        button.rawReading = false;
        button.stablePressed = false;
        button.lastRawChange = millis();
        button.pressedAt = 0;

        button.longFired = false;
        button.pressPending = false;
        button.shortPending = false;
        button.longPending = false;
    }
}

bool down()
{
    return consumePress(buttons[0]);
}

bool up()
{
    // UP is reported on release if it wasn't a long press.
    return consumeShortPress(buttons[1]);
}

bool select()
{
    return consumePress(buttons[2]);
}

bool left()
{
    return consumePress(buttons[3]);
}

bool right()
{
    return consumePress(buttons[4]);
}

bool menuBack()
{
    return consumeLongPress(buttons[1]);
}

}
