#include "nlpp_portrait.h"
#include "lodepng.h"
#include "gfx.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

namespace nlpp
{
    static C3D_Tex s_portraitTex;
    static Tex3DS_SubTexture s_portraitSubtex;
    static bool s_texInitialized = false;
    static bool s_portraitLoaded = false;

    // Track currently loaded image to avoid redundant decodes
    static HeroineID s_curHeroine = HEROINE_NONE;
    static PortraitCategory s_curCat = PORTRAIT_HAIRSTYLE;
    static int s_curIndex = -1;

    static const char *getHeroineDir(HeroineID heroine)
    {
        switch (heroine)
        {
            case HEROINE_MANAKA: return "manaka";
            case HEROINE_RINKO:  return "rinko";
            case HEROINE_NENE:   return "nene";
            default: return "manaka";
        }
    }

    void portraitInit()
    {
        if (s_texInitialized) return;

        // Allocate a 256x256 GPU texture for 146x170 portraits
        C3D_TexInit(&s_portraitTex, 256, 256, GPU_RGBA8);
        C3D_TexSetFilter(&s_portraitTex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&s_portraitTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

        // Clear texture to transparent
        if (s_portraitTex.data)
        {
            memset(s_portraitTex.data, 0, 256 * 256 * 4);
            GSPGPU_FlushDataCache(s_portraitTex.data, 256 * 256 * 4);
        }

        // Subtexture: In Citro2D, top > bottom indicates non-rotated orientation.
        // top corresponds to v=170/256, bottom corresponds to v=0.
        s_portraitSubtex.width = 146;
        s_portraitSubtex.height = 170;
        s_portraitSubtex.left = 0.0f;
        s_portraitSubtex.top = 170.0f / 256.0f;
        s_portraitSubtex.right = 146.0f / 256.0f;
        s_portraitSubtex.bottom = 0.0f;

        s_texInitialized = true;
        s_portraitLoaded = false;
        s_curHeroine = HEROINE_NONE;
        s_curIndex = -1;
    }

    void portraitExit()
    {
        if (s_texInitialized)
        {
            C3D_TexDelete(&s_portraitTex);
            s_texInitialized = false;
            s_portraitLoaded = false;
        }
    }

    static inline uint32_t morton2(uint32_t x, uint32_t y)
    {
        return (x & 1) | ((y & 1) << 1) |
               ((x & 2) << 1) | ((y & 2) << 2) |
               ((x & 4) << 2) | ((y & 4) << 3);
    }

    bool portraitLoad(HeroineID heroine, PortraitCategory category, int index)
    {
        if (!s_texInitialized) portraitInit();
        if (heroine < 1 || heroine > 3 || index < 1 || index > 45) return false;

        // If already loaded, do nothing
        if (s_portraitLoaded && s_curHeroine == heroine && s_curCat == category && s_curIndex == index)
        {
            return true;
        }

        char path[128];
        if (category == PORTRAIT_HAIRSTYLE)
        {
            snprintf(path, sizeof(path), "romfs:/portraits/%s/hairstyles/h_%02d.png", getHeroineDir(heroine), index);
        }
        else
        {
            snprintf(path, sizeof(path), "romfs:/portraits/%s/haircolors/c_%02d.png", getHeroineDir(heroine), index);
        }

        std::vector<unsigned char> pngImage;
        unsigned int width = 0, height = 0;
        unsigned int error = lodepng::decode(pngImage, width, height, path);
        if (error != 0 || width == 0 || height == 0)
        {
            // Fallback for local PC / relative tests
            if (category == PORTRAIT_HAIRSTYLE)
                snprintf(path, sizeof(path), "romfs/portraits/%s/hairstyles/h_%02d.png", getHeroineDir(heroine), index);
            else
                snprintf(path, sizeof(path), "romfs/portraits/%s/haircolors/c_%02d.png", getHeroineDir(heroine), index);
            error = lodepng::decode(pngImage, width, height, path);
            if (error != 0 || width == 0 || height == 0) return false;
        }

        uint32_t *dstPixels = (uint32_t *)s_portraitTex.data;
        if (!dstPixels) return false;

        // Tile RGBA pixels into 3DS PICA200 8x8 Morton order.
        // Invert Y so gy = (170 - 1) - py matches top = 170/256, bottom = 0.
        int copyW = std::min((int)width, 146);
        int copyH = std::min((int)height, 170);

        for (int py = 0; py < copyH; ++py)
        {
            int gy = (170 - 1) - py;
            int tileY = gy >> 3;
            int ly = gy & 7;

            for (int px = 0; px < copyW; ++px)
            {
                int gx = px;
                int tileX = gx >> 3;
                int lx = gx & 7;

                int tileIdx = (tileY << 5) + tileX; // 32 tiles per row (256/8)
                int dstIdx = (tileIdx << 6) + morton2(lx, ly);

                size_t srcIdx = ((size_t)py * width + px) * 4;
                uint8_t r = pngImage[srcIdx + 0];
                uint8_t g = pngImage[srcIdx + 1];
                uint8_t b = pngImage[srcIdx + 2];
                uint8_t a = pngImage[srcIdx + 3];

                dstPixels[dstIdx] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
            }
        }

        // Flush cache to ensure PICA200 sees the new data
        GSPGPU_FlushDataCache(s_portraitTex.data, 256 * 256 * 4);

        s_curHeroine = heroine;
        s_curCat = category;
        s_curIndex = index;
        s_portraitLoaded = true;
        return true;
    }

    void portraitDraw(float x, float y)
    {
        if (!s_texInitialized || !s_portraitLoaded) return;

        C2D_Image img;
        img.tex = &s_portraitTex;
        img.subtex = &s_portraitSubtex;

        C2D_DrawImageAt(img, x, y, GFX_DEPTH_DEFAULT, nullptr, 1.0f, 1.0f);
    }

    bool portraitIsLoaded()
    {
        return s_portraitLoaded;
    }

    // -------------------------------------------------------------------------
    // Option Listing and Name Resolution
    // -------------------------------------------------------------------------

    static const struct { uint8_t id; const char *name; } s_styleNames[] = {
        {0,  "基本 (カジュアル)"},
        {1,  "自宅 (カジュアル)"},
        {2,  "かわいいＡ"},
        {14, "かわいいＢ"},
        {17, "かわいいＣ"},
        {3,  "カジュアルＡ"},
        {15, "カジュアルＢ"},
        {18, "カジュアルＣ"},
        {4,  "清楚Ａ"},
        {16, "清楚Ｂ"},
        {19, "清楚Ｃ"},
        {5,  "アップ [水着・着物]"},
        {6,  "パーティー"},
        {7,  "ライブ"},
        {8,  "ゴスロリ"},
        {9,  "ナチュラル"},
        {20, "NEW"}
    };

    static const char *s_colorNames[19] = {
        "黒",
        "明るい／黄色っぽいＡ",
        "明るい／黄色っぽいＢ",
        "明るい／赤っぽいＡ",
        "明るい／赤っぽいＢ",
        "明るい／黒っぽいＡ",
        "明るい／黒っぽいＢ",
        "自然な茶系／黄色っぽいＡ",
        "自然な茶系／黄色っぽいＢ",
        "自然な茶系／赤っぽいＡ",
        "自然な茶系／赤っぽいＢ",
        "自然な茶系／黒っぽいＡ",
        "自然な茶系／黒っぽいＢ",
        "特殊カラー・ピンク",
        "特殊カラー・ブルー",
        "特殊カラー・ライトグリーン",
        "特殊カラー・パープル",
        "特殊カラー・オレンジ",
        "特殊カラー・グリーン"
    };

    // Determine hair length category: 0: Short, 1: Medium, 2: Long
    static int getLengthCategory(HeroineID heroine, uint8_t hairLength)
    {
        if (heroine == HEROINE_MANAKA)
        {
            if (hairLength == 3) return 2; // Long
            if (hairLength == 2) return 1; // Medium
            return 0; // Short
        }
        else if (heroine == HEROINE_RINKO)
        {
            if (hairLength == 3) return 2; // Long
            if (hairLength == 1) return 1; // Medium / Bob
            return 0; // Short
        }
        else // Nene
        {
            if (hairLength == 2) return 2; // Long
            if (hairLength == 1) return 1; // Medium / Bob
            return 0; // Short
        }
    }

    std::vector<HairStyleOption> getStyleOptions(HeroineID heroine, uint8_t hairLength)
    {
        std::vector<HairStyleOption> opts;
        int lenCat = getLengthCategory(heroine, hairLength);

        // Standard 14 styles present in all lengths
        static const uint8_t base14[] = { 0, 1, 2, 14, 17, 3, 15, 18, 4, 16, 19, 5, 6, 20 };

        if (lenCat == 2) // Long
        {
            static const uint8_t longManaka[] = { 0, 1, 2, 14, 17, 3, 15, 18, 4, 16, 19, 5, 6, 7, 8, 9, 20 };
            static const uint8_t longOther[]  = { 0, 1, 2, 14, 17, 3, 15, 18, 4, 16, 19, 5, 6, 7, 8, 20 };

            const uint8_t *arr = (heroine == HEROINE_MANAKA) ? longManaka : longOther;
            int count = (heroine == HEROINE_MANAKA) ? 17 : 16;

            for (int i = 0; i < count; ++i)
            {
                HairStyleOption opt;
                opt.styleId = arr[i];
                opt.portraitIndex = i + 1; // 1-based index in Long section
                for (const auto &item : s_styleNames)
                {
                    if (item.id == opt.styleId) { opt.displayName = item.name; break; }
                }
                opts.push_back(opt);
            }
        }
        else if (lenCat == 1) // Medium
        {
            int baseIndex = (heroine == HEROINE_MANAKA) ? 18 : 17;
            for (int i = 0; i < 14; ++i)
            {
                HairStyleOption opt;
                opt.styleId = base14[i];
                opt.portraitIndex = baseIndex + i;
                for (const auto &item : s_styleNames)
                {
                    if (item.id == opt.styleId) { opt.displayName = item.name; break; }
                }
                opts.push_back(opt);
            }
        }
        else // Short
        {
            int baseIndex = (heroine == HEROINE_MANAKA) ? 32 : 31;
            for (int i = 0; i < 14; ++i)
            {
                HairStyleOption opt;
                opt.styleId = base14[i];
                opt.portraitIndex = baseIndex + i;
                for (const auto &item : s_styleNames)
                {
                    if (item.id == opt.styleId) { opt.displayName = item.name; break; }
                }
                opts.push_back(opt);
            }
        }

        return opts;
    }

    std::vector<HairColorOption> getColorOptions(HeroineID heroine)
    {
        std::vector<HairColorOption> opts;
        opts.reserve(19);

        for (int i = 0; i < 19; ++i)
        {
            HairColorOption opt;
            opt.colorId = i + 1;
            opt.portraitIndex = i + 1;
            opt.displayName = s_colorNames[i];
            opts.push_back(opt);
        }
        return opts;
    }

    bool portraitLoadByStyle(HeroineID heroine, uint8_t hairLength, uint8_t styleId)
    {
        auto styles = getStyleOptions(heroine, hairLength);
        for (const auto &s : styles)
        {
            if (s.styleId == styleId)
            {
                return portraitLoad(heroine, PORTRAIT_HAIRSTYLE, s.portraitIndex);
            }
        }
        if (!styles.empty())
        {
            return portraitLoad(heroine, PORTRAIT_HAIRSTYLE, styles[0].portraitIndex);
        }
        return false;
    }

    bool portraitLoadByColor(HeroineID heroine, uint8_t colorId)
    {
        if (colorId < 1 || colorId > 19) colorId = 1;
        return portraitLoad(heroine, PORTRAIT_HAIRCOLOR, colorId);
    }

    std::string getCleanStyleName(HeroineID heroine, uint8_t hairLength, uint8_t styleId)
    {
        for (const auto &item : s_styleNames)
        {
            if (item.id == styleId) return item.name;
        }
        return "基本 (カジュアル)";
    }

    std::string getCleanColorName(HeroineID heroine, uint8_t colorId)
    {
        if (colorId >= 1 && colorId <= 19)
        {
            return s_colorNames[colorId - 1];
        }
        return "黒";
    }
}
