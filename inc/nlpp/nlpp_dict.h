#pragma once

#include "nlpp_types.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace nlpp
{
    class Dict
    {
    public:
        static bool init();
        static void exit();

        // Reading lookup & filtering
        static const ReadingEntry *getReadingById(uint16_t id);
        static std::string getReadingText(uint16_t id);
        static std::string getReadingDisplay(uint16_t id);
        static std::string getReadingSlotDisplay(uint8_t slotIndex, uint16_t id);
        static std::vector<const ReadingEntry *> filterReadings(uint8_t slotMask, uint8_t heroineMask, const std::string &search = "");

        // Prefectures (0..46)
        static const char *getPrefectureName(uint16_t id);
        static int getPrefectureCount();

        // Blood types
        static const char *getBloodTypeName(BloodType blood);

        // Hair colors (0..19)
        static const HairColorDef &getHairColor(uint8_t id);
        static int getHairColorCount();
        static void getDefaultHairRGB(HeroineID heroine, uint8_t &r, uint8_t &g, uint8_t &b);
        static uint8_t matchClosestColorId(uint8_t r, uint8_t g, uint8_t b);
        static std::string getColorHighlightDesc(uint8_t colorId);

        // Hair styles
        static const std::vector<HairStyleDef> &getAvailableStyles(HeroineID heroine, uint8_t hairLength);
        static const char *getStyleName(uint8_t styleId);
        static uint16_t getHairModelId(HeroineID heroine, uint8_t hairLength, uint8_t styleId);

        // Hair length stage names
        static std::string getHairLengthName(HeroineID heroine, uint8_t stage);
        static std::vector<uint8_t> getValidLengthStages(HeroineID heroine);

        // Couple titles (Rank 1..23)
        static int getCoupleRank(uint32_t exp);
        static std::string getCoupleTitleName(int rank);
        static uint32_t getCoupleRankExp(int rank);
        static int getCoupleTitleCount();

    private:
        static bool s_loaded;
        static std::vector<ReadingEntry> s_readings;
        static std::unordered_map<uint16_t, size_t> s_readingMap;
    };
}
