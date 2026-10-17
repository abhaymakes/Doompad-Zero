
#pragma once

#include <Arduino.h>

namespace Input {

void begin();

bool up();        // Short press: navigate up
bool down();      // Navigate down
bool left();      // Navigate left / decrease
bool right();     // Navigate right / increase
bool select();    // OK
bool menuBack();  // Long press UP

}
