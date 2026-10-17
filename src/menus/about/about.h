
#pragma once

#include <Adafruit_SSD1306.h>

void aboutBegin(Adafruit_SSD1306 &display);
void aboutUpdate();
void aboutDraw();
bool aboutBackRequested();
