
#include "about.h"
#include "../../ui/input.h"
#include "../../ui/menu_icons.h"

static Adafruit_SSD1306 *oled = nullptr;
static bool backRequested = false;

void aboutBegin(Adafruit_SSD1306 &display)
{
    oled = &display;
    backRequested = false;
    aboutDraw();
}

void aboutDraw()
{
    if (!oled) return;

    oled->clearDisplay();
    oled->setTextColor(SSD1306_WHITE);
    oled->setTextSize(1);

    // 64x64 QR code on the left.
    oled->drawBitmap(
        0, 0,
        epd_bitmap_about_qr,
        64, 64,
        SSD1306_WHITE
    );

    // Branding and firmware information on the right.
    oled->setCursor(69, 5);
    oled->print("Doompad");

    oled->setCursor(69, 15);
    oled->print("Zero");

    oled->drawFastHLine(69, 27, 55, SSD1306_WHITE);

    oled->setCursor(69, 34);
    oled->print("Firmware");

    oled->setCursor(69, 46);
    oled->print("1.0.0");

    oled->display();
}

void aboutUpdate()
{
    if (Input::menuBack()) {
        backRequested = true;
    }
}

bool aboutBackRequested()
{
    if (backRequested) {
        backRequested = false;
        return true;
    }

    return false;
}
