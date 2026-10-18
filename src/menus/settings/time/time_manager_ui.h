#pragma once

#include <Adafruit_SSD1306.h>

namespace TimeManagerUI {

void begin(Adafruit_SSD1306& display);
void update();
void draw();

} // namespace TimeManagerUI