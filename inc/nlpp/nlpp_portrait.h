#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>
#include "nlpp_types.h"

namespace nlpp
{
    enum PortraitCategory
    {
        PORTRAIT_HAIRSTYLE,
        PORTRAIT_HAIRCOLOR
    };

    struct HairStyleOption
    {
        uint8_t styleId;
        std::string displayName; // e.g. "基本 (カジュアル)" without length
        int portraitIndex;       // 1-based index (e.g. 1..45)
    };

    struct HairColorOption
    {
        uint8_t colorId;         // 1..19
        std::string displayName; // e.g. "黒" without initial color marker
        int portraitIndex;       // 1..19
    };

    void portraitInit();
    void portraitExit();

    // Load portrait for specific heroine, category, and 1-based index
    bool portraitLoad(HeroineID heroine, PortraitCategory category, int index);

    // Load portrait directly for heroine, hair length stage, and styleId
    bool portraitLoadByStyle(HeroineID heroine, uint8_t hairLength, uint8_t styleId);

    // Load portrait directly for heroine and hair colorId (1..19)
    bool portraitLoadByColor(HeroineID heroine, uint8_t colorId);

    // Draw currently loaded portrait at screen coordinates (x, y)
    void portraitDraw(float x, float y);

    // Whether a valid portrait is loaded and ready to draw
    bool portraitIsLoaded();

    // Get list of hairstyle options for heroine and length stage (0: Short, 1/2: Medium, 2/3: Long)
    std::vector<HairStyleOption> getStyleOptions(HeroineID heroine, uint8_t hairLength);

    // Get list of all 19 hair color options for heroine
    std::vector<HairColorOption> getColorOptions(HeroineID heroine);

    // Get clean display name for styleId under current length
    std::string getCleanStyleName(HeroineID heroine, uint8_t hairLength, uint8_t styleId);

    // Get clean display name for colorId (1..19)
    std::string getCleanColorName(HeroineID heroine, uint8_t colorId);
}
