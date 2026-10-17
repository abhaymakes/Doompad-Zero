#pragma once

#include <Adafruit_SSD1306.h>

void settingsBegin(Adafruit_SSD1306& display);
void settingsUpdate();
void settingsDraw();

bool settingsBackRequested();