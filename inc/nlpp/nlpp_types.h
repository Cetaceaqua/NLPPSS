#pragma once

#include <3ds.h>
#include <string>
#include <vector>
#include <cstdint>

namespace nlpp
{
    // NEW LovePlus+ Title ID
    constexpr uint64_t TITLE_ID = 0x00040000000F4E00ULL;

    // Slot mappings:
    // Slot 0 (in-game Slot 1), Slot 1 (in-game Slot 2), Slot 2 (in-game Slot 3)
    constexpr int NUM_SLOTS = 3;
    constexpr int SLOT_STRIDE = 25;

    constexpr int FILE_GLOBAL_CONFIG   = 0;
    constexpr int FILE_GLOBAL_GALLERY  = 3; // savedata3 (24,064 bytes)
    constexpr int FILE_BASE_CARD       = 6; // savedata6/31/56 (115 bytes)
    constexpr int FILE_BASE_PROFILE_CARD = 7; // savedata7/32/57 (170 bytes)
    constexpr int FILE_BASE_PROFILE    = 8; // savedata8/33/58 (198 bytes)
    constexpr int FILE_BASE_STATS      = 9; // savedata9/34/59 (960 bytes)
    constexpr int FILE_BASE_TIMELINE   = 12; // savedata12/37/62 (333 bytes)
    constexpr int FILE_BASE_WARDROBE   = 13; // savedata13/38/63 (5,120 bytes)
    constexpr int FILE_BASE_PHOTO      = 14; // savedata14/39/64 (7,756 bytes)
    constexpr int FILE_BASE_GIRLFRIEND = 18; // savedata18/43/68 (1,508 bytes)
    constexpr int FILE_BASE_RELATION   = 21; // savedata21/46/71 (16,135 bytes)

    inline int getSlotFile(int baseFile, int slot)
    {
        return baseFile + slot * SLOT_STRIDE;
    }

    // Heroine IDs
    enum HeroineID : int8_t
    {
        HEROINE_MANAKA = 0, // 高嶺 愛花
        HEROINE_RINKO  = 1, // 小早川 凛子
        HEROINE_NENE   = 2, // 姉崎 寧々
        HEROINE_NONE   = -1
    };

    // Blood Types
    enum BloodType : uint8_t
    {
        BLOOD_A  = 0,
        BLOOD_B  = 1,
        BLOOD_O  = 2,
        BLOOD_AB = 3
    };

    // Reading Slot Types
    enum ReadingSlot : uint8_t
    {
        READING_SLOT_LAST_NAME  = 0, // 0x281 LastNameReading (ぷれいや)
        READING_SLOT_FIRST_NAME = 1, // 0x285 FirstNameReading (たろう)
        READING_SLOT_SUFFIX     = 2, // 0x289 SuffixReading (たろーくん / ちゃん)
        READING_SLOT_PRESET     = 3, // 0x28D PresetReading (ダーリン, 先輩, etc.)
        READING_SLOT_CUSTOM     = 4  // 0x291 CustomReading (30000+, 40000+, 50000+)
    };

    // Reading entry in romfs binary dict
    struct ReadingEntry
    {
        uint16_t id;
        uint8_t slot_mask;    // bit0: Slot0, bit1: Slot1, bit2: Slot2, bit3: Slot3, bit4: Slot4
        uint8_t heroine_mask; // bit0: Manaka, bit1: Rinko, bit2: Nene
        std::string text;
        std::string romaji;
    };

    // Hair color structure
    struct HairColorDef
    {
        uint8_t id;
        const char *category;
        const char *tone;
        uint8_t r, g, b;
    };

    // Hair style definition
    struct HairStyleDef
    {
        uint8_t style_id;
        const char *name;
        const char *attribute;
        const char *scene;
        bool long_only;
        bool manaka_only;
    };

    // 23 Couple Titles
    struct CoupleTitleDef
    {
        uint8_t rank;
        const char *name_jp;
        const char *name_zh;
        uint32_t exp_min;
        uint8_t medals;
    };

    // Player Profile Data (savedata8/33/58)
    struct PlayerProfile
    {
        bool empty = true;
        std::string firstName; // 12B max
        std::string lastName;  // 12B max
        std::string hn;        // 12B max (communication nickname)
        uint8_t birthMonth = 1;
        uint8_t birthDay = 1;
        BloodType blood = BLOOD_A;
        uint16_t prefecture = 0; // 0..46, 0xFFFF is empty
    };

    // Boyfriend Stats & System Data (savedata9/34/59)
    struct BoyfriendStats
    {
        uint16_t fitness = 0;      // 0..255 (0x008)
        uint16_t knowledge = 0;    // 0..255 (0x016)
        uint16_t sensibility = 0;  // 0..255 (0x024)
        uint16_t charm = 0;        // 0..255 (0x032)
        uint32_t rich = 0;         // 0..999,999 (0x2B0)
        uint32_t todo = 0;         // 0..9,999 (0x2D8)

        // Reading containers (0x281..0x294)
        uint32_t readings[5] = {2935, 470, 750, 958, 0};
        uint32_t currentSelector = 0; // 0..4 (0x295)

        // Camera Equipments (0x2FC..0x337)
        int16_t cameraBody = -1;      // Slot 0 (0x300)
        int16_t cameraLens = -1;      // Slot 1 (0x302)
        int16_t cameraFilter = -1;    // Slot 2 (0x304, -1 means no filter)
        int16_t cameraAccessory = -1; // Slot 3 (0x306)
        int16_t cameraExternal = -1;  // Slot 4 (0x308)
        uint8_t equipMask = 0;        // 0x318 (Bit 2 controls filter)
    };

    // Girlfriend Dynamic Status (savedata18/43/68)
    struct GirlfriendStatus
    {
        HeroineID heroine = HEROINE_NONE;
        uint8_t mode = 0;            // 2 = lover mode, 1 = friend mode, 0 = uninit
        uint8_t affection = 128;     // 0..255 (0x243)
        uint32_t coupleExp = 0;      // 0..99,250 (0x2AC)

        // Hairstyle and Appearance (0x464..0x4B2)
        uint8_t hairLength = 0;      // 0x466
        uint8_t hairStyleId = 0;     // 0x468
        uint8_t reservedStyleId = 0xFF; // 0x469
        uint8_t hairTasteTarget = 0; // 0x46A (0=清楚, 1=かわいい, 2=カジュアル)

        // Hair color triad
        uint8_t hairColorId = 0;     // 0x50A (0=default, 1..19=palette)
        uint8_t nextHairColorId = 0; // 0x50B
        uint8_t hairR = 38, hairG = 38, hairB = 41; // 0x4B3..0x4B5
        uint8_t nextHairR = 38, nextHairG = 38, nextHairB = 41; // 0x4B6..0x4B8
        uint8_t hairColorActive = 0xFF; // 0x54C (0x00=active dye, 0xFF=default)

        // Personality Sliders (0x2D0..0x2D3)
        uint16_t sliderX = 500;      // 0x2D0 (0..1000)
        uint16_t sliderY = 500;      // 0x2D2 (0..1000)
        uint16_t sliderZ = 0;        // Reserved
    };

    // Timeline & Anniversary Data (savedata12/37/62)
    struct TimelineData
    {
        uint32_t timelineDays = 1;  // 0x04 (Total calendar days, displayed as %d日)
        uint32_t activeBaseline = 1;// 0x08
        uint16_t maxAbsence = 0;    // 0x14 (Longest offline interval)
        uint32_t loverTimeline = 0; // 0x2E (Days since confession)
        uint32_t homeTimeline = 0;  // 0x32 (Days since home mode)
        uint32_t datingDays = 1;    // 0x39 (Actual active companion dating days)
    };

    // Overall Slot Data
    struct SlotData
    {
        int slotIndex = 0;
        bool empty = true;
        PlayerProfile profile;
        BoyfriendStats stats;
        GirlfriendStatus girlfriend;
        TimelineData timeline;

        bool isFriendMode() const
        {
            return !empty && (girlfriend.mode == 1 || girlfriend.heroine == HEROINE_NONE);
        }
    };
}
