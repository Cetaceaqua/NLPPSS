#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ui
{
    enum KeyboardMode
    {
        KBD_MODE_PINYIN = 0,
        KBD_MODE_KANA   = 1,
        KBD_MODE_EN     = 2,
        KBD_MODE_SYM    = 3
    };

    // Shows virtual keyboard on bottom screen.
    // hint: prompt message displayed on title bar
    // initialText: starting text
    // maxBytes: max UTF-8 byte capacity (e.g. 12 for player names)
    // outText: output string if confirmed
    // drawTop: optional callback to render original top screen content
    // initialMode: keyboard start mode (default: KBD_MODE_PINYIN)
    // Returns true if confirmed with OK, false if cancelled with B/Cancel
    bool showKeyboard(const std::string &hint, const std::string &initialText, size_t maxBytes, std::string &outText, void (*drawTop)() = nullptr, KeyboardMode initialMode = KBD_MODE_PINYIN);
}
