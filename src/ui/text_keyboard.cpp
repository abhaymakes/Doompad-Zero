#include "text_keyboard.h"
#include "input.h"

namespace TextKeyboard {

    enum class Mode : uint8_t {
        Letters,
        Numbers,
        Symbols
    };

    static Adafruit_SSD1306* screen = nullptr;
    static String title;
    static String text;
    static size_t textLimit = 63;

    static Mode mode = Mode::Letters;
    static bool uppercase = false;
    static bool open = false;

    // Rows are eight cells wide.
    static const char* letterRows[] = {
        "abcdefgh",
        "ijklmnop",
        "qrstuvwx",
        "yz      "
    };

    static const char* numberRows[] = {
        "12345678",
        "90      ",
        "@#$%&*!?",
        "-_+=()/ "
    };

    static const char* symbolRows[] = {
        "!@#$%^&*",
        "()[]{}<>",
        "-_+=?/\\:",
        ";'\".,`~ "
    };

    // row 4 is the action bar:
    // ABC, 123, SYM, SPC, DEL, GO

    static uint8_t selectedRow = 0;
    static uint8_t selectedCol = 0;

    static const char* currentRows[4] = {
        nullptr, nullptr, nullptr, nullptr
    };

    static void refreshRows() {
        if (mode == Mode::Letters) {
            currentRows[0] = letterRows[0];
            currentRows[1] = letterRows[1];
            currentRows[2] = letterRows[2];
            currentRows[3] = letterRows[3];
        } else if (mode == Mode::Numbers) {
            currentRows[0] = numberRows[0];
            currentRows[1] = numberRows[1];
            currentRows[2] = numberRows[2];
            currentRows[3] = numberRows[3];
        } else {
            currentRows[0] = symbolRows[0];
            currentRows[1] = symbolRows[1];
            currentRows[2] = symbolRows[2];
            currentRows[3] = symbolRows[3];
        }
    }

    static void insertChar(char c) {
        if (text.length() < textLimit) {
            text += c;
        }
    }

    static void deleteChar() {
        if (!text.isEmpty()) {
            text.remove(text.length() - 1);
        }
    }

    static void activateSelected() {
        if (selectedRow < 4) {
            refreshRows();

            char c = currentRows[selectedRow][selectedCol];

            if (c != ' ' && c != '\0') {
                if (mode == Mode::Letters && uppercase) {
                    c = static_cast<char>(c - 'a' + 'A');
                }

                insertChar(c);
            }

            return;
        }

        switch (selectedCol) {
            case 0:
                if (mode != Mode::Letters) {
                    mode = Mode::Letters;
                    uppercase = false;
                } else {
                    uppercase = !uppercase;
                }
                break;

            case 1:
                mode = Mode::Numbers;
                break;

            case 2:
                mode = Mode::Symbols;
                break;

            case 3:
                insertChar(' ');
                break;

            case 4:
                deleteChar();
                break;

            case 5:
                open = false;
                break;
        }
    }

    void begin(
        Adafruit_SSD1306& display,
        const char* keyboardTitle,
        const String& initialText,
        size_t maxLength
    ) {
        screen = &display;
        title = keyboardTitle ? keyboardTitle : "Enter text";

        textLimit = maxLength;
        text = initialText.substring(0, textLimit);

        mode = Mode::Letters;
        uppercase = false;

        selectedRow = 0;
        selectedCol = 0;

        open = true;
        refreshRows();
        draw();
    }

    Result update() {
        if (!open) {
            return Result::Accepted;
        }

        if (Input::menuBack()) {
            open = false;
            return Result::Cancelled;
        }

        if (Input::left()) {
            if (selectedRow == 4) {
                if (selectedCol > 0) {
                    --selectedCol;
                } else {
                    selectedRow = 3;
                    selectedCol = 7;
                }
            } else if (selectedCol > 0) {
                --selectedCol;
            }

            draw();
            return Result::Editing;
        }

        if (Input::right()) {
            if (selectedRow == 4) {
                if (selectedCol < 5) {
                    ++selectedCol;
                } else {
                    selectedRow = 0;
                    selectedCol = 0;
                }
            } else if (selectedCol < 7) {
                ++selectedCol;
            }

            draw();
            return Result::Editing;
        }

        if (Input::up()) {
            if (selectedRow > 0) {
                --selectedRow;
                if (selectedRow == 4 && selectedCol > 5) {
                    selectedCol = 5;
                }
            } else {
                selectedRow = 4;
                if (selectedCol > 5) {
                    selectedCol = 5;
                }
            }

            draw();
            return Result::Editing;
        }

        if (Input::down()) {
            if (selectedRow < 4) {
                ++selectedRow;
                if (selectedRow == 4 && selectedCol > 5) {
                    selectedCol = 5;
                }
            } else {
                selectedRow = 0;
            }

            draw();
            return Result::Editing;
        }

        if (Input::select()) {
            activateSelected();

            if (!open) {
                return Result::Accepted;
            }

            draw();
        }

        return Result::Editing;
    }

    void draw() {
        if (!screen || !open) {
            return;
        }

        screen->clearDisplay();
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);

        // Title and input field.
        screen->setCursor(0, 0);
        screen->print(title.substring(0, 21));

        screen->drawRect(0, 9, 128, 10, SSD1306_WHITE);

        // Show the end of long text so recent input remains visible.
        const size_t visibleChars = 20;
        String shown = text;

        if (shown.length() > visibleChars) {
            shown = shown.substring(shown.length() - visibleChars);
        }

        screen->setCursor(2, 10);
        screen->print(shown);

        // Four character rows.
        refreshRows();

        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 8; ++col) {
                const int16_t x = col * 16;
                const int16_t y = 20 + row * 8;

                const bool selected =
                    selectedRow == row && selectedCol == col;

                if (selected) {
                    screen->fillRect(x, y, 15, 8, SSD1306_WHITE);
                    screen->setTextColor(SSD1306_BLACK);
                } else {
                    screen->setTextColor(SSD1306_WHITE);
                }

                char c = currentRows[row][col];

                if (c != ' ' && c != '\0') {
                    if (mode == Mode::Letters && uppercase) {
                        c = static_cast<char>(c - 'a' + 'A');
                    }

                    screen->setCursor(x + 4, y);
                    screen->write(c);
                }

                screen->setTextColor(SSD1306_WHITE);
            }
        }

        // Action bar: six selectable keys.
        const char* actions[] = {
            mode == Mode::Letters
                ? (uppercase ? "abc" : "ABC")
                : "ABC",
            "123", "SYM", "SPC", "DEL", "GO"
        };

        for (uint8_t col = 0; col < 6; ++col) {
            const int16_t x = col * 21;
            const int16_t width = (col == 5) ? 23 : 21;
            const bool selected =
                selectedRow == 4 && selectedCol == col;

            if (selected) {
                screen->fillRect(x, 54, width, 10, SSD1306_WHITE);
                screen->setTextColor(SSD1306_BLACK);
            } else {
                screen->setTextColor(SSD1306_WHITE);
            }

            screen->setCursor(x + 2, 55);
            screen->print(actions[col]);
            screen->setTextColor(SSD1306_WHITE);
        }

        screen->display();
    }

    const String& value() {
        return text;
    }

    void setValue(const String& newText) {
        text = newText.substring(0, textLimit);
        draw();
    }

    bool isOpen() {
        return open;
    }

}