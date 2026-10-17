
#include "menu_engine.h"
#include "input.h"

Menu::Menu(
    Adafruit_SSD1306 &display,
    const char *title,
    MenuItem *items,
    uint8_t itemCount,
    MenuLayout layout
)
    : display(display),
      title(title),
      items(items),
      itemCount(itemCount),
      layout(layout)
{
}

uint8_t Menu::getVisibleItems() const
{
    const uint8_t textHeight = 8;
    const uint8_t topPadding = 2;
    const uint8_t bottomPadding = 2;
    const uint8_t usableHeight =
        64 - topPadding - bottomPadding;
    const uint8_t rowHeight = textHeight + 3;

    return max(
        static_cast<uint8_t>(1),
        static_cast<uint8_t>(usableHeight / rowHeight)
    );
}

void Menu::update()
{
    if (itemCount == 0) {
        return;
    }

    if (Input::select()) {
        activate();
        draw();
        return;
    }

    if (Input::left()) {
        if (editing) {
            changeValue(-1);
        } else if (layout == MenuLayout::IconGrid) {
            moveGridHorizontal(-1);
        }

        draw();
        return;
    }

    if (Input::right()) {
        if (editing) {
            changeValue(1);
        } else if (layout == MenuLayout::IconGrid) {
            moveGridHorizontal(1);
        }

        draw();
        return;
    }

    if (Input::up()) {
        if (editing) {
            editing = false;
        } else if (layout == MenuLayout::IconGrid) {
            moveGridVertical(-1);
        } else {
            moveList(-1);
        }

        draw();
        return;
    }

    if (Input::down()) {
        if (!editing) {
            if (layout == MenuLayout::IconGrid) {
                moveGridVertical(1);
            } else {
                moveList(1);
            }
        }

        draw();
        return;
    }
}

void Menu::moveList(int direction)
{
    if (itemCount == 0) {
        return;
    }

    uint8_t visibleItems = getVisibleItems();

    if (direction > 0) {
        if (selected >= itemCount - 1) {
            selected = 0;
            scrollOffset = 0;
            return;
        }

        selected++;
    } else {
        if (selected == 0) {
            selected = itemCount - 1;

            scrollOffset =
                itemCount > visibleItems
                    ? itemCount - visibleItems
                    : 0;

            return;
        }

        selected--;
    }

    if (selected < scrollOffset) {
        scrollOffset = selected;
    }

    if (selected >= scrollOffset + visibleItems) {
        scrollOffset = selected - visibleItems + 1;
    }
}


void Menu::moveGridHorizontal(int direction)
{
    if (itemCount == 0) return;

    if (direction > 0) {
        // Move right to the next item, including the next page.
        if (selected < itemCount - 1) {
            selected++;
        }
    } else {
        // Move left to the previous item, including the previous page.
        if (selected > 0) {
            selected--;
        }
    }
}


void Menu::moveGridVertical(int direction)
{
    uint8_t count = min(itemCount, static_cast<uint8_t>(4));

    if (count == 0) {
        return;
    }

    uint8_t rows = (count + 1) / 2;
    uint8_t currentRow = selected / 2;
    uint8_t column = selected % 2;
    uint8_t targetRow;

    if (direction > 0) {
        targetRow = (currentRow + 1) % rows;
    } else {
        targetRow = currentRow == 0
            ? rows - 1
            : currentRow - 1;
    }

    uint8_t target = targetRow * 2 + column;

    // Avoid selecting a nonexistent cell in an incomplete row.
    if (target >= count) {
        target = targetRow * 2;
    }

    selected = target;
}

void Menu::activate()
{
    if (itemCount == 0 || selected >= itemCount) {
        return;
    }

    MenuItem &item = items[selected];

    if (editing) {
        editing = false;
        return;
    }

    switch (item.type) {
    case MenuItemType::Toggle:
    case MenuItemType::Value:
    case MenuItemType::Choice:
        editing = true;
        break;

    case MenuItemType::Action:
    case MenuItemType::Submenu:
        if (item.action) {
            item.action();
        }
        break;

    case MenuItemType::Info:
        // Information items are display-only for now.
        break;
    }
}

void Menu::changeValue(int amount)
{
    if (itemCount == 0 || selected >= itemCount) {
        return;
    }

    MenuItem &item = items[selected];

    if (!editing) {
        return;
    }

    if (item.type == MenuItemType::Toggle) {
        if (item.toggleValue) {
            *item.toggleValue = amount > 0;
        }

        return;
    }

    if (!item.value) {
        return;
    }

    if (item.type == MenuItemType::Choice) {
        if (item.choiceCount == 0) {
            return;
        }

        int next = *item.value + amount;

        if (next >= item.choiceCount) {
            next = 0;
        } else if (next < 0) {
            next = item.choiceCount - 1;
        }

        *item.value = next;
        return;
    }

    if (item.type == MenuItemType::Value) {
        int next = *item.value + amount;

        if (next > item.maxValue) {
            next = item.minValue;
        } else if (next < item.minValue) {
            next = item.maxValue;
        }

        *item.value = next;
    }
}

void Menu::drawItem(uint8_t index)
{
    MenuItem &item = items[index];

    const uint8_t textHeight = 8;
    const uint8_t rowHeight = textHeight + 3;
    const uint8_t topPadding = 2;

    int16_t y =
        topPadding +
        ((index - scrollOffset) * rowHeight) +
        ((rowHeight - textHeight) / 2);

    bool focused = index == selected;
    bool editingThisItem = focused && editing;

    if (editingThisItem) {
        // Filled row means the option is being edited.
        display.fillRect(
            0, y - 2, 122, rowHeight, SSD1306_WHITE
        );
        display.setTextColor(SSD1306_BLACK);
    } else {
        display.setTextColor(SSD1306_WHITE);

        if (focused) {
            // Outline means focused, but not being edited.
            display.drawRoundRect(
                0, y - 2, 122, rowHeight, 2, SSD1306_WHITE
            );
        }
    }

    display.setTextSize(1);
    display.setCursor(5, y);
    display.print(item.name);

    if (item.type == MenuItemType::Toggle && item.toggleValue) {
        display.setCursor(96, y);
        display.print(*item.toggleValue ? "ON" : "OFF");
    }

    if (item.type == MenuItemType::Value && item.value) {
        display.setCursor(100, y);
        display.print(*item.value);
    }

    if (item.type == MenuItemType::Choice &&
        item.value &&
        item.choices &&
        *item.value >= 0 &&
        *item.value < item.choiceCount) {

        display.setCursor(76, y);
        display.print(item.choices[*item.value]);
    }

    if (item.type == MenuItemType::Submenu) {
        display.setCursor(117, y);
        display.print(">");
    }
}




void Menu::drawMainGrid()
{
    display.clearDisplay();
    display.setTextSize(1);

    const int16_t centerX[4] = {32, 96, 32, 96};
    const int16_t topY[4] = {0, 0, 32, 32};

    if (itemCount == 0) {
        display.display();
        return;
    }

    uint8_t pageStart = (selected / 4) * 4;
    uint8_t pageCount = min(
        static_cast<uint8_t>(itemCount - pageStart),
        static_cast<uint8_t>(4)
    );

    for (uint8_t cell = 0; cell < pageCount; cell++) {
        uint8_t i = pageStart + cell;
        MenuItem &item = items[i];

        int16_t x = centerX[cell];
        int16_t y = topY[cell];
        bool focused = (i == selected);

        if (focused) {
            display.fillRoundRect(
                (cell % 2) * 64 + 2,
                y + 1,
                60,
                30,
                3,
                SSD1306_WHITE
            );
        }

        if (item.icon) {
            display.drawBitmap(
                x - 8,
                y + 2,
                item.icon,
                16,
                16,
                focused ? SSD1306_BLACK : SSD1306_WHITE
            );
        }

        display.setTextColor(
            focused ? SSD1306_BLACK : SSD1306_WHITE
        );

        int16_t x1, y1;
        uint16_t w, h;

        display.getTextBounds(
            item.name, 0, 0, &x1, &y1, &w, &h
        );

        display.setCursor(x - (w / 2) - x1, y + 20);
        display.print(item.name);
    }

    display.display();
}




void Menu::resetEditMode()
{
    editing = false;
}

void Menu::draw()
{
    if (layout == MenuLayout::IconGrid) {
        drawMainGrid();
        return;
    }

    display.clearDisplay();

    uint8_t visibleItems = getVisibleItems();

    for (uint8_t i = 0; i < visibleItems; i++) {
        uint8_t itemIndex = scrollOffset + i;

        if (itemIndex >= itemCount) {
            break;
        }

        drawItem(itemIndex);
    }

    // Existing scrollbar, retained for list menus.
    if (itemCount > visibleItems) {
        const int x = 124;
        const int top = 1;
        const int height = 62;

        display.drawRoundRect(
            x, top, 3, height, 1, SSD1306_WHITE
        );

        int thumbHeight = max(
            6,
            (height * visibleItems) / itemCount
        );

        int maxThumbY = height - thumbHeight;
        int maxScroll = itemCount - visibleItems;
        int thumbY = top;

        if (maxScroll > 0) {
            thumbY = top +
                (maxThumbY * scrollOffset) / maxScroll;
        }

        display.fillRoundRect(
            x, thumbY, 3, thumbHeight, 1, SSD1306_WHITE
        );
    }

    display.display();
}
