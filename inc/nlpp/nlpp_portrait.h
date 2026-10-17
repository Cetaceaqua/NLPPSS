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

    // Dual texture loaders
    bool portraitLoadByStyle(HeroineID heroine, uint8_t hairLength, uint8_t styleId);
    bool portraitLoadByColor(HeroineID heroine, uint8_t colorId);

    // Submenu preview loaders by 1-based index (1..45 for style, 1..19 for color)
    bool portraitLoadStyleIndex(HeroineID heroine, int index);
    bool portraitLoadColorIndex(HeroineID heroine, int index);

    // Draw functions for upper screen
    void portraitDrawStyle(float x, float y, float scaleX = 1.0f, float scaleY = 1.0f);
    void portraitDrawColor(float x, float y, float scaleX = 1.0f, float scaleY = 1.0f);

    // Status queries
    bool portraitIsStyleLoaded();
    bool portraitIsColorLoaded();

    // Backward compatibility functions
    bool portraitLoad(HeroineID heroine, PortraitCategory category, int index);
    void portraitDraw(float x, float y);
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
