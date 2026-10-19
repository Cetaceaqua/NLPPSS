#include "nlpp_dict.h"
#include "ui.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace nlpp
{
    bool Dict::s_loaded = false;
    std::vector<ReadingEntry> Dict::s_readings;
    std::unordered_map<uint16_t, size_t> Dict::s_readingMap;

    static const char *s_prefectures[] = {
        "北海道", "青森县", "岩手县", "宫城县", "秋田县", "山形县", "福岛县",
        "茨城县", "栃木县", "群马县", "埼玉县", "千叶县", "东京都", "神奈川县",
        "新潟县", "富山县", "石川县", "福井县", "山梨县", "长野县", "岐阜县",
        "静冈县", "爱知县", "三重县", "滋贺县", "京都府", "大阪府", "兵库县",
        "奈良县", "和歌山县", "鸟取县", "岛根县", "冈山县", "广岛县", "山口县",
        "德岛县", "香川县", "爱媛县", "高知县", "福冈县", "佐贺县", "长崎县",
        "熊本县", "大分县", "宫崎县", "鹿儿岛县", "冲绳县"
    };

    static const char *s_bloodTypes[] = {
        "A型", "B型", "O型", "AB型"
    };

    static const HairColorDef s_hairColors[20] = {
        {0,  "初期デフォルト", "初期色",        38,  38,  41},
        {1,  "黒髪系",         "黒",            56,  51,  51},
        {2,  "明るい色",       "黄色っぽい Ａ", 160, 137, 81},
        {3,  "明るい色",       "黄色っぽい Ｂ", 188, 150, 58},
        {4,  "明るい色",       "赤っぽい Ａ",   170, 137, 119},
        {5,  "明るい色",       "赤っぽい Ｂ",   122, 94,  73},
        {6,  "明るい色",       "黒っぽい Ａ",   104, 104, 104},
        {7,  "明るい色",       "黒っぽい Ｂ",   53,  53,  53},
        {8,  "自然な茶系",     "黄色っぽい Ａ", 76,  51,  43},
        {9,  "自然な茶系",     "黄色っぽい Ｂ", 53,  15,  15},
        {10, "自然な茶系",     "赤っぽい Ａ",   99,  12,  5},
        {11, "自然な茶系",     "赤っぽい Ｂ",   56,  0,   0},
        {12, "自然な茶系",     "黒っぽい Ａ",   81,  71,  51},
        {13, "自然な茶系",     "黒っぽい Ｂ",   30,  20,  7},
        {14, "特殊カラー",     "ピンク",        216, 51,  91},
        {15, "特殊カラー",     "ブルー",        28,  94,  173},
        {16, "特殊カラー",     "グリーン",      28,  153, 86},
        {17, "特殊カラー",     "パープル",      153, 63,  216},
        {18, "特殊カラー",     "オレンジ",      216, 114, 51},
        {19, "特殊カラー",     "シアン",        22,  79,  79}
    };

    static const HairStyleDef s_allStyles[] = {
        {0,  "基本 (休闲)",             "休闲",       "约会",   false, false},
        {1,  "居家 (休闲)",             "休闲",       "居家",   false, false},
        {2,  "可爱 A",                 "可爱",       "约会",   false, false},
        {3,  "休闲 A",                 "休闲",       "约会",   false, false},
        {4,  "清纯 A",                 "清纯",       "约会",   false, false},
        {5,  "盘发 [泳装·和服] (清纯)", "清纯",       "活动",   false, false},
        {6,  "派对 (清纯)",             "清纯",       "活动",   false, false},
        {7,  "演唱会 (可爱)",           "可爱",       "活动",   true,  false},
        {8,  "哥特萝莉 (可爱)",         "可爱",       "活动",   true,  false},
        {9,  "自然 (清纯)",             "清纯",       "活动",   true,  true},
        {14, "可爱 B",                 "可爱",       "约会",   false, false},
        {15, "休闲 B",                 "休闲",       "约会",   false, false},
        {16, "清纯 B",                 "清纯",       "约会",   false, false},
        {17, "可爱 C",                 "可爱",       "约会",   false, false},
        {18, "休闲 C",                 "休闲",       "约会",   false, false},
        {19, "清纯 C",                 "清纯",       "约会",   false, false},
        {20, "NEW (可爱)",             "可爱",       "约会",   false, false}
    };


    bool Dict::init()
    {
        if (s_loaded) return true;

        const char *candidates[] = {
            "romfs:/dict/readings.bin",
            "romfs/dict/readings.bin",
            "d:/Files/GitHub Desktop/Cetaceaqua/NLPPSS/romfs/dict/readings.bin"
        };

        FILE *f = nullptr;
        for (const char *path : candidates)
        {
            f = fopen(path, "rb");
            if (f) break;
        }

        if (!f) return false;

        char magic[4];
        uint32_t count = 0, headerSize = 0, stringsOffset = 0;
        if (fread(magic, 1, 4, f) != 4 ||
            fread(&count, 4, 1, f) != 1 ||
            fread(&headerSize, 4, 1, f) != 1 ||
            fread(&stringsOffset, 4, 1, f) != 1)
        {
            fclose(f);
            return false;
        }

        if (memcmp(magic, "NLRD", 4) != 0 || count == 0)
        {
            fclose(f);
            return false;
        }

        struct RawIndex
        {
            uint16_t id;
            uint8_t slot_mask;
            uint8_t heroine_mask;
            uint32_t str_offset;
        };

        std::vector<RawIndex> rawIndices(count);
        fseek(f, headerSize, SEEK_SET);
        if (fread(rawIndices.data(), sizeof(RawIndex), count, f) != count)
        {
            fclose(f);
            return false;
        }

        // Read strings block
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        long stringsSize = fileSize - stringsOffset;
        if (stringsSize <= 0)
        {
            fclose(f);
            return false;
        }

        std::vector<char> stringsPool(stringsSize);
        fseek(f, stringsOffset, SEEK_SET);
        if (fread(stringsPool.data(), 1, stringsSize, f) != (size_t)stringsSize)
        {
            fclose(f);
            return false;
        }
        fclose(f);

        s_readings.resize(count);
        s_readingMap.clear();
        s_readingMap.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            s_readings[i].id = rawIndices[i].id;
            s_readings[i].slot_mask = rawIndices[i].slot_mask;
            s_readings[i].heroine_mask = rawIndices[i].heroine_mask;

            uint32_t offset = rawIndices[i].str_offset;
            if (offset < stringsPool.size())
            {
                const char *t = &stringsPool[offset];
                s_readings[i].text = t;
                size_t tLen = strlen(t);
                if (offset + tLen + 1 < stringsPool.size())
                {
                    s_readings[i].romaji = &stringsPool[offset + tLen + 1];
                }
            }
            s_readingMap[s_readings[i].id] = i;
        }

        s_loaded = true;
        return true;
    }

    void Dict::exit()
    {
        s_readings.clear();
        s_readingMap.clear();
        s_loaded = false;
    }

    const ReadingEntry *Dict::getReadingById(uint16_t id)
    {
        if (!s_loaded) init();
        auto it = s_readingMap.find(id);
        if (it != s_readingMap.end())
        {
            return &s_readings[it->second];
        }
        return nullptr;
    }

    std::string Dict::getReadingText(uint16_t id)
    {
        if (id == 0xFFFF) return getTxt("(空)");
        const ReadingEntry *entry = getReadingById(id);
        if (entry) return entry->text;
        char buf[16];
        snprintf(buf, sizeof(buf), "[%u]", id);
        return std::string(buf);
    }

    std::string Dict::getReadingDisplay(uint16_t id)
    {
        if (id == 0xFFFF) return getTxt("(空)");
        const ReadingEntry *entry = getReadingById(id);
        if (entry)
        {
            if (!entry->romaji.empty())
                return entry->text + " (" + entry->romaji + ")";
            return entry->text;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "[%u]", id);
        return std::string(buf);
    }

    static std::string cleanSearchStr(const std::string &s)
    {
        std::string res;
        res.reserve(s.size());
        for (char c : s)
        {
            if (c != '-' && c != '\'' && c != ' ' && c != '_')
                res += (char)tolower((unsigned char)c);
        }
        return res;
    }

    static std::string romajiPrefixToKana(const std::string &romaji)
    {
        static const struct { const char *rom; const char *kana; } s_map[] = {
            {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
            {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
            {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
            {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
            {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
            {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
            {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
            {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
            {"shi", "し"}, {"chi", "ち"}, {"tsu", "つ"},
            {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
            {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
            {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
            {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
            {"ha", "は"}, {"hi", "ひ"}, {"fu", "ふ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
            {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
            {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
            {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
            {"wa", "わ"}, {"wo", "を"},
            {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
            {"za", "ざ"}, {"ji", "じ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
            {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
            {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
            {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
            {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"}
        };
        for (const auto &item : s_map)
        {
            size_t len = strlen(item.rom);
            if (romaji.size() >= len && romaji.compare(0, len, item.rom) == 0)
            {
                return item.kana;
            }
        }
        return "";
    }

    static std::string kanaToHiragana(const std::string &str)
    {
        std::string res;
        res.reserve(str.size());
        const uint8_t *p = (const uint8_t *)str.data();
        size_t len = str.size();
        size_t i = 0;
        while (i < len)
        {
            if (i + 2 < len && p[i] == 0xE3)
            {
                uint32_t cp = ((p[i] & 0x0F) << 12) | ((p[i+1] & 0x3F) << 6) | (p[i+2] & 0x3F);
                if (cp >= 0x30A1 && cp <= 0x30F6) // Katakana -> Hiragana
                {
                    cp -= 0x60;
                    res += (char)(0xE0 | ((cp >> 12) & 0x0F));
                    res += (char)(0x80 | ((cp >> 6) & 0x3F));
                    res += (char)(0x80 | (cp & 0x3F));
                    i += 3;
                    continue;
                }
            }
            res += str[i++];
        }
        return res;
    }

    std::vector<const ReadingEntry *> Dict::filterReadings(uint8_t slotMask, uint8_t heroineMask, const std::string &search)
    {
        if (!s_loaded) init();
        std::vector<const ReadingEntry *> result;
        result.reserve(256);

        if (search.empty())
        {
            for (const auto &entry : s_readings)
            {
                if (slotMask && !(entry.slot_mask & slotMask)) continue;
                if (heroineMask && !(entry.heroine_mask & heroineMask)) continue;
                result.push_back(&entry);
            }
            return result;
        }

        std::string lowerSearch;
        lowerSearch.reserve(search.size());
        for (char c : search) lowerSearch += (char)tolower((unsigned char)c);
        std::string cleanSearch = cleanSearchStr(lowerSearch);
        std::string hSearch = kanaToHiragana(search);

        std::unordered_set<uint16_t> seenIds;

        // Phase 1: Candidate 0 (prolonged sound) for base readings (Slot 0 / Slot 1)
        if ((slotMask & 0x03) != 0)
        {
            std::string firstChar;
            unsigned char c = (unsigned char)search[0];
            if (c < 0x80)
            {
                // Romaji input -> map first syllable to Kana
                firstChar = romajiPrefixToKana(lowerSearch);
            }
            else
            {
                // UTF-8 Kana input (normalized to Hiragana)
                if (!hSearch.empty())
                {
                    unsigned char hc = (unsigned char)hSearch[0];
                    size_t charLen = 1;
                    if ((hc & 0xE0) == 0xC0) charLen = 2;
                    else if ((hc & 0xF0) == 0xE0) charLen = 3;
                    else if ((hc & 0xF8) == 0xF0) charLen = 4;
                    if (charLen <= hSearch.size())
                    {
                        firstChar = hSearch.substr(0, charLen);
                    }
                }
            }

            if (!firstChar.empty() && firstChar != "っ" && firstChar != "ッ")
            {
                std::string prolonged = firstChar + "ー";
                for (const auto &entry : s_readings)
                {
                    if (entry.id <= 957 && entry.text == prolonged)
                    {
                        result.push_back(&entry);
                        seenIds.insert(entry.id);
                        break;
                    }
                }
            }
        }

        // Phase 2: Exact matches (text, normalized kana, or romaji)
        for (const auto &entry : s_readings)
        {
            if (slotMask && !(entry.slot_mask & slotMask)) continue;
            if (heroineMask && !(entry.heroine_mask & heroineMask)) continue;
            if (seenIds.find(entry.id) != seenIds.end()) continue;

            std::string hText = kanaToHiragana(entry.text);
            bool match = (entry.text == search || (!hSearch.empty() && hText == hSearch) || entry.romaji == lowerSearch);
            if (!match && !cleanSearch.empty())
            {
                std::string cleanRom = cleanSearchStr(entry.romaji);
                if (cleanRom == cleanSearch) match = true;
            }

            if (match)
            {
                result.push_back(&entry);
                seenIds.insert(entry.id);
            }
        }

        // Phase 3: Prefix matches
        for (const auto &entry : s_readings)
        {
            if (slotMask && !(entry.slot_mask & slotMask)) continue;
            if (heroineMask && !(entry.heroine_mask & heroineMask)) continue;
            if (seenIds.find(entry.id) != seenIds.end()) continue;

            std::string hText = kanaToHiragana(entry.text);
            bool match = (entry.text.rfind(search, 0) == 0 ||
                          (!hSearch.empty() && hText.rfind(hSearch, 0) == 0) ||
                          entry.romaji.rfind(lowerSearch, 0) == 0);
            if (!match && !cleanSearch.empty())
            {
                std::string cleanRom = cleanSearchStr(entry.romaji);
                if (cleanRom.rfind(cleanSearch, 0) == 0) match = true;
            }

            if (match)
            {
                result.push_back(&entry);
                seenIds.insert(entry.id);
            }
        }

        // Phase 4: Substring matches
        for (const auto &entry : s_readings)
        {
            if (slotMask && !(entry.slot_mask & slotMask)) continue;
            if (heroineMask && !(entry.heroine_mask & heroineMask)) continue;
            if (seenIds.find(entry.id) != seenIds.end()) continue;

            std::string hText = kanaToHiragana(entry.text);
            bool match = (entry.text.find(search) != std::string::npos ||
                          (!hSearch.empty() && hText.find(hSearch) != std::string::npos) ||
                          entry.romaji.find(lowerSearch) != std::string::npos);
            if (!match && !cleanSearch.empty())
            {
                std::string cleanRom = cleanSearchStr(entry.romaji);
                if (cleanRom.find(cleanSearch) != std::string::npos) match = true;
            }

            if (match)
            {
                result.push_back(&entry);
                seenIds.insert(entry.id);
            }
        }

        return result;
    }

    const char *Dict::getPrefectureName(uint16_t id)
    {
        if (id < 47) return getTxt(s_prefectures[id]);
        return getTxt("未设定");
    }

    int Dict::getPrefectureCount()
    {
        return 47;
    }

    const char *Dict::getBloodTypeName(BloodType blood)
    {
        if ((size_t)blood < 4) return getTxt(s_bloodTypes[(size_t)blood]);
        return getTxt("A型");
    }

    const HairColorDef &Dict::getHairColor(uint8_t id)
    {
        if (id < 20) return s_hairColors[id];
        return s_hairColors[0];
    }

    int Dict::getHairColorCount()
    {
        return 20;
    }

    void Dict::getDefaultHairRGB(HeroineID heroine, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        switch (heroine)
        {
            case HEROINE_MANAKA: // 26 26 29
                r = 38; g = 38; b = 41; break;
            case HEROINE_RINKO:  // 26 26 33
                r = 38; g = 38; b = 51; break;
            case HEROINE_NENE:   // 4C 33 2B
                r = 76; g = 51; b = 43; break;
            default:
                r = 38; g = 38; b = 41; break;
        }
    }

    uint8_t Dict::matchClosestColorId(uint8_t r, uint8_t g, uint8_t b)
    {
        uint8_t bestId = 1;
        uint32_t bestDist = UINT32_MAX;
        for (int i = 1; i <= 19; ++i)
        {
            const auto &p = s_hairColors[i];
            int dr = (int)r - (int)p.r;
            int dg = (int)g - (int)p.g;
            int db = (int)b - (int)p.b;
            uint32_t dist = (uint32_t)(2 * dr * dr + 4 * dg * dg + 3 * db * db);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestId = (uint8_t)i;
            }
        }
        return bestId;
    }

    std::string Dict::getColorHighlightDesc(uint8_t colorId)
    {
        switch (colorId)
        {
            case 0:
            case 1:
                return getTxt("自然纯白");
            case 2:
            case 3:
                return getTxt("淡金冷光");
            case 4:
            case 5:
                return getTxt("暖淡紫粉");
            case 6:
            case 7:
                return getTxt("银白冷灰");
            case 8:
            case 9:
            case 12:
            case 13:
                return getTxt("琥珀栗茶");
            case 10:
            case 11:
                return getTxt("深赤酒红");
            case 14:
                return getTxt("艳丽粉红");
            case 15:
                return getTxt("天蓝反光");
            case 16:
                return getTxt("翡翠草绿");
            case 17:
                return getTxt("魅惑紫罗");
            case 18:
                return getTxt("明艳珊瑚");
            case 19:
                return getTxt("墨绿深青");
            default:
                return getTxt("标准中性");
        }
    }

    const std::vector<HairStyleDef> &Dict::getAvailableStyles(HeroineID heroine, uint8_t hairLength)
    {
        static std::vector<HairStyleDef> cached;
        cached.clear();

        // Length stage 2 is Long for all heroines (0x466 = 2)
        bool isLong = (hairLength >= 2);

        for (const auto &style : s_allStyles)
        {
            if (style.long_only && !isLong) continue;
            if (style.manaka_only && heroine != HEROINE_MANAKA) continue;
            cached.push_back(style);
        }
        return cached;
    }

    const char *Dict::getStyleName(uint8_t styleId)
    {
        for (const auto &s : s_allStyles)
        {
            if (s.style_id == styleId) return getTxt(s.name);
        }
        return getTxt("基本 (休闲)");
    }

    uint16_t Dict::getHairModelId(HeroineID heroine, uint8_t hairLength, uint8_t styleId)
    {
        // hairLength: 2 = Long, 1 = Middle, 0 = Short
        // heroine: 0 = Manaka, 1 = Rinko, 2 = Nene
        if (heroine < HEROINE_MANAKA || heroine > HEROINE_NENE) return 0;
        int h = (int)heroine;
        int l = std::min((int)hairLength, 2);

        struct StyleModelRow {
            uint8_t styleId;
            uint16_t modelIds[3][3]; // [heroine: 0=Manaka, 1=Rinko, 2=Nene][length: 0=Short, 1=Mid, 2=Long]
        };

        static const StyleModelRow s_table[] = {
            { 0, {{ 19,  11,   0}, { 45,  37,  27}, { 69,  61,  51}} },
            { 1, {{ 20,  12,   1}, { 46,  38,  28}, { 70,  62,  52}} },
            { 2, {{ 21,  13,   2}, { 47,  39,  29}, { 71,  63,  53}} },
            { 3, {{ 22,  14,   3}, { 46,  40,  30}, { 72,  64,  54}} },
            { 4, {{ 23,  15,   4}, { 48,  41,  31}, { 73,  65,  55}} },
            { 5, {{ 24,  16,   5}, { 46,  42,  32}, { 74,  66,  56}} },
            { 6, {{ 25,  17,   6}, { 49,  43,  33}, { 75,  67,  57}} },
            { 7, {{  0,   0,   7}, {  0,   0,  34}, {  0,   0,  58}} },
            { 8, {{  0,   0,   8}, {  0,   0,  35}, {  0,   0,  59}} },
            { 9, {{  0,   0,   9}, {  0,   0,   0}, {  0,   0,   0}} },
            {14, {{341, 335, 329}, {359, 353, 347}, {377, 371, 365}} },
            {15, {{343, 337, 331}, {361, 355, 349}, {379, 373, 367}} },
            {16, {{345, 339, 333}, {363, 357, 351}, {381, 375, 369}} },
            {17, {{342, 336, 330}, {360, 354, 348}, {378, 372, 366}} },
            {18, {{344, 338, 332}, {362, 356, 350}, {380, 374, 368}} },
            {19, {{346, 340, 334}, {364, 358, 352}, {382, 376, 370}} },
            {20, {{438, 434, 430}, {450, 446, 442}, {462, 458, 454}} }
        };

        for (const auto &row : s_table)
        {
            if (row.styleId == styleId)
            {
                return row.modelIds[h][l];
            }
        }
        return 0;
    }

    std::string Dict::getHairLengthName(HeroineID heroine, uint8_t stage)
    {
        (void)heroine;
        // Savedata 0x466 stages: 2 = Long, 1 = Medium, 0 = Short
        if (stage >= 2) return getTxt("长发");
        if (stage == 1) return getTxt("中长发");
        return getTxt("短发");
    }

    std::vector<uint8_t> Dict::getValidLengthStages(HeroineID heroine)
    {
        (void)heroine;
        // All heroines support 3 stages in 0x466: 2 (Long), 1 (Middle), 0 (Short)
        return {2, 1, 0};
    }

    static const uint32_t s_rankThresholds[22] = {
        100, 250, 750, 1200, 2500, 3500, 5000, 7500, 9999, 13000,
        14500, 16250, 18250, 20500, 23000, 25750, 28750, 32000, 35500, 39250,
        43250, 47500
    };

    static const uint32_t s_recommendedExp[23] = {
        50, 150, 500, 1000, 1800, 3000, 4200, 6000, 8800, 11500,
        13800, 15500, 17200, 19500, 21800, 24500, 27200, 30500, 33800, 37500,
        41200, 45500, 50000
    };

    static const char *s_coupleTitleNames[23] = {
        "恋爱新手",
        "平凡情侣",
        "中坚情侣",
        "老手情侣",
        "恋人本格派",
        "熟练的二人",
        "资深情侣",
        "恩爱的二人",
        "最佳拍档",
        "永远的二人",
        "究极的二人",
        "空前绝后的二人",
        "绝代的二人",
        "前所未闻的二人",
        "天下无双的二人",
        "举世瞩目的二人",
        "太阳系的二人",
        "银河系的二人",
        "全宇宙的二人",
        "传说的二人",
        "完美无瑕的二人",
        "永恒的二人",
        "最爱的二人"
    };

    int Dict::getCoupleRank(uint32_t exp)
    {
        for (int i = 0; i < 22; ++i)
        {
            if (s_rankThresholds[i] > exp)
                return i + 1;
        }
        return 23;
    }

    std::string Dict::getCoupleTitleName(int rank)
    {
        if (rank < 1 || rank > 23) return getTxt("无");
        return getTxt(s_coupleTitleNames[rank - 1]);
    }

    uint32_t Dict::getCoupleRankExp(int rank)
    {
        if (rank < 1) rank = 1;
        if (rank > 23) rank = 23;
        return s_recommendedExp[rank - 1];
    }

    int Dict::getCoupleTitleCount()
    {
        return 23;
    }
}

