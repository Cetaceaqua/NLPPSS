#include "ui/keyboard.h"
#include "gfx.h"
#include "ui.h"
#include "cfg.h"
#include "3dsgettext.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>

namespace ui
{
    enum FocusArea
    {
        FOCUS_TOOLBAR = 0,
        FOCUS_GRID = 1,
        FOCUS_BOTTOM_BAR = 2
    };

    struct FocusPos
    {
        FocusArea area = FOCUS_GRID;
        int row = 0;
        int col = 0;
    };

    struct KeyItem
    {
        int id = 0;
        int x = 0, y = 0, w = 0, h = 0;
        std::string label;
        int candIndex = -1; // For pinyin candidates
        int centerX() const { return x + w / 2; }
        int centerY() const { return y + h / 2; }
    };

    struct PinyinEntry
    {
        char pinyin[8];
        uint32_t offset;
        uint16_t count;
        uint16_t byteLen;
    };

    static bool s_pinyinLoaded = false;
    static std::vector<PinyinEntry> s_pinyinIndex;
    static std::vector<uint8_t> s_pinyinStrings;

    static bool loadPinyinDict()
    {
        if (s_pinyinLoaded) return true;

        const char *candidates[] = {
            "romfs:/dict/pinyin.bin",
            "romfs/dict/pinyin.bin",
            "d:/Files/GitHub Desktop/Cetaceaqua/NLPPSS/romfs/dict/pinyin.bin"
        };

        FILE *f = nullptr;
        for (const char *path : candidates)
        {
            f = fopen(path, "rb");
            if (f) break;
        }

        if (!f) return false;

        char magic[4];
        uint16_t ver = 0, count = 0;
        if (fread(magic, 1, 4, f) != 4 ||
            fread(&ver, 2, 1, f) != 1 ||
            fread(&count, 2, 1, f) != 1)
        {
            fclose(f);
            return false;
        }

        if (memcmp(magic, "NLPY", 4) != 0 || count == 0)
        {
            fclose(f);
            return false;
        }

        s_pinyinIndex.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            if (fread(s_pinyinIndex[i].pinyin, 1, 8, f) != 8 ||
                fread(&s_pinyinIndex[i].offset, 4, 1, f) != 1 ||
                fread(&s_pinyinIndex[i].count, 2, 1, f) != 1 ||
                fread(&s_pinyinIndex[i].byteLen, 2, 1, f) != 1)
            {
                fclose(f);
                s_pinyinIndex.clear();
                return false;
            }
        }

        fseek(f, 0, SEEK_END);
        long totalSize = ftell(f);
        long strStart = 8 + count * 16;
        long strLen = totalSize - strStart;
        if (strLen > 0)
        {
            fseek(f, strStart, SEEK_SET);
            s_pinyinStrings.resize(strLen);
            fread(s_pinyinStrings.data(), 1, strLen, f);
        }

        fclose(f);
        s_pinyinLoaded = true;
        return true;
    }

    static inline bool isStringSupportedByFont(const std::string &str)
    {
        if (!::font || str.empty()) return true;
        const uint8_t *p = (const uint8_t *)str.data();
        size_t len = str.size();
        size_t i = 0;
        while (i < len)
        {
            size_t clen = 1;
            if ((p[i] & 0x80) == 0) clen = 1;
            else if ((p[i] & 0xE0) == 0xC0) clen = 2;
            else if ((p[i] & 0xF0) == 0xE0) clen = 3;
            else if ((p[i] & 0xF8) == 0xF0) clen = 4;
            if (i + clen > len) return false;

            uint32_t cp = 0;
            if (clen == 1) cp = p[i];
            else if (clen == 2) cp = ((p[i] & 0x1F) << 6) | (p[i + 1] & 0x3F);
            else if (clen == 3) cp = ((p[i] & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            else if (clen == 4) cp = ((p[i] & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);

            if (cp != ' ' && !gfx::fontHasChar(::font, cp))
                return false;
            i += clen;
        }
        return true;
    }

    static void getPinyinCandidates(const std::string &py, std::vector<std::string> &outCandidates)
    {
        outCandidates.clear();
        if (!loadPinyinDict() || py.empty()) return;

        std::string lowerPy = py;
        for (char &c : lowerPy) c = tolower((unsigned char)c);

        int left = 0, right = (int)s_pinyinIndex.size() - 1;
        int found = -1;
        while (left <= right)
        {
            int mid = left + (right - left) / 2;
            int cmp = strcmp(s_pinyinIndex[mid].pinyin, lowerPy.c_str());
            if (cmp == 0)
            {
                found = mid;
                break;
            }
            else if (cmp < 0) left = mid + 1;
            else right = mid - 1;
        }

        if (found >= 0)
        {
            const auto &entry = s_pinyinIndex[found];
            const uint8_t *ptr = s_pinyinStrings.data() + entry.offset;
            size_t bytesLeft = entry.byteLen;

            while (bytesLeft > 0)
            {
                size_t charLen = 1;
                if ((*ptr & 0x80) == 0) charLen = 1;
                else if ((*ptr & 0xE0) == 0xC0) charLen = 2;
                else if ((*ptr & 0xF0) == 0xE0) charLen = 3;
                else if ((*ptr & 0xF8) == 0xF0) charLen = 4;

                if (charLen > bytesLeft) break;
                std::string c((const char *)ptr, charLen);
                if (isStringSupportedByFont(c))
                {
                    outCandidates.push_back(c);
                }
                ptr += charLen;
                bytesLeft -= charLen;
            }
        }
        else
        {
            for (const auto &entry : s_pinyinIndex)
            {
                if (strncmp(entry.pinyin, lowerPy.c_str(), lowerPy.length()) == 0)
                {
                    const uint8_t *ptr = s_pinyinStrings.data() + entry.offset;
                    size_t bytesLeft = entry.byteLen;
                    int added = 0;
                    while (bytesLeft > 0 && added < 3)
                    {
                        size_t charLen = 1;
                        if ((*ptr & 0x80) == 0) charLen = 1;
                        else if ((*ptr & 0xE0) == 0xC0) charLen = 2;
                        else if ((*ptr & 0xF0) == 0xE0) charLen = 3;
                        else if ((*ptr & 0xF8) == 0xF0) charLen = 4;

                        if (charLen > bytesLeft) break;
                        std::string c((const char *)ptr, charLen);
                        if (isStringSupportedByFont(c) && std::find(outCandidates.begin(), outCandidates.end(), c) == outCandidates.end())
                        {
                            outCandidates.push_back(c);
                            added++;
                        }
                        ptr += charLen;
                        bytesLeft -= charLen;
                    }
                    if (outCandidates.size() >= 30) break;
                }
            }
        }
    }

    static void popUtf8Char(std::string &s)
    {
        if (s.empty()) return;
        while (!s.empty())
        {
            unsigned char b = (unsigned char)s.back();
            s.pop_back();
            if ((b & 0xC0) != 0x80) break;
        }
    }

    static std::string getLastUtf8Char(const std::string &s)
    {
        if (s.empty()) return "";
        size_t idx = s.length() - 1;
        while (idx > 0 && ((unsigned char)s[idx] & 0xC0) == 0x80)
        {
            idx--;
        }
        return s.substr(idx);
    }

    // Dakuten table
    static const char *s_dakFrom[] = {
        "か", "き", "く", "け", "こ",
        "さ", "し", "す", "せ", "そ",
        "た", "ち", "つ", "て", "と",
        "は", "ひ", "ふ", "へ", "ほ",
        "カ", "キ", "ク", "ケ", "コ",
        "サ", "シ", "ス", "セ", "ソ",
        "タ", "チ", "ツ", "テ", "ト",
        "ハ", "ヒ", "フ", "ヘ", "ホ"
    };
    static const char *s_dakTo[] = {
        "が", "ぎ", "ぐ", "げ", "ご",
        "ざ", "じ", "ず", "ぜ", "ぞ",
        "だ", "ぢ", "づ", "で", "ど",
        "ば", "び", "ぶ", "べ", "ぼ",
        "ガ", "ギ", "グ", "ゲ", "ゴ",
        "ザ", "ジ", "ズ", "ゼ", "ゾ",
        "ダ", "ヂ", "ヅ", "デ", "ド",
        "バ", "ビ", "ブ", "ベ", "ボ"
    };

    // Handakuten table
    static const char *s_hdakFrom[] = {
        "は", "ひ", "ふ", "へ", "ほ",
        "ば", "び", "ぶ", "べ", "ぼ",
        "ハ", "ヒ", "フ", "ヘ", "ホ",
        "バ", "ビ", "ブ", "ベ", "ボ"
    };
    static const char *s_hdakTo[] = {
        "ぱ", "ぴ", "ぷ", "ぺ", "ぽ",
        "ぱ", "ぴ", "ぷ", "ぺ", "ぽ",
        "パ", "ピ", "プ", "ペ", "ポ",
        "パ", "ピ", "プ", "ペ", "ポ"
    };

    // Small kana table
    static const char *s_smallFrom[] = {
        "あ", "い", "う", "え", "お", "つ", "や", "ゆ", "よ", "わ",
        "ア", "イ", "ウ", "エ", "オ", "ツ", "ヤ", "ユ", "ヨ", "ワ"
    };
    static const char *s_smallTo[] = {
        "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "っ", "ゃ", "ゅ", "ょ", "ゎ",
        "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ", "ャ", "ュ", "ョ", "ヮ"
    };

    static void applyKanaModifier(std::string &s, int type) // 0: dak, 1: hdak, 2: small
    {
        if (s.empty()) return;
        std::string last = getLastUtf8Char(s);
        std::string repl = last;

        if (type == 0) // Dakuten
        {
            for (size_t i = 0; i < sizeof(s_dakFrom) / sizeof(s_dakFrom[0]); ++i)
            {
                if (last == s_dakFrom[i]) { repl = s_dakTo[i]; break; }
                if (last == s_dakTo[i])   { repl = s_dakFrom[i]; break; }
            }
        }
        else if (type == 1) // Handakuten
        {
            for (size_t i = 0; i < sizeof(s_hdakFrom) / sizeof(s_hdakFrom[0]); ++i)
            {
                if (last == s_hdakFrom[i]) { repl = s_hdakTo[i]; break; }
                if (last == s_hdakTo[i])   { repl = s_hdakFrom[i]; break; }
            }
        }
        else if (type == 2) // Small kana
        {
            for (size_t i = 0; i < sizeof(s_smallFrom) / sizeof(s_smallFrom[0]); ++i)
            {
                if (last == s_smallFrom[i]) { repl = s_smallTo[i]; break; }
                if (last == s_smallTo[i])   { repl = s_smallFrom[i]; break; }
            }
        }

        if (repl != last)
        {
            popUtf8Char(s);
            s += repl;
        }
    }

    // Kana 50-sound table (10 cols x 5 rows)
    static const char *s_kanaHiragana[5][10] = {
        {"あ", "か", "さ", "た", "な", "は", "ま", "や", "ら", "わ"},
        {"い", "き", "し", "ち", "に", "ひ", "み", "",   "り", "を"},
        {"う", "く", "す", "つ", "ぬ", "ふ", "む", "ゆ", "る", "ん"},
        {"え", "け", "せ", "て", "ね", "へ", "め", "",   "れ", "ー"},
        {"お", "こ", "そ", "と", "の", "ほ", "も", "よ", "ろ", ""}
    };

    static const char *s_kanaKatakana[5][10] = {
        {"ア", "カ", "サ", "タ", "ナ", "ハ", "マ", "ヤ", "ラ", "ワ"},
        {"イ", "キ", "シ", "チ", "ニ", "ヒ", "ミ", "",   "リ", "ヲ"},
        {"ウ", "ク", "ス", "ツ", "ヌ", "フ", "ム", "ユ", "ル", "ン"},
        {"エ", "ケ", "セ", "テ", "ネ", "ヘ", "メ", "",   "レ", "ー"},
        {"オ", "コ", "ソ", "ト", "ノ", "ホ", "モ", "ヨ", "ロ", ""}
    };

    // QWERTY layout
    static const char *s_qwertyRows[3] = {
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm"
    };

    // 123 layout
    static const char *s_symRows[4][10] = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
        {"!", "?", "@", "#", "$", "%", "&", "*", "(", ")"},
        {"-", "_", "+", "=", ":", ";", "\"", "'", "/", "~"},
        {"<", ">", "[", "]", "{", "}", "\\", "|", ",", "."}
    };

    static void drawTopBottomBar(const std::string &txt)
    {
        unsigned x = 200 - (gfx::getTextWidth(txt) / 2);
        C2D_DrawRectSolid(0, 224, GFX_DEPTH_DEFAULT, 400, 16, gfx::rectLt);
        C2D_DrawRectSolid(0, 223, GFX_DEPTH_DEFAULT, 400, 1, gfx::divClr);
        gfx::drawText(txt, x, 224, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
    }

    static int getGridMaxRow(KeyboardMode m)
    {
        if (m == KBD_MODE_PINYIN || m == KBD_MODE_EN) return 3;
        if (m == KBD_MODE_KANA) return 4;
        return 3; // SYM
    }

    static std::vector<KeyItem> getGridRowKeys(KeyboardMode mode, int r, bool shift, bool katakana)
    {
        std::vector<KeyItem> items;
        if (mode == KBD_MODE_PINYIN)
        {
            if (r == 0)
            {
                for (int i = 0; i < 10; ++i)
                    items.push_back({i, 6 + i * 31, 85, 28, 26, std::string(1, s_qwertyRows[0][i])});
            }
            else if (r == 1)
            {
                for (int i = 0; i < 9; ++i)
                    items.push_back({i, 21 + i * 31, 115, 28, 26, std::string(1, s_qwertyRows[1][i])});
            }
            else if (r == 2)
            {
                for (int i = 0; i < 7; ++i)
                    items.push_back({i, 52 + i * 31, 145, 28, 26, std::string(1, s_qwertyRows[2][i])});
            }
            else if (r == 3)
            {
                // Pinyin Row 3: 清空 (left) and 空格 Space (wide right)
                items.push_back({0, 16, 175, 64, 26, getTxt("清空")});
                items.push_back({1, 88, 175, 216, 26, getTxt("空格")});
            }
        }
        else if (mode == KBD_MODE_EN)
        {
            if (r == 0)
            {
                for (int i = 0; i < 10; ++i)
                {
                    char c = s_qwertyRows[0][i];
                    if (shift) c = toupper((unsigned char)c);
                    items.push_back({i, 6 + i * 31, 85, 28, 26, std::string(1, c)});
                }
            }
            else if (r == 1)
            {
                for (int i = 0; i < 9; ++i)
                {
                    char c = s_qwertyRows[1][i];
                    if (shift) c = toupper((unsigned char)c);
                    items.push_back({i, 21 + i * 31, 115, 28, 26, std::string(1, c)});
                }
            }
            else if (r == 2)
            {
                for (int i = 0; i < 7; ++i)
                {
                    char c = s_qwertyRows[2][i];
                    if (shift) c = toupper((unsigned char)c);
                    items.push_back({i, 52 + i * 31, 145, 28, 26, std::string(1, c)});
                }
            }
            else if (r == 3)
            {
                // EN Row 3: 清空, 空格 Space, 大写/小写 (in place of old backspace)
                items.push_back({0, 10, 175, 54, 26, getTxt("清空")});
                items.push_back({1, 70, 175, 178, 26, getTxt("空格")});
                items.push_back({2, 254, 175, 56, 26, shift ? getTxt("小写") : getTxt("大写")});
            }
        }
        else if (mode == KBD_MODE_KANA)
        {
            for (int c = 0; c < 10; ++c)
            {
                const char *ch = katakana ? s_kanaKatakana[r][c] : s_kanaHiragana[r][c];
                if (ch && ch[0] != '\0')
                {
                    items.push_back({c, 6 + c * 31, 85 + r * 24, 28, 22, ch});
                }
            }
        }
        else if (mode == KBD_MODE_SYM)
        {
            for (int c = 0; c < 10; ++c)
            {
                const char *ch = s_symRows[r][c];
                if (ch && ch[0] != '\0')
                {
                    items.push_back({c, 6 + c * 31, 85 + r * 29, 28, 26, ch});
                }
            }
        }
        return items;
    }

    static std::vector<KeyItem> getToolbarKeys(KeyboardMode mode, int candPage, const std::vector<std::string> &candidates, const std::string &pinyinBuf, bool katakana)
    {
        std::vector<KeyItem> items;
        if (mode == KBD_MODE_PINYIN)
        {
            if (!pinyinBuf.empty() && !candidates.empty())
            {
                int idCounter = 0;
                if (candPage > 0)
                {
                    items.push_back({idCounter++, 8, 57, 24, 22, "<", -1});
                }
                for (int i = 0; i < 5; ++i)
                {
                    int idx = candPage * 5 + i;
                    if (idx < (int)candidates.size())
                    {
                        items.push_back({idCounter++, 34 + i * 50, 57, 46, 22, candidates[idx], idx});
                    }
                }
                if ((candPage + 1) * 5 < (int)candidates.size())
                {
                    items.push_back({idCounter++, 288, 57, 24, 22, ">", -1});
                }
            }
        }
        else if (mode == KBD_MODE_KANA)
        {
            items.push_back({0, 28,  57, 68, 22, katakana ? getTxt("平假名") : getTxt("片假名")});
            items.push_back({1, 112, 57, 40, 22, "゛"});
            items.push_back({2, 168, 57, 40, 22, "゜"});
            items.push_back({3, 224, 57, 68, 22, getTxt("小文字")});
        }
        // EN mode and SYM mode have no toolbar items as requested
        return items;
    }

    static std::vector<KeyItem> getBottomBarKeys()
    {
        std::vector<KeyItem> items;
        // Equal width: 7 buttons, each 42px wide, 3px spacing, 4px margins
        items.push_back({0, 4,   210, 42, 26, getTxt("拼音")});
        items.push_back({1, 49,  210, 42, 26, getTxt("假名")});
        items.push_back({2, 94,  210, 42, 26, "EN"});
        items.push_back({3, 139, 210, 42, 26, "123"});
        items.push_back({4, 184, 210, 42, 26, getTxt("退格")});
        items.push_back({5, 229, 210, 42, 26, getTxt("取消")});
        items.push_back({6, 274, 210, 42, 26, getTxt("完成")});
        return items;
    }

    static int findNearestCol(int curCenterX, const std::vector<KeyItem> &targets)
    {
        if (targets.empty()) return 0;
        int bestCol = 0;
        int bestDist = std::abs(targets[0].centerX() - curCenterX);
        for (size_t i = 1; i < targets.size(); ++i)
        {
            int dist = std::abs(targets[i].centerX() - curCenterX);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestCol = (int)i;
            }
        }
        return bestCol;
    }

    bool showKeyboard(const std::string &hint, const std::string &initialText, size_t maxBytes, std::string &outText, void (*drawTop)(), KeyboardMode initialMode)
    {
        loadPinyinDict();

        std::string curText = initialText;
        std::string pinyinBuf = "";
        KeyboardMode mode = initialMode;
        bool shift = false;
        bool katakana = false;
        int candPage = 0;
        std::vector<std::string> candidates;

        FocusPos focus;
        focus.area = FOCUS_GRID;
        focus.row = 0;
        focus.col = 0;

        bool prevTouch = false;
        uint32_t blinkTimer = 0;

        // Dynamic pulsing blue highlight parameters (TYSS global style)
        static int clrSh = 0xFA;
        static bool clrAdd = false;

        auto refreshCandidates = [&]() {
            candPage = 0;
            getPinyinCandidates(pinyinBuf, candidates);
        };

        while (aptMainLoop())
        {
            ui::updateInput();
            uint32_t down = ui::padKeysDown();
            uint32_t held = ui::padKeysHeld();
            touchPosition touch = ui::getTouchPosition();
            bool isTouch = (held & KEY_TOUCH);
            blinkTimer++;

            // Update dynamic pulsing blue highlight
            if (clrAdd)
            {
                clrSh += 6;
                if (clrSh >= 0xFA) clrAdd = false;
            }
            else
            {
                clrSh -= 3;
                if (clrSh <= 0x88) clrAdd = true;
            }
            uint32_t rectClr = 0xFF << 24 | 0xC5 << 16 | clrSh << 8 | 0x00;

            std::vector<KeyItem> curRowKeys = getGridRowKeys(mode, focus.row, shift, katakana);
            std::vector<KeyItem> toolbarKeys = getToolbarKeys(mode, candPage, candidates, pinyinBuf, katakana);
            std::vector<KeyItem> bottomKeys = getBottomBarKeys();

            int currentCenterX = 160;
            if (focus.area == FOCUS_GRID)
            {
                if (focus.col >= 0 && focus.col < (int)curRowKeys.size())
                    currentCenterX = curRowKeys[focus.col].centerX();
            }
            else if (focus.area == FOCUS_TOOLBAR)
            {
                if (focus.col >= 0 && focus.col < (int)toolbarKeys.size())
                    currentCenterX = toolbarKeys[focus.col].centerX();
            }
            else if (focus.area == FOCUS_BOTTOM_BAR)
            {
                if (focus.col >= 0 && focus.col < (int)bottomKeys.size())
                    currentCenterX = bottomKeys[focus.col].centerX();
            }

            // -----------------------------------------------------------------
            // D-Pad Navigation (Geometric Nearest-Center Algorithm)
            // -----------------------------------------------------------------
            bool kUp = (down & KEY_DUP);
            bool kDown = (down & KEY_DDOWN);
            bool kLeft = (down & KEY_DLEFT);
            bool kRight = (down & KEY_DRIGHT);

            if (kUp)
            {
                if (focus.area == FOCUS_BOTTOM_BAR)
                {
                    int targetRow = getGridMaxRow(mode);
                    auto targets = getGridRowKeys(mode, targetRow, shift, katakana);
                    focus.area = FOCUS_GRID;
                    focus.row = targetRow;
                    focus.col = findNearestCol(currentCenterX, targets);
                    curRowKeys = targets;
                }
                else if (focus.area == FOCUS_GRID)
                {
                    if (focus.row > 0)
                    {
                        int targetRow = focus.row - 1;
                        auto targets = getGridRowKeys(mode, targetRow, shift, katakana);
                        focus.row = targetRow;
                        focus.col = findNearestCol(currentCenterX, targets);
                        curRowKeys = targets;
                    }
                    else if (focus.row == 0)
                    {
                        if (!toolbarKeys.empty())
                        {
                            focus.area = FOCUS_TOOLBAR;
                            focus.col = findNearestCol(currentCenterX, toolbarKeys);
                        }
                    }
                }
            }
            else if (kDown)
            {
                if (focus.area == FOCUS_TOOLBAR)
                {
                    auto targets = getGridRowKeys(mode, 0, shift, katakana);
                    focus.area = FOCUS_GRID;
                    focus.row = 0;
                    focus.col = findNearestCol(currentCenterX, targets);
                    curRowKeys = targets;
                }
                else if (focus.area == FOCUS_GRID)
                {
                    int maxR = getGridMaxRow(mode);
                    if (focus.row < maxR)
                    {
                        int targetRow = focus.row + 1;
                        auto targets = getGridRowKeys(mode, targetRow, shift, katakana);
                        focus.row = targetRow;
                        focus.col = findNearestCol(currentCenterX, targets);
                        curRowKeys = targets;
                    }
                    else
                    {
                        focus.area = FOCUS_BOTTOM_BAR;
                        focus.col = findNearestCol(currentCenterX, bottomKeys);
                    }
                }
            }
            else if (kLeft)
            {
                if (focus.area == FOCUS_GRID)
                {
                    if (!curRowKeys.empty())
                        focus.col = (focus.col > 0) ? focus.col - 1 : (int)curRowKeys.size() - 1;
                }
                else if (focus.area == FOCUS_TOOLBAR)
                {
                    if (!toolbarKeys.empty())
                        focus.col = (focus.col > 0) ? focus.col - 1 : (int)toolbarKeys.size() - 1;
                }
                else if (focus.area == FOCUS_BOTTOM_BAR)
                {
                    focus.col = (focus.col > 0) ? focus.col - 1 : (int)bottomKeys.size() - 1;
                }
            }
            else if (kRight)
            {
                if (focus.area == FOCUS_GRID)
                {
                    if (!curRowKeys.empty())
                        focus.col = (focus.col + 1 < (int)curRowKeys.size()) ? focus.col + 1 : 0;
                }
                else if (focus.area == FOCUS_TOOLBAR)
                {
                    if (!toolbarKeys.empty())
                        focus.col = (focus.col + 1 < (int)toolbarKeys.size()) ? focus.col + 1 : 0;
                }
                else if (focus.area == FOCUS_BOTTOM_BAR)
                {
                    focus.col = (focus.col + 1 < (int)bottomKeys.size()) ? focus.col + 1 : 0;
                }
            }

            // -----------------------------------------------------------------
            // Helper: Flush Pinyin Buffer to Text (Rule 2)
            // -----------------------------------------------------------------
            auto flushPinyinToText = [&]() {
                if (!pinyinBuf.empty())
                {
                    if (curText.length() + pinyinBuf.length() <= maxBytes)
                    {
                        curText += pinyinBuf;
                    }
                    pinyinBuf.clear();
                    refreshCandidates();
                }
            };

            // -----------------------------------------------------------------
            // Circle Pad Left / Right: Global Page/Mode Switching
            // -----------------------------------------------------------------
            if (down & KEY_CPAD_LEFT)
            {
                flushPinyinToText();
                mode = (KeyboardMode)((mode + 3) % 4);
                focus.area = FOCUS_GRID;
                focus.row = 0;
                focus.col = 0;
            }
            else if (down & KEY_CPAD_RIGHT)
            {
                flushPinyinToText();
                mode = (KeyboardMode)((mode + 1) % 4);
                focus.area = FOCUS_GRID;
                focus.row = 0;
                focus.col = 0;
            }

            // -----------------------------------------------------------------
            // Action Trigger Helper
            // -----------------------------------------------------------------
            auto executeFocusedAction = [&]() -> int {
                if (focus.area == FOCUS_BOTTOM_BAR)
                {
                    if (focus.col == 0)
                    {
                        if (mode != KBD_MODE_PINYIN) { flushPinyinToText(); mode = KBD_MODE_PINYIN; refreshCandidates(); }
                    }
                    else if (focus.col == 1)
                    {
                        if (mode != KBD_MODE_KANA) { flushPinyinToText(); mode = KBD_MODE_KANA; }
                    }
                    else if (focus.col == 2)
                    {
                        if (mode != KBD_MODE_EN) { flushPinyinToText(); mode = KBD_MODE_EN; }
                    }
                    else if (focus.col == 3)
                    {
                        if (mode != KBD_MODE_SYM) { flushPinyinToText(); mode = KBD_MODE_SYM; }
                    }
                    else if (focus.col == 4) // 退格 (Backspace)
                    {
                        if (!pinyinBuf.empty()) { pinyinBuf.pop_back(); refreshCandidates(); }
                        else if (!curText.empty()) popUtf8Char(curText);
                    }
                    else if (focus.col == 5) return -1; // 取消
                    else if (focus.col == 6) // 完成
                    {
                        flushPinyinToText();
                        outText = curText;
                        return 1;
                    }
                }
                else if (focus.area == FOCUS_TOOLBAR)
                {
                    if (mode == KBD_MODE_PINYIN)
                    {
                        if (focus.col >= 0 && focus.col < (int)toolbarKeys.size())
                        {
                            const auto &it = toolbarKeys[focus.col];
                            if (it.label == "<")
                            {
                                if (candPage > 0)
                                {
                                    candPage--;
                                    auto newToolbar = getToolbarKeys(mode, candPage, candidates, pinyinBuf, katakana);
                                    int newLeftIdx = -1;
                                    for (size_t k = 0; k < newToolbar.size(); ++k)
                                    {
                                        if (newToolbar[k].label == "<") { newLeftIdx = (int)k; break; }
                                    }
                                    if (newLeftIdx != -1)
                                        focus.col = newLeftIdx;
                                    else
                                        focus.col = 0;
                                }
                            }
                            else if (it.label == ">")
                            {
                                if ((candPage + 1) * 5 < (int)candidates.size())
                                {
                                    candPage++;
                                    auto newToolbar = getToolbarKeys(mode, candPage, candidates, pinyinBuf, katakana);
                                    int newRightIdx = -1;
                                    for (size_t k = 0; k < newToolbar.size(); ++k)
                                    {
                                        if (newToolbar[k].label == ">") { newRightIdx = (int)k; break; }
                                    }
                                    if (newRightIdx != -1)
                                        focus.col = newRightIdx;
                                    else if (!newToolbar.empty())
                                        focus.col = (int)newToolbar.size() - 1;
                                }
                            }
                            else if (it.candIndex >= 0 && it.candIndex < (int)candidates.size())
                            {
                                const std::string &ch = candidates[it.candIndex];
                                if (curText.length() + ch.length() <= maxBytes)
                                {
                                    curText += ch;
                                    pinyinBuf.clear();
                                    refreshCandidates();
                                    focus.area = FOCUS_GRID;
                                    focus.row = 0;
                                    focus.col = 0;
                                }
                            }
                        }
                    }
                    else if (mode == KBD_MODE_KANA)
                    {
                        if (focus.col == 0) katakana = !katakana;
                        else if (focus.col == 1) applyKanaModifier(curText, 0); // 浊点
                        else if (focus.col == 2) applyKanaModifier(curText, 1); // 半浊点
                        else if (focus.col == 3) applyKanaModifier(curText, 2); // 小文字
                    }
                }
                else if (focus.area == FOCUS_GRID)
                {
                    if (mode == KBD_MODE_PINYIN)
                    {
                        if (focus.row >= 0 && focus.row <= 2)
                        {
                            char c = s_qwertyRows[focus.row][focus.col];
                            if (pinyinBuf.length() < 10)
                            {
                                pinyinBuf += c;
                                refreshCandidates();
                            }
                        }
                        else if (focus.row == 3)
                        {
                            if (focus.col == 0) // 清空
                            {
                                if (!pinyinBuf.empty()) { pinyinBuf.clear(); refreshCandidates(); }
                                else curText.clear();
                            }
                            else if (focus.col == 1) // 空格 Space
                            {
                                // Rule 10: If candidates exist, commit 1st candidate.
                                // If NO candidates but pinyinBuf has text, commit pinyinBuf directly as English text!
                                // If pinyinBuf is empty, insert regular space.
                                if (!candidates.empty())
                                {
                                    const std::string &ch = candidates[0];
                                    if (curText.length() + ch.length() <= maxBytes)
                                    {
                                        curText += ch;
                                        pinyinBuf.clear();
                                        refreshCandidates();
                                    }
                                }
                                else if (!pinyinBuf.empty())
                                {
                                    if (curText.length() + pinyinBuf.length() <= maxBytes)
                                    {
                                        curText += pinyinBuf;
                                        pinyinBuf.clear();
                                        refreshCandidates();
                                    }
                                }
                                else if (curText.length() + 1 <= maxBytes)
                                {
                                    curText += " ";
                                }
                            }
                        }
                    }
                    else if (mode == KBD_MODE_EN)
                    {
                        if (focus.row >= 0 && focus.row <= 2)
                        {
                            char c = s_qwertyRows[focus.row][focus.col];
                            if (shift) c = toupper((unsigned char)c);
                            if (curText.length() + 1 <= maxBytes) curText += c;
                        }
                        else if (focus.row == 3)
                        {
                            if (focus.col == 0) // 清空
                            {
                                curText.clear();
                            }
                            else if (focus.col == 1) // 空格 Space
                            {
                                if (curText.length() + 1 <= maxBytes) curText += " ";
                            }
                            else if (focus.col == 2) // 大写 / 小写 (Caps Toggle)
                            {
                                shift = !shift;
                            }
                        }
                    }
                    else if (mode == KBD_MODE_KANA || mode == KBD_MODE_SYM)
                    {
                        auto activeRowKeys = getGridRowKeys(mode, focus.row, shift, katakana);
                        if (focus.col >= 0 && focus.col < (int)activeRowKeys.size())
                        {
                            const std::string &ch = activeRowKeys[focus.col].label;
                            if (!ch.empty() && curText.length() + ch.length() <= maxBytes)
                                curText += ch;
                        }
                    }
                }
                return 0;
            };

            // -----------------------------------------------------------------
            // Hardware Button Handlers
            // -----------------------------------------------------------------
            if (down & KEY_A)
            {
                int res = executeFocusedAction();
                if (res == 1) return true;
                if (res == -1) return false;
            }

            if (down & KEY_START)
            {
                flushPinyinToText();
                outText = curText;
                return true;
            }

            if (down & KEY_B)
            {
                return false; // Rule 9: B strictly cancels, no backspace on B
            }

            if (down & KEY_X)
            {
                // Rule 9: X strictly backspaces
                if (!pinyinBuf.empty())
                {
                    pinyinBuf.pop_back();
                    refreshCandidates();
                }
                else if (!curText.empty())
                {
                    popUtf8Char(curText);
                }
            }

            // L/R for candidate paging in Pinyin mode
            if (mode == KBD_MODE_PINYIN && !pinyinBuf.empty())
            {
                if ((down & KEY_L) && candPage > 0)
                {
                    candPage--;
                }
                else if ((down & KEY_R) && (candPage + 1) * 5 < (int)candidates.size())
                {
                    candPage++;
                }
            }

            // -----------------------------------------------------------------
            // Stylus Touch Handling
            // -----------------------------------------------------------------
            if (isTouch && !prevTouch)
            {
                int tx = touch.px;
                int ty = touch.py;

                // 1. Candidate Bar (Y: 55..81)
                if (ty >= 55 && ty <= 81)
                {
                    for (size_t i = 0; i < toolbarKeys.size(); ++i)
                    {
                        const auto &it = toolbarKeys[i];
                        if (tx >= it.x && tx <= it.x + it.w)
                        {
                            focus.area = FOCUS_TOOLBAR;
                            focus.col = (int)i;
                            int res = executeFocusedAction();
                            if (res == 1) return true;
                            if (res == -1) return false;
                            break;
                        }
                    }
                }
                // 2. Main Keypad Grid (Y: 85..205)
                else if (ty >= 85 && ty <= 205)
                {
                    int maxR = getGridMaxRow(mode);
                    for (int r = 0; r <= maxR; ++r)
                    {
                        auto rowItems = getGridRowKeys(mode, r, shift, katakana);
                        if (rowItems.empty()) continue;
                        if (ty >= rowItems[0].y && ty <= rowItems[0].y + rowItems[0].h)
                        {
                            for (size_t c = 0; c < rowItems.size(); ++c)
                            {
                                if (tx >= rowItems[c].x && tx <= rowItems[c].x + rowItems[c].w)
                                {
                                    focus.area = FOCUS_GRID;
                                    focus.row = r;
                                    focus.col = (int)c;
                                    curRowKeys = rowItems;
                                    int res = executeFocusedAction();
                                    if (res == 1) return true;
                                    if (res == -1) return false;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
                // 3. Bottom Action Bar (Y: 208..238)
                else if (ty >= 208 && ty <= 238)
                {
                    for (size_t i = 0; i < bottomKeys.size(); ++i)
                    {
                        if (tx >= bottomKeys[i].x && tx <= bottomKeys[i].x + bottomKeys[i].w)
                        {
                            focus.area = FOCUS_BOTTOM_BAR;
                            focus.col = (int)i;
                            int res = executeFocusedAction();
                            if (res == 1) return true;
                            if (res == -1) return false;
                            break;
                        }
                    }
                }
            }

            prevTouch = isTouch;

            // -----------------------------------------------------------------
            // Render Frame
            // -----------------------------------------------------------------
            gfx::frameBegin();

            bool light = (bool)cfg::config["lightback"];

            uint32_t botTitleBg     = gfx::rectLt;
            uint32_t botTitleBorder = gfx::divClr;
            uint32_t botTitleText   = gfx::txtCont;

            uint32_t botInputBg     = light ? 0xFFFFFFFF : 0xFF202020;
            uint32_t botInputBorder = light ? 0xFFCCCCCC : 0xFF444444;
            uint32_t botInputText   = gfx::txtCont;

            uint32_t candBarBg      = light ? 0xFFEAEAEA : 0xFF1C1C1C;
            uint32_t candBtnBg      = gfx::btnClr;
            uint32_t candBtnTxt     = gfx::txtCont;
            uint32_t candSlotBg     = light ? 0xFFFFFFFF : 0xFF2D2D2D;
            uint32_t candNumTxt     = light ? 0xFF888888 : 0xFFAAAAAA;
            uint32_t candCharTxt    = gfx::txtCont; // Rule 1: Clean, harmonious text color (no dark orange)

            uint32_t barBtnBg       = gfx::btnClr;
            uint32_t barBtnTxt      = gfx::txtCont;
            uint32_t barBtnActiveBg = light ? 0xFFBBDDFF : 0xFF505030;

            uint32_t keyBg          = light ? 0xFFFFFFFF : 0xFF353535;
            uint32_t keyTxt         = gfx::txtCont;

            uint32_t keyClearBg     = light ? 0xFFE4E4E4 : 0xFF2A2A2A;
            uint32_t keyClearTxt    = light ? 0xFF555555 : 0xFFAAAAAA;
            uint32_t keySpaceBg     = light ? 0xFFE0E0E0 : 0xFF383838;
            uint32_t keySpaceTxt    = gfx::txtCont;
            uint32_t keyBsBg        = light ? 0xFFFFDADB : 0xFF3A2A2A;
            uint32_t keyBsTxt       = light ? 0xFFCC2222 : 0xFFFFAAAA;

            uint32_t botActionBarBg = gfx::rectLt;
            uint32_t tabActiveBg    = light ? 0xFFFFFFFF : 0xFF505050;
            uint32_t tabActiveTxt   = gfx::txtCont;
            uint32_t tabInactiveBg  = gfx::btnClr;
            uint32_t tabInactiveTxt = light ? 0xFF666666 : 0xFF999999;

            uint32_t btnCancelBg    = light ? 0xFFFFDADB : 0xFF452525;
            uint32_t btnCancelTxt   = light ? 0xFFC02020 : 0xFFFF8888;
            uint32_t btnDoneBg      = light ? 0xFFD6F5D6 : 0xFF254525;
            uint32_t btnDoneTxt     = light ? 0xFF187A18 : 0xFF88FF88;

            // -----------------------------------------------------------------
            // TOP SCREEN: Original Top Content + Operation Bar at Bottom
            // -----------------------------------------------------------------
            gfx::frameStartTop();
            if (drawTop)
            {
                drawTop();
            }
            else
            {
                ui::drawUIBar(TITLE_TEXT + " - " + hint, ui::SCREEN_TOP, true);
            }
            // Rule 1: START text prompt. Circle pad is \ue01A\ue077\ue019.
            drawTopBottomBar(getTxt("\ue000 确定  \ue001 取消  \ue002 退格  START 完成  \ue01A\ue077\ue019 模式"));

            // -----------------------------------------------------------------
            // BOTTOM SCREEN: Virtual Touch Keyboard
            // -----------------------------------------------------------------
            gfx::frameStartBot();

            // 1. Header Bar (Y: 0..22)
            C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT, 320, 22, botTitleBg);
            C2D_DrawRectSolid(0, 22, GFX_DEPTH_DEFAULT, 320, 1, botTitleBorder);
            gfx::drawText(hint, 10, 4, GFX_DEPTH_DEFAULT, 0.46f, botTitleText);

            // 2. Text Input Box (Y: 26..52)
            C2D_DrawRectSolid(6, 26, GFX_DEPTH_DEFAULT, 308, 26, botInputBg);
            C2D_DrawRectSolid(6, 26, GFX_DEPTH_DEFAULT, 308, 1, botInputBorder);
            C2D_DrawRectSolid(6, 51, GFX_DEPTH_DEFAULT, 308, 1, botInputBorder);
            C2D_DrawRectSolid(6, 26, GFX_DEPTH_DEFAULT, 1, 26, botInputBorder);
            C2D_DrawRectSolid(313, 26, GFX_DEPTH_DEFAULT, 1, 26, botInputBorder);

            std::string boxTxt = curText;
            if (!pinyinBuf.empty()) boxTxt += " [" + pinyinBuf + "]";
            if ((blinkTimer / 30) % 2 == 0) boxTxt += "|";
            gfx::drawText(boxTxt, 12, 32, GFX_DEPTH_DEFAULT, 0.5f, botInputText);

            char capBuf[32];
            snprintf(capBuf, sizeof(capBuf), "%zu/%zuB", curText.length(), maxBytes);
            int capW = gfx::getTextWidth(capBuf);
            uint32_t capClr = (curText.length() >= maxBytes) ? (light ? 0xFF2222DD : 0xFF5555FF) : (light ? 0xFF777777 : 0xFFAAAAAA);
            gfx::drawText(capBuf, 308 - capW, 32, GFX_DEPTH_DEFAULT, 0.44f, capClr);

            // 3. Candidate Bar / Toolbar (Y: 55..81)
            C2D_DrawRectSolid(6, 55, GFX_DEPTH_DEFAULT, 308, 26, candBarBg);
            C2D_DrawRectSolid(6, 55, GFX_DEPTH_DEFAULT, 308, 1, gfx::divClr);
            C2D_DrawRectSolid(6, 80, GFX_DEPTH_DEFAULT, 308, 1, gfx::divClr);

            // Rule 1: No more static prompt string when pinyinBuf is empty
            if (mode == KBD_MODE_PINYIN && !pinyinBuf.empty())
            {
                for (size_t i = 0; i < toolbarKeys.size(); ++i)
                {
                    const auto &it = toolbarKeys[i];
                    if (it.label == "<" || it.label == ">")
                    {
                        C2D_DrawRectSolid(it.x, it.y, GFX_DEPTH_DEFAULT, it.w, it.h, candBtnBg);
                        if (focus.area == FOCUS_TOOLBAR && focus.col == (int)i)
                            gfx::drawBoundingBox(it.x, it.y, it.w, it.h, GFX_DEPTH_DEFAULT, rectClr);
                        int tx = it.x + (it.w - gfx::getTextWidth(it.label)) / 2;
                        int ty = it.y + (it.h - 14) / 2;
                        gfx::drawText(it.label, tx, ty, GFX_DEPTH_DEFAULT, 0.5f, candBtnTxt);
                    }
                    else
                    {
                        C2D_DrawRectSolid(it.x, it.y, GFX_DEPTH_DEFAULT, it.w, it.h, candSlotBg);
                        if (focus.area == FOCUS_TOOLBAR && focus.col == (int)i)
                            gfx::drawBoundingBox(it.x, it.y, it.w, it.h, GFX_DEPTH_DEFAULT, rectClr);
                        char numPrefix[16];
                        snprintf(numPrefix, sizeof(numPrefix), "%d.", (int)(it.candIndex % 5) + 1);
                        int tyNum = it.y + (it.h - 12) / 2;
                        int tyChar = it.y + (it.h - 14) / 2;
                        gfx::drawText(numPrefix, it.x + 2, tyNum, GFX_DEPTH_DEFAULT, 0.42f, candNumTxt);
                        gfx::drawText(it.label, it.x + 18, tyChar, GFX_DEPTH_DEFAULT, 0.5f, candCharTxt);
                    }
                }
            }
            else if (mode == KBD_MODE_KANA)
            {
                for (size_t i = 0; i < toolbarKeys.size(); ++i)
                {
                    const auto &it = toolbarKeys[i];
                    uint32_t bg = (i == 0 && katakana) ? barBtnActiveBg : barBtnBg;
                    C2D_DrawRectSolid(it.x, it.y, GFX_DEPTH_DEFAULT, it.w, it.h, bg);
                    if (focus.area == FOCUS_TOOLBAR && focus.col == (int)i)
                        gfx::drawBoundingBox(it.x, it.y, it.w, it.h, GFX_DEPTH_DEFAULT, rectClr);

                    float scale = 0.46f;
                    int textWidth = gfx::getTextWidth(it.label);
                    if (textWidth > it.w - 4)
                    {
                        scale = (float)(it.w - 4) / textWidth * 0.46f;
                        textWidth = it.w - 4;
                    }
                    int tx = it.x + (it.w - textWidth) / 2;
                    int ty = it.y + (it.h - (int)(scale * 28.0f)) / 2;
                    gfx::drawText(it.label, tx, ty, GFX_DEPTH_DEFAULT, scale, barBtnTxt);
                }
            }

            // 4. Keyboard Grid (Y: 85..205)
            int maxR = getGridMaxRow(mode);
            for (int r = 0; r <= maxR; ++r)
            {
                auto rowKeys = getGridRowKeys(mode, r, shift, katakana);
                for (size_t c = 0; c < rowKeys.size(); ++c)
                {
                    const auto &k = rowKeys[c];
                    if (k.label.empty()) continue;

                    uint32_t bg = keyBg;
                    uint32_t tc = keyTxt;
                    float scale = 0.52f;

                    if (mode == KBD_MODE_PINYIN && r == 3)
                    {
                        if (c == 0) { bg = keyClearBg; tc = keyClearTxt; scale = 0.44f; }
                        else if (c == 1) { bg = keySpaceBg; tc = keySpaceTxt; scale = 0.48f; }
                    }
                    else if (mode == KBD_MODE_EN && r == 3)
                    {
                        if (c == 0) { bg = keyClearBg; tc = keyClearTxt; scale = 0.44f; }
                        else if (c == 1) { bg = keySpaceBg; tc = keySpaceTxt; scale = 0.48f; }
                        else if (c == 2) { bg = barBtnBg; tc = barBtnTxt; scale = 0.46f; }
                    }
                    else if (mode == KBD_MODE_KANA)
                    {
                        scale = 0.46f;
                    }

                    C2D_DrawRectSolid(k.x, k.y, GFX_DEPTH_DEFAULT, k.w, k.h, bg);
                    // Rule 6: Draw global dynamic blue highlight
                    if (focus.area == FOCUS_GRID && focus.row == r && focus.col == (int)c)
                    {
                        gfx::drawBoundingBox(k.x, k.y, k.w, k.h, GFX_DEPTH_DEFAULT, rectClr);
                    }

                    int tx = k.x + (k.w - gfx::getTextWidth(k.label)) / 2;
                    int ty = k.y + (k.h - (int)(scale * 28.0f)) / 2;
                    gfx::drawText(k.label, tx, ty, GFX_DEPTH_DEFAULT, scale, tc);
                }
            }

            // 5. Bottom Action Bar (Y: 208..238)
            C2D_DrawRectSolid(0, 208, GFX_DEPTH_DEFAULT, 320, 32, botActionBarBg);
            C2D_DrawRectSolid(0, 208, GFX_DEPTH_DEFAULT, 320, 1, gfx::divClr);

            for (size_t i = 0; i < bottomKeys.size(); ++i)
            {
                const auto &b = bottomKeys[i];
                uint32_t bg = gfx::btnClr;
                uint32_t tc = gfx::txtCont;

                if (i <= 3) // Tabs
                {
                    bool active = ((int)mode == (int)i);
                    bg = active ? tabActiveBg : tabInactiveBg;
                    tc = active ? tabActiveTxt : tabInactiveTxt;
                }
                else if (i == 4) // 退格
                {
                    bg = keyBsBg;
                    tc = keyBsTxt;
                }
                else if (i == 5) // 取消
                {
                    bg = btnCancelBg;
                    tc = btnCancelTxt;
                }
                else if (i == 6) // 完成
                {
                    bg = btnDoneBg;
                    tc = btnDoneTxt;
                }

                C2D_DrawRectSolid(b.x, b.y, GFX_DEPTH_DEFAULT, b.w, b.h, bg);
                // Rule 6: Draw global dynamic blue highlight
                if (focus.area == FOCUS_BOTTOM_BAR && focus.col == (int)i)
                {
                    gfx::drawBoundingBox(b.x, b.y, b.w, b.h, GFX_DEPTH_DEFAULT, rectClr);
                }

                float scale = 0.46f;
                int textWidth = gfx::getTextWidth(b.label);
                if (textWidth > b.w - 4)
                {
                    scale = (float)(b.w - 4) / textWidth * 0.46f;
                    textWidth = b.w - 4;
                }
                int tx = b.x + (b.w - textWidth) / 2;
                int ty = b.y + (b.h - (int)(scale * 28.0f)) / 2;
                gfx::drawText(b.label, tx, ty, GFX_DEPTH_DEFAULT, scale, tc);
            }

            gfx::frameEnd();
        }

        return false;
    }
}
