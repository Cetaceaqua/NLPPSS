#include "nlpp_save.h"
#include "nlpp_dict.h"
#include "fs.h"
#include "util.h"
#include "cfg.h"
#include "lodepng.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace nlpp
{
    SaveManager g_saveMgr;

    SaveManager::SaveManager()
    {
        for (int i = 0; i < 81; ++i)
        {
            m_fileLoaded[i] = false;
            m_fileDirty[i] = false;
        }
    }

    SaveManager::~SaveManager()
    {
        exit();
    }

    bool SaveManager::init()
    {
        Dict::init();
        detectSources();
        if (m_source != SOURCE_NONE)
        {
            return openSaveArchive();
        }
        return false;
    }

    void SaveManager::exit()
    {
        closeSaveArchive();
        for (int i = 0; i < 81; ++i)
        {
            m_files[i].clear();
            m_fileLoaded[i] = false;
            m_fileDirty[i] = false;
        }
        m_modified = false;
    }

    static std::string sanitizeUtf8(const uint8_t *buf, size_t maxLen)
    {
        size_t len = 0;
        while (len < maxLen && buf[len] != 0) ++len;
        if (len == 0) return "";

        size_t validLen = 0;
        size_t i = 0;
        while (i < len)
        {
            uint8_t c = buf[i];
            size_t charLen = 1;
            if ((c & 0x80) == 0) charLen = 1;
            else if ((c & 0xE0) == 0xC0) charLen = 2;
            else if ((c & 0xF0) == 0xE0) charLen = 3;
            else if ((c & 0xF8) == 0xF0) charLen = 4;
            else break;

            if (i + charLen > len) break;

            bool seqOk = true;
            for (size_t k = 1; k < charLen; ++k)
            {
                if ((buf[i + k] & 0xC0) != 0x80) { seqOk = false; break; }
            }
            if (!seqOk) break;

            i += charLen;
            validLen = i;
        }

        return std::string((const char *)buf, validLen);
    }

    bool SaveManager::detectSources()
    {
        m_hasGameCard = false;
        m_hasSD = false;

        // 1. Check GameCard with card slot insertion check
        bool ins = false;
        Result res = FSUSER_CardSlotIsInserted(&ins);
        if (R_SUCCEEDED(res) && ins)
        {
            FS_CardType cardType;
            res = FSUSER_GetCardType(&cardType);
            if (R_SUCCEEDED(res) && cardType == CARD_CTR)
            {
                u32 count = 0;
                res = AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
                if (R_SUCCEEDED(res) && count > 0)
                {
                    uint64_t cartID = 0;
                    res = AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, 1, &cartID);
                    if (R_SUCCEEDED(res) && cartID == TITLE_ID)
                    {
                        FS_Archive testArch = 0;
                        res = FSUSER_OpenArchive(&testArch, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""));
                        if (R_SUCCEEDED(res))
                        {
                            m_hasGameCard = true;
                            FSUSER_CloseArchive(testArch);
                        }
                    }
                }
            }
        }

        // 2. Check SD Digital installed title (ARCHIVE_USER_SAVEDATA)
        FS_Archive testArch = 0;
        uint32_t path[3] = {MEDIATYPE_SD, (uint32_t)TITLE_ID, (uint32_t)(TITLE_ID >> 32)};
        FS_Path binData = {PATH_BINARY, 12, path};
        res = FSUSER_OpenArchive(&testArch, ARCHIVE_USER_SAVEDATA, binData);
        if (R_SUCCEEDED(res))
        {
            m_hasSD = true;
            FSUSER_CloseArchive(testArch);
        }

        if (m_source == SOURCE_NONE)
        {
            if (m_hasSD) m_source = SOURCE_SD;
            else if (m_hasGameCard) m_source = SOURCE_GAMECARD;
        }

        return (m_hasGameCard || m_hasSD);
    }

    bool SaveManager::switchSource(SaveSource source)
    {
        if (source == m_source) return true;
        closeSaveArchive();
        m_source = source;
        return openSaveArchive();
    }

    bool SaveManager::openSaveArchive()
    {
        if (m_archOpen) closeSaveArchive();

        Result res = -1;
        if (m_source == SOURCE_GAMECARD)
        {
            res = FSUSER_OpenArchive(&m_saveArch, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""));
        }
        else if (m_source == SOURCE_SD)
        {
            uint32_t path[3] = {MEDIATYPE_SD, (uint32_t)TITLE_ID, (uint32_t)(TITLE_ID >> 32)};
            FS_Path binData = {PATH_BINARY, 12, path};
            res = FSUSER_OpenArchive(&m_saveArch, ARCHIVE_USER_SAVEDATA, binData);
        }

        if (R_FAILED(res))
        {
            m_archOpen = false;
            return false;
        }

        m_archOpen = true;
        return reloadAll();
    }

    bool SaveManager::reloadAll()
    {
        if (!m_archOpen) return false;
        loadAllFiles();

        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            parseSlot(i);
        }
        m_modified = false;
        return true;
    }

    void SaveManager::closeSaveArchive()
    {
        if (m_archOpen)
        {
            FSUSER_CloseArchive(m_saveArch);
            m_archOpen = false;
        }
    }

    bool SaveManager::loadAllFiles()
    {
        if (!m_archOpen) return false;

        for (int i = 0; i < 81; ++i)
        {
            m_files[i].clear();
            m_fileLoaded[i] = false;
            m_fileDirty[i] = false;

            char fname[32];
            snprintf(fname, sizeof(fname), "/savedata%d", i);

            Handle fileHandle;
            Result res = FSUSER_OpenFile(&fileHandle, m_saveArch, fsMakePath(PATH_ASCII, fname), FS_OPEN_READ, 0);
            if (R_SUCCEEDED(res))
            {
                u64 fileSize = 0;
                FSFILE_GetSize(fileHandle, &fileSize);
                if (fileSize > 0 && fileSize < 1024 * 1024) // < 1MB
                {
                    m_files[i].resize(fileSize);
                    u32 bytesRead = 0;
                    FSFILE_Read(fileHandle, &bytesRead, 0, m_files[i].data(), fileSize);
                    m_fileLoaded[i] = true;
                }
                FSFILE_Close(fileHandle);
            }
        }
        return true;
    }

    bool SaveManager::selectSlot(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return false;
        m_currentSlot = slotIndex;
        return true;
    }

    static bool isSlotDataEmpty(const SlotData &sd)
    {
        // 1. Unset or invalid prefecture (valid indices in Japan are 0..46; 0xFFFF indicates empty/uninitialized)
        if (sd.profile.prefecture == 0xFFFF || sd.profile.prefecture > 46)
            return true;

        // 2. Both first and last names empty
        if (sd.profile.firstName.empty() && sd.profile.lastName.empty())
            return true;

        // 3. Name placeholder: lastName is empty and firstName is "*PR*@"
        if (sd.profile.lastName.empty() && sd.profile.firstName == "*PR*@")
            return true;

        // 4. Factory default placeholder group:
        // lastName == "ぷれいや" && firstName == "太郎" && hn == "ぷれ" &&
        // birthMonth == 1 && birthDay == 1 && blood == 0 && prefecture == 12 &&
        // lastNameReadingId == 0 && firstNameReadingId == 0
        if (sd.profile.lastName == "ぷれいや" && sd.profile.firstName == "太郎" &&
            sd.profile.hn == "ぷれ" && sd.profile.birthMonth == 1 && sd.profile.birthDay == 1 &&
            (int)sd.profile.blood == 0 && sd.profile.prefecture == 12 &&
            sd.stats.readings[0] == 0 && sd.stats.readings[1] == 0)
        {
            return true;
        }

        return false;
    }

    bool SaveManager::parseSlot(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return false;
        SlotData &sd = m_slots[slot];
        sd = SlotData();
        sd.slotIndex = slot;

        int cardFile  = getSlotFile(FILE_BASE_CARD, slot);
        int profFile  = getSlotFile(FILE_BASE_PROFILE, slot);
        int statsFile = getSlotFile(FILE_BASE_STATS, slot);
        int gfFile    = getSlotFile(FILE_BASE_GIRLFRIEND, slot);
        int timeFile  = getSlotFile(FILE_BASE_TIMELINE, slot);

        // 0. Primary Empty Slot Detection: Check Slot Info Card (savedata6/31/56, 115B)
        bool cardEmpty = false;
        bool cardDetermined = false;
        if (m_fileLoaded[cardFile] && m_files[cardFile].size() >= 12)
        {
            uint8_t activeFlag = m_files[cardFile][0x04];
            int32_t heroineTarget = *(const int32_t *)&m_files[cardFile][0x08];
            if (activeFlag == 0x00 && heroineTarget == -1)
            {
                cardEmpty = true;
                cardDetermined = true;
            }
            else if (activeFlag == 0x01)
            {
                cardEmpty = false;
                cardDetermined = true;
            }
        }

        // 1. Profile (savedata8/33/58, 198B)
        if (!m_fileLoaded[profFile] || m_files[profFile].size() < 198)
        {
            sd.empty = true;
            sd.profile.empty = true;
            return false;
        }

        const uint8_t *pbuf = m_files[profFile].data();
        sd.profile.prefecture = *(const uint16_t *)&pbuf[0x90];

        // Names (12B max, null-terminated UTF-8, sanitize against incomplete multi-byte sequences)
        sd.profile.firstName = sanitizeUtf8(&pbuf[0x04], 12);
        sd.profile.lastName  = sanitizeUtf8(&pbuf[0x14], 12);
        sd.profile.hn        = sanitizeUtf8(&pbuf[0x64], 12);

        sd.profile.birthMonth = pbuf[0x84];
        sd.profile.birthDay   = pbuf[0x85];
        sd.profile.blood      = (BloodType)pbuf[0x8C];

        // 2. Stats (savedata9/34/59, 960B)
        if (m_fileLoaded[statsFile] && m_files[statsFile].size() >= 960)
        {
            const uint8_t *sbuf = m_files[statsFile].data();
            sd.stats.fitness     = *(const uint16_t *)&sbuf[0x00C];
            sd.stats.knowledge   = *(const uint16_t *)&sbuf[0x01A];
            sd.stats.sensibility = *(const uint16_t *)&sbuf[0x028];
            sd.stats.charm       = *(const uint16_t *)&sbuf[0x036];
            sd.stats.rich        = *(const uint32_t *)&sbuf[0x2B0];
            sd.stats.todo        = *(const uint32_t *)&sbuf[0x2D8];

            for (int r = 0; r < 5; ++r)
            {
                sd.stats.readings[r] = *(const uint32_t *)&sbuf[0x281 + r * 4];
            }
            sd.stats.currentSelector = *(const uint32_t *)&sbuf[0x295];

            sd.stats.cameraBody      = *(const int16_t *)&sbuf[0x300];
            sd.stats.cameraLens      = *(const int16_t *)&sbuf[0x302];
            sd.stats.cameraFilter    = *(const int16_t *)&sbuf[0x304];
            sd.stats.cameraAccessory = *(const int16_t *)&sbuf[0x306];
            sd.stats.cameraExternal  = *(const int16_t *)&sbuf[0x308];
            sd.stats.equipMask       = sbuf[0x318];
            if ((sd.stats.equipMask & 0x04) == 0)
            {
                sd.stats.cameraFilter = -1;
            }
        }

        // 3. Girlfriend (savedata18/43/68, 1508B)
        if (m_fileLoaded[gfFile] && m_files[gfFile].size() >= 1508)
        {
            const uint8_t *gbuf = m_files[gfFile].data();
            sd.girlfriend.mode = gbuf[0x04];
            sd.girlfriend.heroine = (HeroineID)gbuf[0x08];
            sd.girlfriend.affection = gbuf[0x243];
            sd.girlfriend.coupleExp = *(const uint32_t *)&gbuf[0x2AC];

            sd.girlfriend.hairLength = gbuf[0x466];
            sd.girlfriend.hairStyleId = gbuf[0x468];
            sd.girlfriend.reservedStyleId = gbuf[0x469];
            sd.girlfriend.hairTasteTarget = gbuf[0x46A];

            sd.girlfriend.hairColorId = gbuf[0x50A];
            sd.girlfriend.nextHairColorId = gbuf[0x50B];
            sd.girlfriend.hairR = gbuf[0x4B3];
            sd.girlfriend.hairG = gbuf[0x4B4];
            sd.girlfriend.hairB = gbuf[0x4B5];
            sd.girlfriend.nextHairR = gbuf[0x4B6];
            sd.girlfriend.nextHairG = gbuf[0x4B7];
            sd.girlfriend.nextHairB = gbuf[0x4B8];
            sd.girlfriend.hairColorActive = gbuf[0x54C];

            // Section 7.6 & 7.9: Physical 2D personality coordinates at 0x2D0 ~ 0x2D3
            sd.girlfriend.sliderX = *(const uint16_t *)&gbuf[0x2D0];
            sd.girlfriend.sliderY = *(const uint16_t *)&gbuf[0x2D2];
            sd.girlfriend.sliderZ = 0;
        }

        // 4. Timeline (savedata12/37/62, 333B)
        if (m_fileLoaded[timeFile] && m_files[timeFile].size() >= 333)
        {
            const uint8_t *tbuf = m_files[timeFile].data();
            sd.timeline.timelineDays   = *(const uint32_t *)&tbuf[0x04];
            sd.timeline.activeBaseline = *(const uint32_t *)&tbuf[0x08];
            sd.timeline.maxAbsence     = *(const uint16_t *)&tbuf[0x14];
            sd.timeline.loverTimeline  = *(const uint32_t *)&tbuf[0x2E];
            sd.timeline.homeTimeline   = *(const uint32_t *)&tbuf[0x32];
            sd.timeline.datingDays     = *(const uint32_t *)&tbuf[0x39];
        }

        // Final Empty Slot Detection Rule (Section 1.3):
        // Primary: Card activeFlag == 0x00 && heroineTarget == -1
        if (cardDetermined)
        {
            sd.empty = cardEmpty;
            sd.profile.empty = cardEmpty;
        }
        else
        {
            // Fallback to secondary criteria if card is unavailable
            if (isSlotDataEmpty(sd))
            {
                sd.empty = true;
                sd.profile.empty = true;
            }
            else
            {
                sd.empty = false;
                sd.profile.empty = false;
            }
        }

        return true;
    }

    bool SaveManager::serializeSlot(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return false;
        SlotData &sd = m_slots[slot];

        int cardFile  = getSlotFile(FILE_BASE_CARD, slot);
        int profFile  = getSlotFile(FILE_BASE_PROFILE, slot);
        int statsFile = getSlotFile(FILE_BASE_STATS, slot);
        int gfFile    = getSlotFile(FILE_BASE_GIRLFRIEND, slot);
        int timeFile  = getSlotFile(FILE_BASE_TIMELINE, slot);

        time_t rawtime;
        time(&rawtime);
        struct tm *ti = localtime(&rawtime);

        // 0. Slot Info Card (savedata6/31/56, 115B)
        if (m_files[cardFile].size() < 115) m_files[cardFile].resize(115, 0);
        uint8_t *cbuf = m_files[cardFile].data();
        *(uint32_t *)&cbuf[0x00] = 4; // Version 4

        if (sd.empty)
        {
            cbuf[0x04] = 0x00; // Empty slot flag
            *(int32_t *)&cbuf[0x08] = -1; // Unbound heroine
            m_fileDirty[cardFile] = true;

            // Profile
            if (m_files[profFile].size() < 198) m_files[profFile].resize(198, 0);
            uint8_t *pbuf = m_files[profFile].data();
            *(uint32_t *)&pbuf[0x00] = 1;
            *(uint32_t *)&pbuf[0x80] = 2000;
            *(uint16_t *)&pbuf[0x90] = 0xFFFF;
            m_fileDirty[profFile] = true;
            return true;
        }

        cbuf[0x04] = 0x01; // Active slot flag
        *(int32_t *)&cbuf[0x08] = (sd.girlfriend.heroine >= 0) ? (int32_t)sd.girlfriend.heroine : 0;
        // Section 9.5: Anti-cold-war timestamp sync in Card at 0x18 ~ 0x23
        if (ti)
        {
            *(uint32_t *)&cbuf[0x18] = (uint32_t)(ti->tm_year + 1900);
            cbuf[0x1C] = (uint8_t)(ti->tm_mon + 1);
            cbuf[0x1D] = (uint8_t)ti->tm_mday;
            cbuf[0x1E] = (uint8_t)ti->tm_wday;
            cbuf[0x1F] = (uint8_t)ti->tm_hour;
            cbuf[0x20] = (uint8_t)ti->tm_min;
            cbuf[0x21] = (uint8_t)ti->tm_sec;
            *(uint16_t *)&cbuf[0x22] = 0;
        }
        m_fileDirty[cardFile] = true;

        // 1. Profile (198B)
        if (m_files[profFile].size() < 198) m_files[profFile].resize(198, 0);
        uint8_t *pbuf = m_files[profFile].data();
        *(uint32_t *)&pbuf[0x00] = 1; // Protected header version

        memset(&pbuf[0x04], 0, 12);
        memcpy(&pbuf[0x04], sd.profile.firstName.c_str(), std::min(sd.profile.firstName.length(), (size_t)12));

        memset(&pbuf[0x14], 0, 12);
        memcpy(&pbuf[0x14], sd.profile.lastName.c_str(), std::min(sd.profile.lastName.length(), (size_t)12));

        memset(&pbuf[0x64], 0, 12);
        memcpy(&pbuf[0x64], sd.profile.hn.c_str(), std::min(sd.profile.hn.length(), (size_t)12));

        *(uint32_t *)&pbuf[0x80] = 2000; // Protected birth year
        pbuf[0x84] = sd.profile.birthMonth;
        pbuf[0x85] = sd.profile.birthDay;
        pbuf[0x8C] = (uint8_t)sd.profile.blood;
        *(uint16_t *)&pbuf[0x90] = sd.profile.prefecture;
        m_fileDirty[profFile] = true;

        // 2. Stats (960B)
        if (m_files[statsFile].size() < 960) m_files[statsFile].resize(960, 0);
        uint8_t *sbuf = m_files[statsFile].data();

        // Section 6.1: File and component versions
        *(uint32_t *)&sbuf[0x000] = 3;
        *(uint32_t *)&sbuf[0x004] = 1;

        // 4 stats: fitness, knowledge, sensibility, charm (14B step at 0x08)
        // Section 6.5: Dual-write both current (+0x04) and baseline (+0x06)
        const uint16_t statVals[4] = {
            (uint16_t)std::min(sd.stats.fitness, (uint16_t)255),
            (uint16_t)std::min(sd.stats.knowledge, (uint16_t)255),
            (uint16_t)std::min(sd.stats.sensibility, (uint16_t)255),
            (uint16_t)std::min(sd.stats.charm, (uint16_t)255)
        };
        for (int i = 0; i < 4; ++i)
        {
            uint8_t *statPtr = &sbuf[0x08 + i * 0x0E];
            *(uint32_t *)&statPtr[0x00] = 1; // individual stat version
            *(uint16_t *)&statPtr[0x04] = statVals[i]; // Current value
            *(uint16_t *)&statPtr[0x06] = statVals[i]; // Baseline value
        }

        *(uint32_t *)&sbuf[0x2B0] = std::min(sd.stats.rich, (uint32_t)999999);
        *(uint32_t *)&sbuf[0x2D8] = std::min(sd.stats.todo, (uint32_t)9999);

        for (int r = 0; r < 5; ++r)
        {
            *(uint32_t *)&sbuf[0x281 + r * 4] = sd.stats.readings[r];
        }
        *(uint32_t *)&sbuf[0x295] = sd.stats.currentSelector;

        *(int16_t *)&sbuf[0x300] = sd.stats.cameraBody;
        *(int16_t *)&sbuf[0x302] = sd.stats.cameraLens;
        *(int16_t *)&sbuf[0x304] = sd.stats.cameraFilter;
        *(int16_t *)&sbuf[0x306] = sd.stats.cameraAccessory;
        *(int16_t *)&sbuf[0x308] = sd.stats.cameraExternal;
        sbuf[0x318] = sd.stats.equipMask;

        // Camera Filter Removal - Six-in-One Atomic Standard (Section 6.4.8, 6.5)
        if (sd.stats.cameraFilter == -1 || (sd.stats.equipMask & 0x04) == 0)
        {
            *(int16_t *)&sbuf[0x304] = -1;
            sbuf[0x318] &= ~(0x04);
            *(uint32_t *)&sbuf[0x33C] = 38; // Factory bare lens shutter speed (26 00 00 00)
            *(uint32_t *)&sbuf[0x344] = 20; // Factory bare lens ISO sensitivity (14 00 00 00)
            *(uint32_t *)&sbuf[0x34C] = 9;  // filter_mode_5 (bare lens white balance override A)
            *(uint32_t *)&sbuf[0x350] = 3;  // filter_mode_6 (bare lens white balance override B)
        }
        m_fileDirty[statsFile] = true;

        // 3. Girlfriend (1508B)
        if (m_files[gfFile].size() < 1508) m_files[gfFile].resize(1508, 0);
        uint8_t *gbuf = m_files[gfFile].data();
        gbuf[0x243] = sd.girlfriend.affection;
        // Section 9.4: Couple title/exp is stored at 0x2AC
        *(uint32_t *)&gbuf[0x2AC] = sd.girlfriend.coupleExp;

        uint8_t hairLen = std::min(sd.girlfriend.hairLength, (uint8_t)2);
        gbuf[0x466] = hairLen;
        gbuf[0x468] = sd.girlfriend.hairStyleId;
        gbuf[0x469] = sd.girlfriend.reservedStyleId;
        gbuf[0x46A] = sd.girlfriend.hairTasteTarget;

        // Update hair affinity matrix (0x46B ~ 0x4B2)
        if (sd.girlfriend.hairStyleId < 24)
        {
            gbuf[0x46B + (hairLen * 24 + sd.girlfriend.hairStyleId)] = 5;
        }

        // Section 7.3 & 7.9: Hair color hexad sync
        gbuf[0x50A] = sd.girlfriend.hairColorId;
        gbuf[0x50B] = sd.girlfriend.nextHairColorId;
        gbuf[0x4B3] = sd.girlfriend.hairR;
        gbuf[0x4B4] = sd.girlfriend.hairG;
        gbuf[0x4B5] = sd.girlfriend.hairB;
        gbuf[0x4B6] = sd.girlfriend.nextHairR;
        gbuf[0x4B7] = sd.girlfriend.nextHairG;
        gbuf[0x4B8] = sd.girlfriend.nextHairB;
        gbuf[0x54C] = sd.girlfriend.hairColorActive;

        // Section 7.6 & 7.9: RGB Cache mirror
        gbuf[0x2DE] = sd.girlfriend.hairR;
        gbuf[0x2DF] = sd.girlfriend.hairG;
        gbuf[0x2E0] = sd.girlfriend.hairB;

        // Section 7.2: Active 3D head model ID
        uint16_t modelId = Dict::getHairModelId(sd.girlfriend.heroine, hairLen, sd.girlfriend.hairStyleId);
        if (modelId != 0)
        {
            *(uint16_t *)&gbuf[0x2DA] = modelId;
        }

        // Section 7.6 & 7.9: Personality 2D coordinates at 0x2D0 ~ 0x2D3
        *(uint16_t *)&gbuf[0x2D0] = std::min(sd.girlfriend.sliderX, (uint16_t)1000);
        *(uint16_t *)&gbuf[0x2D2] = std::min(sd.girlfriend.sliderY, (uint16_t)1000);
        m_fileDirty[gfFile] = true;

        // 4. Timeline (333B)
        if (m_files[timeFile].size() < 333) m_files[timeFile].resize(333, 0);
        uint8_t *tbuf = m_files[timeFile].data();
        *(uint32_t *)&tbuf[0x00] = 2; // Component version 2
        *(uint32_t *)&tbuf[0x04] = sd.timeline.timelineDays;
        *(uint32_t *)&tbuf[0x08] = sd.timeline.timelineDays; // Active baseline sync
        if (sd.girlfriend.mode == 2 && sd.timeline.timelineDays > 1)
        {
            *(uint32_t *)&tbuf[0x2E] = sd.timeline.timelineDays - 1; // Lover timeline
            *(uint32_t *)&tbuf[0x32] = (sd.timeline.timelineDays > 3) ? (sd.timeline.timelineDays - 3) : 0; // Home timeline
        }
        else
        {
            *(uint32_t *)&tbuf[0x2E] = 0;
            *(uint32_t *)&tbuf[0x32] = 0;
        }
        *(uint32_t *)&tbuf[0x39] = sd.timeline.datingDays;
        *(uint16_t *)&tbuf[0x14] = 0; // Clear max absence
        tbuf[0x38] = 0; // Clear cold war threshold

        // Section 9.5: Anti-cold-war timestamp sync at 0x3D ~ 0x48
        if (ti)
        {
            *(uint32_t *)&tbuf[0x3D] = (uint32_t)(ti->tm_year + 1900);
            tbuf[0x41] = (uint8_t)(ti->tm_mon + 1);
            tbuf[0x42] = (uint8_t)ti->tm_mday;
            tbuf[0x43] = (uint8_t)ti->tm_wday;
            tbuf[0x44] = (uint8_t)ti->tm_hour;
            tbuf[0x45] = (uint8_t)ti->tm_min;
            tbuf[0x46] = (uint8_t)ti->tm_sec;
            *(uint16_t *)&tbuf[0x47] = 0;
        }

        // Section 9.5: 256-byte zero padding protection (0x4D ~ 0x14C)
        memset(&tbuf[0x4D], 0, 256);
        m_fileDirty[timeFile] = true;

        // Sync appellations to savedata3 and savedata21
        syncAppellations(slot);
        return true;
    }

    bool SaveManager::syncAppellations(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return false;
        const SlotData &sd = m_slots[slot];
        if (sd.empty || sd.girlfriend.heroine == HEROINE_NONE) return false;

        int statsFile = getSlotFile(FILE_BASE_STATS, slot);
        if (!m_fileLoaded[statsFile] || m_files[statsFile].size() < 960) return false;
        const uint8_t *sbuf = m_files[statsFile].data();

        // 1. Synchronize to savedata3 standby models
        if (m_fileLoaded[FILE_GLOBAL_GALLERY] && m_files[FILE_GLOBAL_GALLERY].size() >= 24064)
        {
            uint8_t *g3 = m_files[FILE_GLOBAL_GALLERY].data();
            int h = (int)sd.girlfriend.heroine;
            for (int mode = 0; mode < 6; ++mode)
            {
                uint32_t offset = 0x6ed + (h * 6 + mode) * 770;
                if (offset + 24 <= m_files[FILE_GLOBAL_GALLERY].size())
                {
                    memcpy(&g3[offset], &sbuf[0x281], 24);
                }
            }
            m_fileDirty[FILE_GLOBAL_GALLERY] = true;
        }

        // 2. Synchronize to savedata21 history ring buffer latest frame
        int relFile = getSlotFile(FILE_BASE_RELATION, slot);
        if (m_fileLoaded[relFile] && m_files[relFile].size() >= 16135)
        {
            uint8_t *rbuf = m_files[relFile].data();
            int h = (int)sd.girlfriend.heroine;
            int startRec = h * 40;
            int maxSeqRec = -1;
            uint32_t maxSeq = 0;

            for (int r = 0; r < 40; ++r)
            {
                uint32_t recOff = 12 + (startRec + r) * 71;
                if (recOff + 71 <= m_files[relFile].size())
                {
                    uint32_t seq = *(const uint32_t *)&rbuf[recOff + 4];
                    if (seq > maxSeq || maxSeqRec == -1)
                    {
                        maxSeq = seq;
                        maxSeqRec = r;
                    }
                }
            }

            if (maxSeqRec != -1)
            {
                uint32_t latestOff = 12 + (startRec + maxSeqRec) * 71;
                uint32_t selector = sd.stats.currentSelector;
                uint32_t readingId = sd.stats.readings[selector < 5 ? selector : 0];

                rbuf[latestOff + 0x0F] = (uint8_t)selector;
                *(uint32_t *)&rbuf[latestOff + 0x10] = readingId;
                m_fileDirty[relFile] = true;
            }
        }
        return true;
    }

    bool SaveManager::cloneSlot(int srcSlot, int dstSlot)
    {
        if (srcSlot < 0 || srcSlot >= NUM_SLOTS || dstSlot < 0 || dstSlot >= NUM_SLOTS) return false;
        if (srcSlot == dstSlot) return true;

        serializeSlot(srcSlot);

        // Section 10.4: Copy all 25 files starting from slot card (file 6)
        for (int i = 0; i < SLOT_STRIDE; ++i)
        {
            int srcF = 6 + srcSlot * SLOT_STRIDE + i;
            int dstF = 6 + dstSlot * SLOT_STRIDE + i;
            if (srcF < 81 && dstF < 81 && m_fileLoaded[srcF])
            {
                m_files[dstF] = m_files[srcF];
                m_fileLoaded[dstF] = true;
                m_fileDirty[dstF] = true;
            }
        }

        // Section 10.4: Global gallery option appellation pointer sync (savedata3 0x3A5A ~ 0x3A5C)
        if (m_fileLoaded[FILE_GLOBAL_GALLERY] && m_files[FILE_GLOBAL_GALLERY].size() >= 0x3A5D)
        {
            uint8_t *gbuf = m_files[FILE_GLOBAL_GALLERY].data();
            HeroineID h = m_slots[srcSlot].girlfriend.heroine;
            if (h >= HEROINE_MANAKA && h <= HEROINE_NENE)
            {
                gbuf[0x3A5A + (int)h] = (uint8_t)dstSlot;
                m_fileDirty[FILE_GLOBAL_GALLERY] = true;
            }
        }

        parseSlot(dstSlot);
        m_modified = true;
        return true;
    }

    bool SaveManager::clearSlot(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return false;

        // Try loading from factory clean template (romfs/template/empty_slot.bin)
        const char *candidates[] = {
            "romfs:/template/empty_slot.bin",
            "romfs/template/empty_slot.bin",
            "d:/Files/GitHub Desktop/Cetaceaqua/NLPPSS/romfs/template/empty_slot.bin"
        };

        FILE *f = nullptr;
        for (const char *path : candidates)
        {
            f = fopen(path, "rb");
            if (f) break;
        }

        bool loadedFromTemplate = false;
        if (f)
        {
            char magic[4];
            uint32_t count = 0;
            if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "NLES", 4) == 0 &&
                fread(&count, 4, 1, f) == 1 && count == 25)
            {
                struct Entry { uint32_t off; uint32_t size; } entries[25];
                if (fread(entries, sizeof(Entry), 25, f) == 25)
                {
                    bool allOk = true;
                    for (int k = 0; k < 25; ++k)
                    {
                        int targetF = 6 + slot * SLOT_STRIDE + k;
                        m_files[targetF].resize(entries[k].size);
                        fseek(f, entries[k].off, SEEK_SET);
                        if (fread(m_files[targetF].data(), 1, entries[k].size, f) == entries[k].size)
                        {
                            m_fileLoaded[targetF] = true;
                            m_fileDirty[targetF] = true;
                        }
                        else
                        {
                            allOk = false;
                            break;
                        }
                    }
                    if (allOk) loadedFromTemplate = true;
                }
            }
            fclose(f);
        }

        // Fallback to manual factory initialization if template was missing
        if (!loadedFromTemplate)
        {
            // 1. Slot Card (savedata6/31/56, 115 bytes)
            static const uint8_t s_emptyCard115[115] = {
                0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x6C, 0x07, 0x00, 0x00,
                0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x6C, 0x07, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x6C, 0x07, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0xD0, 0x07, 0x00, 0x00, 0x01, 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00
            };
            int cardF = 6 + slot * SLOT_STRIDE;
            m_files[cardF].assign(s_emptyCard115, s_emptyCard115 + 115);
            m_fileLoaded[cardF] = true;
            m_fileDirty[cardF] = true;

            // 2. Profile Card (savedata7/32/57, 170 bytes)
            int subF = 7 + slot * SLOT_STRIDE;
            m_files[subF].assign(170, 0);
            *(uint32_t *)&m_files[subF][0x00] = 1;
            *(uint16_t *)&m_files[subF][0x0C] = 0xFFFF;
            m_fileLoaded[subF] = true;
            m_fileDirty[subF] = true;

            // 3. Player Profile (savedata8/33/58, 198 bytes)
            int profF = 8 + slot * SLOT_STRIDE;
            m_files[profF].assign(198, 0);
            *(uint32_t *)&m_files[profF][0x00] = 1;
            *(uint16_t *)&m_files[profF][0x90] = 0xFFFF; // Empty prefecture marker
            m_fileLoaded[profF] = true;
            m_fileDirty[profF] = true;

            // 4. Stats (savedata9/34/59, 960 bytes)
            int statsF = 9 + slot * SLOT_STRIDE;
            m_files[statsF].assign(960, 0);
            *(uint32_t *)&m_files[statsF][0x00] = 3;
            *(uint32_t *)&m_files[statsF][0x04] = 1;
            for (int s = 0; s < 4; ++s)
            {
                *(uint32_t *)&m_files[statsF][0x08 + s * 0x0E] = 1;
            }
            m_fileLoaded[statsF] = true;
            m_fileDirty[statsF] = true;
        }

        // Section 10.5: Global gallery option pointer fallback (savedata3 0x3A5A ~ 0x3A5C)
        if (m_fileLoaded[FILE_GLOBAL_GALLERY] && m_files[FILE_GLOBAL_GALLERY].size() >= 0x3A5D)
        {
            uint8_t *gbuf = m_files[FILE_GLOBAL_GALLERY].data();
            int fallbackSlot = 0;
            for (int s = 0; s < NUM_SLOTS; ++s)
            {
                if (s != slot && !m_slots[s].empty)
                {
                    fallbackSlot = s;
                    break;
                }
            }
            for (int h = 0; h < 3; ++h)
            {
                if (gbuf[0x3A5A + h] == slot)
                {
                    gbuf[0x3A5A + h] = (uint8_t)fallbackSlot;
                    m_fileDirty[FILE_GLOBAL_GALLERY] = true;
                }
            }
        }

        parseSlot(slot);
        m_modified = true;
        return true;
    }

    void SaveManager::removeCameraFilter(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        m_slots[slot].stats.cameraFilter = -1;
        m_slots[slot].stats.equipMask &= ~(0x04); // Clear bit 2
        m_modified = true;

        // Section 6.4.8, 6.5: Six-in-One Atomic Standard immediately applied to raw buffer
        int statsFile = (slot == 0) ? 9 : (slot == 1 ? 34 : 59);
        if (m_files[statsFile].size() >= 0x354)
        {
            uint8_t *sbuf = m_files[statsFile].data();
            *(int16_t *)&sbuf[0x304] = -1;
            sbuf[0x318] &= ~(0x04);
            *(uint32_t *)&sbuf[0x33C] = 38; // Factory bare lens shutter speed (26 00 00 00)
            *(uint32_t *)&sbuf[0x344] = 20; // Factory bare lens ISO sensitivity (14 00 00 00)
            *(uint32_t *)&sbuf[0x34C] = 9;  // filter_mode_5 (bare lens white balance override A)
            *(uint32_t *)&sbuf[0x350] = 3;  // filter_mode_6 (bare lens white balance override B)
            m_fileDirty[statsFile] = true;
        }
    }

#pragma pack(push, 1)
    struct ZipLocalHeader
    {
        uint32_t signature;       // 0x04034b50
        uint16_t versionNeeded;   // 20
        uint16_t flags;           // 0
        uint16_t compression;     // 8 = Deflate, 0 = Store
        uint16_t modTime;
        uint16_t modDate;
        uint32_t crc32;
        uint32_t compSize;
        uint32_t uncompSize;
        uint16_t nameLen;
        uint16_t extraLen;
    };

    struct ZipCentralHeader
    {
        uint32_t signature;       // 0x02014b50
        uint16_t versionMadeBy;   // 20
        uint16_t versionNeeded;   // 20
        uint16_t flags;           // 0
        uint16_t compression;     // 8 = Deflate, 0 = Store
        uint16_t modTime;
        uint16_t modDate;
        uint32_t crc32;
        uint32_t compSize;
        uint32_t uncompSize;
        uint16_t nameLen;
        uint16_t extraLen;
        uint16_t commentLen;
        uint16_t diskStart;
        uint16_t internalAttr;
        uint32_t externalAttr;
        uint32_t localHeaderOffset;
    };

    struct ZipEOCD
    {
        uint32_t signature;       // 0x06054b50
        uint16_t diskNum;
        uint16_t cdStartDisk;
        uint16_t numEntriesThisDisk;
        uint16_t totalEntries;
        uint32_t cdSize;
        uint32_t cdOffset;
        uint16_t commentLen;
    };
#pragma pack(pop)

    bool SaveManager::createAutoBackup(std::string *outPath)
    {
        time_t rawtime;
        time(&rawtime);
        struct tm *ti = localtime(&rawtime);

        char prefix = (m_source == SOURCE_GAMECARD) ? 'C' : 'D';
        char baseName[128];
        snprintf(baseName, sizeof(baseName), "%c_%04d_%02d_%02d_%02d_%02d_%02d",
                 prefix,
                 ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
                 ti->tm_hour, ti->tm_min, ti->tm_sec);

        bool exportToZip = (cfg::config["zip"] == 1);

        if (exportToZip)
        {
            char zipPath[160];
            snprintf(zipPath, sizeof(zipPath), "/NLPPSS/Saves/%s.zip", baseName);

            std::vector<uint8_t> zipBuffer;
            std::vector<ZipCentralHeader> cdHeaders;
            std::vector<std::string> fileNames;

            uint16_t dosTime = (ti->tm_hour << 11) | (ti->tm_min << 5) | (ti->tm_sec >> 1);
            uint16_t dosDate = ((ti->tm_year + 1900 - 1980) << 9) | ((ti->tm_mon + 1) << 5) | ti->tm_mday;

            for (int i = 0; i < 81; ++i)
            {
                if (m_fileLoaded[i] && !m_files[i].empty())
                {
                    char fname[32];
                    snprintf(fname, sizeof(fname), "savedata%d", i);
                    std::string nameStr = fname;

                    const uint8_t *inData = m_files[i].data();
                    size_t inSize = m_files[i].size();
                    uint32_t crc = lodepng_crc32(inData, inSize);

                    unsigned char *compData = nullptr;
                    size_t compSize = 0;
                    LodePNGCompressSettings compSettings;
                    lodepng_compress_settings_init(&compSettings);
                    int deflateLvl = cfg::config["deflateLevel"];
                    if (deflateLvl <= 1)
                    {
                        compSettings.btype = 1; // Fixed Huffman (Fastest)
                        compSettings.windowsize = 1024;
                        compSettings.nicematch = 16;
                        compSettings.lazymatching = 0;
                    }
                    else if (deflateLvl <= 3)
                    {
                        compSettings.btype = 2; // Dynamic Huffman
                        compSettings.windowsize = 2048;
                        compSettings.nicematch = 32;
                        compSettings.lazymatching = 0;
                    }
                    else if (deflateLvl <= 6)
                    {
                        compSettings.btype = 2;
                        compSettings.windowsize = 4096;
                        compSettings.nicematch = 128;
                        compSettings.lazymatching = 1;
                    }
                    else
                    {
                        compSettings.btype = 2;
                        compSettings.windowsize = 32768;
                        compSettings.nicematch = 258;
                        compSettings.lazymatching = 1;
                    }
                    unsigned err = lodepng_deflate(&compData, &compSize, inData, inSize, &compSettings);

                    uint16_t compMethod = 8;
                    if (err != 0 || !compData || compSize >= inSize)
                    {
                        compMethod = 0;
                        if (compData) { free(compData); compData = nullptr; }
                    }

                    uint32_t localOffset = (uint32_t)zipBuffer.size();

                    ZipLocalHeader lh;
                    lh.signature = 0x04034b50;
                    lh.versionNeeded = 20;
                    lh.flags = 0;
                    lh.compression = compMethod;
                    lh.modTime = dosTime;
                    lh.modDate = dosDate;
                    lh.crc32 = crc;
                    lh.compSize = (compMethod == 8) ? (uint32_t)compSize : (uint32_t)inSize;
                    lh.uncompSize = (uint32_t)inSize;
                    lh.nameLen = (uint16_t)nameStr.length();
                    lh.extraLen = 0;

                    size_t curPos = zipBuffer.size();
                    zipBuffer.resize(curPos + sizeof(lh) + lh.nameLen + lh.compSize);
                    memcpy(&zipBuffer[curPos], &lh, sizeof(lh));
                    memcpy(&zipBuffer[curPos + sizeof(lh)], nameStr.data(), lh.nameLen);
                    if (compMethod == 8)
                    {
                        memcpy(&zipBuffer[curPos + sizeof(lh) + lh.nameLen], compData, compSize);
                        free(compData);
                    }
                    else
                    {
                        memcpy(&zipBuffer[curPos + sizeof(lh) + lh.nameLen], inData, inSize);
                    }

                    ZipCentralHeader cd;
                    cd.signature = 0x02014b50;
                    cd.versionMadeBy = 20;
                    cd.versionNeeded = 20;
                    cd.flags = 0;
                    cd.compression = compMethod;
                    cd.modTime = dosTime;
                    cd.modDate = dosDate;
                    cd.crc32 = crc;
                    cd.compSize = lh.compSize;
                    cd.uncompSize = lh.uncompSize;
                    cd.nameLen = lh.nameLen;
                    cd.extraLen = 0;
                    cd.commentLen = 0;
                    cd.diskStart = 0;
                    cd.internalAttr = 0;
                    cd.externalAttr = 0;
                    cd.localHeaderOffset = localOffset;

                    cdHeaders.push_back(cd);
                    fileNames.push_back(nameStr);
                }
            }

            uint32_t cdOffset = (uint32_t)zipBuffer.size();
            for (size_t k = 0; k < cdHeaders.size(); ++k)
            {
                size_t curPos = zipBuffer.size();
                zipBuffer.resize(curPos + sizeof(ZipCentralHeader) + fileNames[k].length());
                memcpy(&zipBuffer[curPos], &cdHeaders[k], sizeof(ZipCentralHeader));
                memcpy(&zipBuffer[curPos + sizeof(ZipCentralHeader)], fileNames[k].data(), fileNames[k].length());
            }
            uint32_t cdSize = (uint32_t)zipBuffer.size() - cdOffset;

            ZipEOCD eocd;
            eocd.signature = 0x06054b50;
            eocd.diskNum = 0;
            eocd.cdStartDisk = 0;
            eocd.numEntriesThisDisk = (uint16_t)cdHeaders.size();
            eocd.totalEntries = (uint16_t)cdHeaders.size();
            eocd.cdSize = cdSize;
            eocd.cdOffset = cdOffset;
            eocd.commentLen = 0;

            size_t curPos = zipBuffer.size();
            zipBuffer.resize(curPos + sizeof(eocd));
            memcpy(&zipBuffer[curPos], &eocd, sizeof(eocd));

            FS_Path zipFsPath = fsMakePath(PATH_ASCII, zipPath);
            FSUSER_CreateFile(fs::getSDMCArch(), zipFsPath, 0, zipBuffer.size());

            Handle fh;
            Result res = FSUSER_OpenFile(&fh, fs::getSDMCArch(), zipFsPath, FS_OPEN_WRITE, 0);
            if (R_SUCCEEDED(res))
            {
                u32 written = 0;
                FSFILE_Write(fh, &written, 0, zipBuffer.data(), zipBuffer.size(), FS_WRITE_FLUSH);
                FSFILE_Close(fh);
            }

            if (outPath) *outPath = zipPath;
            return true;
        }

        char dirName[160];
        snprintf(dirName, sizeof(dirName), "/NLPPSS/Saves/%s", baseName);
        fs::createDirRec(fs::getSDMCArch(), util::toUtf16(dirName));

        for (int i = 0; i < 81; ++i)
        {
            if (m_fileLoaded[i] && !m_files[i].empty())
            {
                char filePath[160];
                snprintf(filePath, sizeof(filePath), "%s/savedata%d", dirName, i);

                FS_Path binPath = fsMakePath(PATH_ASCII, filePath);
                FSUSER_CreateFile(fs::getSDMCArch(), binPath, 0, m_files[i].size());

                Handle fh;
                Result res = FSUSER_OpenFile(&fh, fs::getSDMCArch(), binPath, FS_OPEN_WRITE, 0);
                if (R_SUCCEEDED(res))
                {
                    u32 written = 0;
                    FSFILE_Write(fh, &written, 0, m_files[i].data(), m_files[i].size(), FS_WRITE_FLUSH);
                    FSFILE_Close(fh);
                }
            }
        }

        if (outPath) *outPath = dirName;
        return true;
    }

    std::vector<BackupInfo> SaveManager::listBackups()
    {
        std::vector<BackupInfo> list;
        std::string root = "/NLPPSS/Saves";

        Handle dirHandle;
        Result res = FSUSER_OpenDirectory(&dirHandle, fs::getSDMCArch(), fsMakePath(PATH_ASCII, root.c_str()));
        if (R_FAILED(res)) return list;

        FS_DirectoryEntry entry;
        u32 entriesRead = 0;
        while (R_SUCCEEDED(FSDIR_Read(dirHandle, &entriesRead, 1, &entry)) && entriesRead > 0)
        {
            std::string name = util::toUtf8((char16_t *)entry.name);
            if (name == "." || name == "..") continue;

            if (entry.attributes & FS_ATTRIBUTE_DIRECTORY)
            {
                BackupInfo info;
                info.dirName = name;
                info.fullPath = root + "/" + name;
                info.dateStr = name;
                list.push_back(info);
            }
            else if (util::endsWith(name, std::string(".zip")))
            {
                BackupInfo info;
                info.dirName = name;
                info.fullPath = root + "/" + name;
                info.dateStr = name;
                list.push_back(info);
            }
        }
        FSDIR_Close(dirHandle);

        std::sort(list.begin(), list.end(), [](const BackupInfo &a, const BackupInfo &b) {
            return a.dirName > b.dirName;
        });
        return list;
    }

    bool SaveManager::restoreBackup(const std::string &backupDir)
    {
        if (util::endsWith(backupDir, std::string(".zip")))
        {
            Handle fh;
            Result res = FSUSER_OpenFile(&fh, fs::getSDMCArch(), fsMakePath(PATH_ASCII, backupDir.c_str()), FS_OPEN_READ, 0);
            if (R_FAILED(res)) return false;

            u64 zipFileSize = 0;
            FSFILE_GetSize(fh, &zipFileSize);
            if (zipFileSize < sizeof(ZipEOCD))
            {
                FSFILE_Close(fh);
                return false;
            }

            std::vector<uint8_t> zipBytes(zipFileSize);
            u32 bytesRead = 0;
            FSFILE_Read(fh, &bytesRead, 0, zipBytes.data(), zipFileSize);
            FSFILE_Close(fh);

            int eocdPos = -1;
            for (int p = (int)zipBytes.size() - (int)sizeof(ZipEOCD); p >= 0 && p >= (int)zipBytes.size() - 65557; --p)
            {
                if (*(const uint32_t *)&zipBytes[p] == 0x06054b50)
                {
                    eocdPos = p;
                    break;
                }
            }
            if (eocdPos < 0) return false;

            const ZipEOCD *eocd = (const ZipEOCD *)&zipBytes[eocdPos];
            uint32_t cdOffset = eocd->cdOffset;
            uint16_t totalEntries = eocd->totalEntries;

            size_t p = cdOffset;
            for (uint16_t e = 0; e < totalEntries && p + sizeof(ZipCentralHeader) <= zipBytes.size(); ++e)
            {
                const ZipCentralHeader *cd = (const ZipCentralHeader *)&zipBytes[p];
                if (cd->signature != 0x02014b50) break;

                std::string fname((const char *)&zipBytes[p + sizeof(ZipCentralHeader)], cd->nameLen);
                p += sizeof(ZipCentralHeader) + cd->nameLen + cd->extraLen + cd->commentLen;

                int fileIdx = -1;
                size_t sPos = fname.find("savedata");
                if (sPos != std::string::npos)
                {
                    fileIdx = atoi(fname.c_str() + sPos + 8);
                }

                if (fileIdx >= 0 && fileIdx < 81)
                {
                    uint32_t lhOffset = cd->localHeaderOffset;
                    if (lhOffset + sizeof(ZipLocalHeader) <= zipBytes.size())
                    {
                        const ZipLocalHeader *lh = (const ZipLocalHeader *)&zipBytes[lhOffset];
                        if (lh->signature == 0x04034b50)
                        {
                            size_t dataOffset = lhOffset + sizeof(ZipLocalHeader) + lh->nameLen + lh->extraLen;
                            if (dataOffset + lh->compSize <= zipBytes.size())
                            {
                                const uint8_t *compPtr = &zipBytes[dataOffset];
                                if (lh->compression == 0) // Store
                                {
                                    m_files[fileIdx].assign(compPtr, compPtr + lh->uncompSize);
                                    m_fileLoaded[fileIdx] = true;
                                    m_fileDirty[fileIdx] = true;
                                }
                                else if (lh->compression == 8) // Deflate
                                {
                                    unsigned char *uncomp = nullptr;
                                    size_t uncompSize = 0;
                                    LodePNGDecompressSettings decSettings;
                                    lodepng_decompress_settings_init(&decSettings);
                                    unsigned err = lodepng_inflate(&uncomp, &uncompSize, compPtr, lh->compSize, &decSettings);
                                    if (err == 0 && uncomp)
                                    {
                                        m_files[fileIdx].assign(uncomp, uncomp + uncompSize);
                                        m_fileLoaded[fileIdx] = true;
                                        m_fileDirty[fileIdx] = true;
                                        free(uncomp);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            for (int i = 0; i < 81; ++i)
            {
                char filePath[128];
                snprintf(filePath, sizeof(filePath), "%s/savedata%d", backupDir.c_str(), i);

                Handle fh;
                Result res = FSUSER_OpenFile(&fh, fs::getSDMCArch(), fsMakePath(PATH_ASCII, filePath), FS_OPEN_READ, 0);
                if (R_SUCCEEDED(res))
                {
                    u64 fsize = 0;
                    FSFILE_GetSize(fh, &fsize);
                    if (fsize > 0 && fsize < 1024 * 1024)
                    {
                        m_files[i].resize(fsize);
                        u32 bytesRead = 0;
                        FSFILE_Read(fh, &bytesRead, 0, m_files[i].data(), fsize);
                        m_fileLoaded[i] = true;
                        m_fileDirty[i] = true;
                    }
                    FSFILE_Close(fh);
                }
            }
        }

        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            parseSlot(i);
        }
        m_modified = true;
        return true;
    }

    bool SaveManager::commitChanges(std::string *statusMsg)
    {
        if (!m_archOpen)
        {
            if (statusMsg) *statusMsg = "存档归档未打开!";
            return false;
        }

        // Serialize all 3 slots
        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            serializeSlot(i);
        }


        // Write all dirty files
        int writtenCount = 0;
        for (int i = 0; i < 81; ++i)
        {
            if (m_fileDirty[i] && m_fileLoaded[i] && !m_files[i].empty())
            {
                char fname[32];
                snprintf(fname, sizeof(fname), "/savedata%d", i);

                FS_Path binPath = fsMakePath(PATH_ASCII, fname);
                Handle fh;
                Result res = FSUSER_OpenFile(&fh, m_saveArch, binPath, FS_OPEN_WRITE, 0);
                if (R_FAILED(res))
                {
                    // If file doesn't exist, create it
                    FSUSER_CreateFile(m_saveArch, binPath, 0, m_files[i].size());
                    res = FSUSER_OpenFile(&fh, m_saveArch, binPath, FS_OPEN_WRITE, 0);
                }

                if (R_SUCCEEDED(res))
                {
                    FSFILE_SetSize(fh, m_files[i].size());
                    u32 bytesWritten = 0;
                    FSFILE_Write(fh, &bytesWritten, 0, m_files[i].data(), m_files[i].size(), FS_WRITE_FLUSH);
                    FSFILE_Close(fh);
                    m_fileDirty[i] = false;
                    writtenCount++;
                }
            }
        }

        // Commit archive
        Result res = FSUSER_ControlArchive(m_saveArch, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
        if (R_FAILED(res))
        {
            if (statusMsg) *statusMsg = "提交存档数据失败 (COMMIT_SAVE_DATA)!";
            return false;
        }

        // For SD installed title, wipe secure value
        if (m_source == SOURCE_SD)
        {
            u64 in = ((u64)SECUREVALUE_SLOT_SD << 32) | (0x0F4E << 8);
            u8 out = 0;
            FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &in, 8, &out, 1);
        }

        m_modified = false;
        if (statusMsg)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "已成功写回 %d 个文件!", writtenCount);
            *statusMsg = buf;
        }
        return true;
    }
}
