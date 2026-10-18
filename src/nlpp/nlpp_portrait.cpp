#include "nlpp_portrait.h"
#include "lodepng.h"
#include "gfx.h"
#include "ui.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

namespace nlpp
{
    static C3D_Tex s_styleTex;
    static Tex3DS_SubTexture s_styleSubtex;
    static bool s_styleLoaded = false;
    static HeroineID s_curStyleHeroine = HEROINE_NONE;
    static int s_curStyleIndex = -1;

    static C3D_Tex s_colorTex;
    static Tex3DS_SubTexture s_colorSubtex;
    static bool s_colorLoaded = false;
    static HeroineID s_curColorHeroine = HEROINE_NONE;
    static int s_curColorIndex = -1;

    static bool s_texInitialized = false;

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

        // Texture 1: Hairstyle (256x256 GPU_RGBA8)
        C3D_TexInit(&s_styleTex, 256, 256, GPU_RGBA8);
        C3D_TexSetFilter(&s_styleTex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&s_styleTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        if (s_styleTex.data)
        {
            memset(s_styleTex.data, 0, 256 * 256 * 4);
            GSPGPU_FlushDataCache(s_styleTex.data, 256 * 256 * 4);
        }

        s_styleSubtex.width = 146;
        s_styleSubtex.height = 170;
        s_styleSubtex.left = 0.0f;
        s_styleSubtex.top = 1.0f;
        s_styleSubtex.right = 146.0f / 256.0f;
        s_styleSubtex.bottom = 1.0f - (170.0f / 256.0f);

        // Texture 2: Haircolor (256x256 GPU_RGBA8)
        C3D_TexInit(&s_colorTex, 256, 256, GPU_RGBA8);
        C3D_TexSetFilter(&s_colorTex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&s_colorTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        if (s_colorTex.data)
        {
            memset(s_colorTex.data, 0, 256 * 256 * 4);
            GSPGPU_FlushDataCache(s_colorTex.data, 256 * 256 * 4);
        }

        s_colorSubtex.width = 146;
        s_colorSubtex.height = 170;
        s_colorSubtex.left = 0.0f;
        s_colorSubtex.top = 1.0f;
        s_colorSubtex.right = 146.0f / 256.0f;
        s_colorSubtex.bottom = 1.0f - (170.0f / 256.0f);

        s_texInitialized = true;
        s_styleLoaded = false;
        s_colorLoaded = false;
        s_curStyleHeroine = HEROINE_NONE;
        s_curStyleIndex = -1;
        s_curColorHeroine = HEROINE_NONE;
        s_curColorIndex = -1;
    }

    void portraitExit()
    {
        if (s_texInitialized)
        {
            C3D_TexDelete(&s_styleTex);
            C3D_TexDelete(&s_colorTex);
            s_texInitialized = false;
            s_styleLoaded = false;
            s_colorLoaded = false;
        }
    }

    static inline uint32_t morton2(uint32_t x, uint32_t y)
    {
        return (x & 1) | ((y & 1) << 1) |
               ((x & 2) << 1) | ((y & 2) << 2) |
               ((x & 4) << 2) | ((y & 4) << 3);
    }

    static bool decodeAndTilePng(const char *relPath, C3D_Tex &tex)
    {
        char fullPath[128];
        snprintf(fullPath, sizeof(fullPath), "romfs:/%s", relPath);

        std::vector<unsigned char> pngImage;
        unsigned int width = 0, height = 0;
        unsigned int error = lodepng::decode(pngImage, width, height, fullPath);
        if (error != 0 || width == 0 || height == 0)
        {
            // Fallback for local PC / tests
            snprintf(fullPath, sizeof(fullPath), "romfs/%s", relPath);
            error = lodepng::decode(pngImage, width, height, fullPath);
            if (error != 0 || width == 0 || height == 0) return false;
        }

        uint32_t *dstPixels = (uint32_t *)tex.data;
        if (!dstPixels) return false;

        memset(dstPixels, 0, 256 * 256 * 4);

        int copyW = std::min((int)width, 146);
        int copyH = std::min((int)height, 170);

        for (int py = 0; py < copyH; ++py)
        {
            int gy = py; // Direct mapping: top row py=0 goes to gy=0
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

                // 3DS PICA200 GPU_RGBA8 little-endian layout: Byte0=A, Byte1=B, Byte2=G, Byte3=R
                dstPixels[dstIdx] = (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)g << 16) | ((uint32_t)r << 24);
            }
        }

        GSPGPU_FlushDataCache(tex.data, 256 * 256 * 4);
        return true;
    }

    bool portraitLoadStyleIndex(HeroineID heroine, int index)
    {
        if (!s_texInitialized) portraitInit();
        if ((int)heroine < 0 || (int)heroine > 2 || index < 1 || index > 45) return false;

        if (s_styleLoaded && s_curStyleHeroine == heroine && s_curStyleIndex == index)
        {
            return true;
        }

        char relPath[128];
        snprintf(relPath, sizeof(relPath), "portraits/%s/hairstyles/h_%02d.png", getHeroineDir(heroine), index);

        if (!decodeAndTilePng(relPath, s_styleTex)) return false;

        s_curStyleHeroine = heroine;
        s_curStyleIndex = index;
        s_styleLoaded = true;
        return true;
    }

    bool portraitLoadColorIndex(HeroineID heroine, int index)
    {
        if (!s_texInitialized) portraitInit();
        if ((int)heroine < 0 || (int)heroine > 2 || index < 1 || index > 19) return false;

        if (s_colorLoaded && s_curColorHeroine == heroine && s_curColorIndex == index)
        {
            return true;
        }

        char relPath[128];
        snprintf(relPath, sizeof(relPath), "portraits/%s/haircolors/c_%02d.png", getHeroineDir(heroine), index);

        if (!decodeAndTilePng(relPath, s_colorTex)) return false;

        s_curColorHeroine = heroine;
        s_curColorIndex = index;
        s_colorLoaded = true;
        return true;
    }

    void portraitDrawStyle(float x, float y, float scaleX, float scaleY)
    {
        if (!s_texInitialized || !s_styleLoaded) return;
        C2D_Image img;
        img.tex = &s_styleTex;
        img.subtex = &s_styleSubtex;
        C2D_DrawImageAt(img, x, y, GFX_DEPTH_DEFAULT, nullptr, scaleX, scaleY);
    }

    void portraitDrawColor(float x, float y, float scaleX, float scaleY)
    {
        if (!s_texInitialized || !s_colorLoaded) return;
        C2D_Image img;
        img.tex = &s_colorTex;
        img.subtex = &s_colorSubtex;
        C2D_DrawImageAt(img, x, y, GFX_DEPTH_DEFAULT, nullptr, scaleX, scaleY);
    }

    bool portraitIsStyleLoaded()
    {
        return s_styleLoaded;
    }

    bool portraitIsColorLoaded()
    {
        return s_colorLoaded;
    }

    bool portraitLoad(HeroineID heroine, PortraitCategory category, int index)
    {
        if (category == PORTRAIT_HAIRSTYLE)
            return portraitLoadStyleIndex(heroine, index);
        else
            return portraitLoadColorIndex(heroine, index);
    }

    void portraitDraw(float x, float y)
    {
        portraitDrawStyle(x, y);
    }

    bool portraitIsLoaded()
    {
        return s_styleLoaded || s_colorLoaded;
    }

    // -------------------------------------------------------------------------
    // Option Listing and Name Resolution
    // -------------------------------------------------------------------------

    static const struct { uint8_t id; const char *name; } s_styleNames[] = {
        {0,  "基本 (休闲)"},
        {1,  "居家 (休闲)"},
        {2,  "可爱 A"},
        {14, "可爱 B"},
        {17, "可爱 C"},
        {3,  "休闲 A"},
        {15, "休闲 B"},
        {18, "休闲 C"},
        {4,  "清纯 A"},
        {16, "清纯 B"},
        {19, "清纯 C"},
        {5,  "盘发 [泳装·和服] (清纯)"},
        {6,  "派对 (清纯)"},
        {7,  "演唱会 (可爱)"},
        {8,  "哥特萝莉 (可爱)"},
        {9,  "自然 (清纯)"},
        {20, "NEW (可爱)"}
    };

    static const char *s_colorNames[19] = {
        "黑色",
        "明亮色／偏黄 A",
        "明亮色／偏黄 B",
        "明亮色／偏红 A",
        "明亮色／偏红 B",
        "明亮色／偏黑 A",
        "明亮色／偏黑 B",
        "自然茶色／偏黄 A",
        "自然茶色／偏黄 B",
        "自然茶色／偏红 A",
        "自然茶色／偏红 B",
        "自然茶色／偏黑 A",
        "自然茶色／偏黑 B",
        "特殊色／粉红",
        "特殊色／蓝色",
        "特殊色／浅绿",
        "特殊色／紫色",
        "特殊色／橙色",
        "特殊色／绿色"
    };

    // Determine hair length category: 0: Short, 1: Medium, 2: Long
    // In physical savedata18[0x466]: 2 is Long, 1 is Medium, 0 is Short for all heroines
    static int getLengthCategory(HeroineID heroine, uint8_t hairLength)
    {
        (void)heroine;
        if (hairLength >= 2) return 2; // Long
        if (hairLength == 1) return 1; // Medium
        return 0; // Short
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
                    if (item.id == opt.styleId) { opt.displayName = getTxt(item.name); break; }
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
                    if (item.id == opt.styleId) { opt.displayName = getTxt(item.name); break; }
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
                    if (item.id == opt.styleId) { opt.displayName = getTxt(item.name); break; }
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
            opt.displayName = getTxt(s_colorNames[i]);
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
                return portraitLoadStyleIndex(heroine, s.portraitIndex);
            }
        }
        if (!styles.empty())
        {
            return portraitLoadStyleIndex(heroine, styles[0].portraitIndex);
        }
        return false;
    }

    bool portraitLoadByColor(HeroineID heroine, uint8_t colorId)
    {
        if (colorId < 1 || colorId > 19)
        {
            // Default color from Section 7.2.5:
            // Manaka: 黑 (Color 1)
            // Rinko: 自然な茶系／黒っぽいＢ (Color 13)
            // Nene: 自然な茶系／黄色っぽいＢ (Color 9)
            if (heroine == HEROINE_RINKO) colorId = 13;
            else if (heroine == HEROINE_NENE) colorId = 9;
            else colorId = 1;
        }
        return portraitLoadColorIndex(heroine, colorId);
    }

    std::string getCleanStyleName(HeroineID heroine, uint8_t hairLength, uint8_t styleId)
    {
        for (const auto &item : s_styleNames)
        {
            if (item.id == styleId) return getTxt(item.name);
        }
        return getTxt("基本 (休闲)");
    }

    std::string getCleanColorName(HeroineID heroine, uint8_t colorId)
    {
        if (colorId >= 1 && colorId <= 19)
        {
            return getTxt(s_colorNames[colorId - 1]);
        }
        return getTxt("默认发色");
    }
}
