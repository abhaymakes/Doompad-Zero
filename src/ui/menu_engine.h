
#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

enum class MenuLayout {
    List,
    IconGrid
};

enum class MenuItemType {
    Action,
    Submenu,
    Toggle,
    Value,
    Choice,
    Info
};

struct MenuItem {
    const char *name;
    MenuItemType type;

    void (*action)();

    bool *toggleValue;

    int *value;
    int minValue;
    int maxValue;

    const char **choices;
    uint8_t choiceCount;

    const unsigned char *icon;
};

class Menu {
public:
    Menu(
        Adafruit_SSD1306 &display,
        const char *title,
        MenuItem *items,
        uint8_t itemCount,
        MenuLayout layout = MenuLayout::List
    );

    void update();
    void draw();
    void drawMainGrid();
    void resetEditMode();

private:
    Adafruit_SSD1306 &display;

    const char *title;
    MenuItem *items;
    uint8_t itemCount;
    MenuLayout layout;

    uint8_t selected = 0;
    uint8_t scrollOffset = 0;
    bool editing = false;

    uint8_t getVisibleItems() const;

    void drawItem(uint8_t index);
    void activate();
    void changeValue(int amount);

    void moveList(int direction);
    void moveGridHorizontal(int direction);
    void moveGridVertical(int direction);
};
