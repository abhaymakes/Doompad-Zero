#include "input.h"
#include <Arduino.h>
#include <stdint.h>


namespace {

constexpr uint8_t PIN_DOWN   = 0;
constexpr uint8_t PIN_UP     = 1;
constexpr uint8_t PIN_SELECT = 2;
constexpr uint8_t PIN_LEFT   = 3;
constexpr uint8_t PIN_RIGHT  = 4;

constexpr uint32_t DEBOUNCE_MS   = 35;
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

void updateButton(Button& button, uint32_t now)
{
    // INPUT_PULLUP means LOW = physically pressed.
    const bool reading = digitalRead(button.pin) == LOW;

    if (reading != button.rawReading) {
        button.rawReading = reading;
        button.lastRawChange = now;
    }

    // Accept a new state only after it has remained stable.
    if ((uint32_t)(now - button.lastRawChange) >= DEBOUNCE_MS &&
        button.rawReading != button.stablePressed) {

        button.stablePressed = button.rawReading;

        if (button.stablePressed) {
            // Debounced press.
            button.pressedAt = now;
            button.longFired = false;
            button.pressPending = true;
        } else {
            // Debounced release. A long press must not also
            // generate a short-press event.
            if (!button.longFired) {
                button.shortPending = true;
            }
        }
    }

    // Fire the long-press event once, while still held.
    if (button.stablePressed &&
        !button.longFired &&
        (uint32_t)(now - button.pressedAt) >= LONG_PRESS_MS) {

        button.longFired = true;
        button.longPending = true;

        // Suppress any not-yet-consumed press event for UP.
        if (button.pin == PIN_UP) {
            button.pressPending = false;
        }
    }
}

bool consumePress(Button& button)
{
    if (!button.pressPending) {
        return false;
    }

    button.pressPending = false;
    return true;
}

bool consumeShortPress(Button& button)
{
    if (!button.shortPending) {
        return false;
    }

    button.shortPending = false;
    return true;
}

bool consumeLongPress(Button& button)
{
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
    const uint32_t now = millis();

    for (Button& button : buttons) {
        pinMode(button.pin, INPUT_PULLUP);

        // Initialize from the actual pin state to avoid a
        // false press event immediately after startup.
        const bool pressed = digitalRead(button.pin) == LOW;

        button.rawReading = pressed;
        button.stablePressed = pressed;
        button.lastRawChange = now;
        button.pressedAt = pressed ? now : 0;

        button.longFired = false;
        button.pressPending = false;
        button.shortPending = false;
        button.longPending = false;
    }
}

// Call exactly once per main-loop iteration.
void update()
{
    const uint32_t now = millis();

    for (Button& button : buttons) {
        updateButton(button, now);
    }
}

bool down()
{
    return consumePress(buttons[0]);
}

bool up()
{
    // Short UP is emitted on release.
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
    // Long UP is emitted once after 600 ms.
    return consumeLongPress(buttons[1]);
}

} // namespace Input
