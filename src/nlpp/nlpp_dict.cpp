#include "nlpp_dict.h"
#include "ui.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace nlpp
{
    bool Dict::s_loaded = false;
    std::vector<ReadingEntry> Dict::s_readings;
    std::unordered_map<uint16_t, size_t> Dict::s_readingMap;

    static const char *s_prefectures[] = {
        "北海道", "青森県", "岩手県", "宮城県", "秋田県", "山形県", "福島県",
        "茨城県", "栃木県", "群馬県", "埼玉県", "千葉県", "東京都", "神奈川県",
        "新潟県", "富山県", "石川県", "福井県", "山梨県", "長野県", "岐阜県",
        "静岡県", "愛知県", "三重県", "滋賀県", "京都府", "大阪府", "兵庫県",
        "奈良県", "和歌山県", "鳥取県", "島根県", "岡山県", "広島県", "山口県",
        "徳島県", "香川県", "愛媛県", "高知県", "福岡県", "佐賀県", "長崎県",
        "熊本県", "大分県", "宮崎県", "鹿児島県", "沖縄県"
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
        {0,  "基本",                   "カジュアル", "デート",   false, false},
        {1,  "自宅",                   "カジュアル", "自宅",     false, false},
        {2,  "かわいいＡ",             "かわいい",   "デート",   false, false},
        {3,  "カジュアルＡ",           "カジュアル", "デート",   false, false},
        {4,  "清楚Ａ",                 "清楚",       "デート",   false, false},
        {5,  "アップ [水着・着物]",    "清楚",       "イベント", false, false},
        {6,  "パーティー",             "清楚",       "イベント", false, false},
        {7,  "ライブ",                 "かわいい",   "イベント", true,  false},
        {8,  "ゴスロリ",               "かわいい",   "イベント", true,  false},
        {9,  "ナチュラル",             "清楚",       "イベント", true,  true},
        {14, "かわいいＢ",             "かわいい",   "デート",   false, false},
        {15, "カジュアルＢ",           "カジュアル", "デート",   false, false},
        {16, "清楚Ｂ",                 "清楚",       "デート",   false, false},
        {17, "かわいいＣ",             "かわいい",   "デート",   false, false},
        {18, "カジュアルＣ",           "カジュアル", "デート",   false, false},
        {19, "清楚Ｃ",                 "清楚",       "デート",   false, false},
        {20, "NEW",                    "かわいい",   "デート",   false, false}
    };

    static const CoupleTitleDef s_coupleTitles[23] = {
        {1,  "恋愛ビギナー",       "恋爱新手",       0,     1},
        {2,  "平凡カップル",       "平凡情侣",       100,   2},
        {3,  "中堅カップル",       "中坚情侣",       250,   3},
        {4,  "ベテランカップル",   "老手情侣",       750,   4},
        {5,  "恋人本格派",         "恋人本格派",     1200,  5},
        {6,  "熟練のふたり",       "熟练的二人",     2500,  6},
        {7,  "シニアカップル",     "资深情侣",       3500,  7},
        {8,  "二人はラブラブ",     "卿卿我我",       5000,  8},
        {9,  "ベストパートナー",   "最佳拍档",       7500,  9},
        {10, "とわのふたり",       "永久的二人",     9999,  10},
        {11, "究極のふたり",       "究极的二人",     13000, 11},
        {12, "空前絶後のふたり",   "空前绝后的二人", 14500, 12},
        {13, "不出世のふたり",     "旷世绝代的二人", 16250, 13},
        {14, "前代未聞のふたり",   "前所未闻的二人", 18250, 14},
        {15, "天下無双のふたり",   "天下无双的二人", 20500, 15},
        {16, "世界のふたり",       "举世瞩目的二人", 23000, 16},
        {17, "太陽系のふたり",     "太阳系的二人",   25750, 17},
        {18, "銀河系のふたり",     "银河系的二人",   28750, 18},
        {19, "宇宙のふたり",       "宇宙级的二人",   32000, 19},
        {20, "伝説のふたり",       "传说的二人",     35500, 20},
        {21, "完全無欠のふたり",   "完美无瑕的二人", 39250, 21},
        {22, "永遠のふたり",       "永远的二人",     43250, 22},
        {23, "最愛のふたり",       "最爱的二人",     47500, 23}
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
                s_readings[i].text = &stringsPool[offset];
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
        const ReadingEntry *entry = getReadingById(id);
        if (entry) return entry->text;
        char buf[16];
        snprintf(buf, sizeof(buf), "[%u]", id);
        return std::string(buf);
    }

    std::vector<const ReadingEntry *> Dict::filterReadings(uint8_t slotMask, uint8_t heroineMask, const std::string &search)
    {
        if (!s_loaded) init();
        std::vector<const ReadingEntry *> result;
        result.reserve(256);

        for (const auto &entry : s_readings)
        {
            if (slotMask && !(entry.slot_mask & slotMask)) continue;
            if (heroineMask && !(entry.heroine_mask & heroineMask)) continue;
            if (!search.empty())
            {
                if (entry.text.find(search) == std::string::npos) continue;
            }
            result.push_back(&entry);
        }
        return result;
    }

    const char *Dict::getPrefectureName(uint16_t id)
    {
        if (id < 47) return s_prefectures[id];
        return "未設定";
    }

    int Dict::getPrefectureCount()
    {
        return 47;
    }

    const char *Dict::getBloodTypeName(BloodType blood)
    {
        if ((size_t)blood < 4) return s_bloodTypes[(size_t)blood];
        return "A型";
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

    const std::vector<HairStyleDef> &Dict::getAvailableStyles(HeroineID heroine, uint8_t hairLength)
    {
        static std::vector<HairStyleDef> cached;
        cached.clear();

        // Length stage 3 (or 2 for Nene) is Long
        bool isLong = false;
        if (heroine == HEROINE_MANAKA && hairLength == 3) isLong = true;
        else if (heroine == HEROINE_RINKO && hairLength == 3) isLong = true;
        else if (heroine == HEROINE_NENE && hairLength == 2) isLong = true;

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
            if (s.style_id == styleId) return s.name;
        }
        return "基本";
    }

    std::string Dict::getHairLengthName(HeroineID heroine, uint8_t stage)
    {
        // 0: Long, 1: Medium, 2: Short
        int cat = 1;
        switch (heroine)
        {
            case HEROINE_MANAKA:
                if (stage >= 3) cat = 0;
                else if (stage == 2 || stage == 1) cat = 1;
                else cat = 2;
                break;
            case HEROINE_RINKO:
                if (stage >= 3) cat = 0;
                else if (stage == 2 || stage == 1) cat = 1;
                else cat = 2;
                break;
            case HEROINE_NENE:
                if (stage >= 2) cat = 0;
                else if (stage == 1) cat = 1;
                else cat = 2;
                break;
            default:
                cat = 1;
                break;
        }

        if (cat == 0) return std::string("ロング ") + getTxt("长发");
        if (cat == 1) return std::string("ミドル ") + getTxt("中长发");
        return std::string("ショート ") + getTxt("短发");
    }

    std::vector<uint8_t> Dict::getValidLengthStages(HeroineID heroine)
    {
        switch (heroine)
        {
            case HEROINE_MANAKA: return {3, 2, 0};
            case HEROINE_RINKO:  return {3, 2, 0};
            case HEROINE_NENE:   return {2, 1, 0};
            default:             return {0};
        }
    }

    const CoupleTitleDef &Dict::getCoupleTitle(uint8_t rank)
    {
        if (rank >= 1 && rank <= 23) return s_coupleTitles[rank - 1];
        return s_coupleTitles[0];
    }

    const CoupleTitleDef &Dict::getCoupleTitleByExp(uint32_t exp)
    {
        for (int i = 22; i >= 0; --i)
        {
            if (exp >= s_coupleTitles[i].exp_min)
            {
                return s_coupleTitles[i];
            }
        }
        return s_coupleTitles[0];
    }

    int Dict::getCoupleTitleCount()
    {
        return 23;
    }
}
