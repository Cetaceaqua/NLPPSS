#include "nlpp_save.h"
#include "nlpp_dict.h"
#include "fs.h"
#include "util.h"
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

        int profFile = getSlotFile(FILE_BASE_PROFILE, slot);
        int statsFile = getSlotFile(FILE_BASE_STATS, slot);
        int gfFile = getSlotFile(FILE_BASE_GIRLFRIEND, slot);
        int timeFile = getSlotFile(FILE_BASE_TIMELINE, slot);

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
        sd.profile.birthDay = pbuf[0x85];
        sd.profile.blood = (BloodType)pbuf[0x8C];

        // 2. Stats (savedata9/34/59, 960B)
        if (m_fileLoaded[statsFile] && m_files[statsFile].size() >= 960)
        {
            const uint8_t *sbuf = m_files[statsFile].data();
            sd.stats.fitness     = *(const uint16_t *)&sbuf[0x008];
            sd.stats.knowledge   = *(const uint16_t *)&sbuf[0x016];
            sd.stats.sensibility = *(const uint16_t *)&sbuf[0x024];
            sd.stats.charm       = *(const uint16_t *)&sbuf[0x032];
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

            sd.girlfriend.sliderX = *(const uint16_t *)&gbuf[0x3C8];
            sd.girlfriend.sliderY = *(const uint16_t *)&gbuf[0x3DC];
            sd.girlfriend.sliderZ = *(const uint16_t *)&gbuf[0x3F0];
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

        // Comprehensive empty slot detection
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

        return true;
    }

    bool SaveManager::serializeSlot(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return false;
        SlotData &sd = m_slots[slot];

        int profFile = getSlotFile(FILE_BASE_PROFILE, slot);
        int statsFile = getSlotFile(FILE_BASE_STATS, slot);
        int gfFile = getSlotFile(FILE_BASE_GIRLFRIEND, slot);
        int timeFile = getSlotFile(FILE_BASE_TIMELINE, slot);

        // 1. Profile (198B)
        if (m_files[profFile].size() < 198) m_files[profFile].resize(198, 0);
        uint8_t *pbuf = m_files[profFile].data();

        if (sd.empty)
        {
            *(uint16_t *)&pbuf[0x90] = 0xFFFF;
        }
        else
        {
            memset(&pbuf[0x04], 0, 12);
            memcpy(&pbuf[0x04], sd.profile.firstName.c_str(), std::min(sd.profile.firstName.length(), (size_t)12));

            memset(&pbuf[0x14], 0, 12);
            memcpy(&pbuf[0x14], sd.profile.lastName.c_str(), std::min(sd.profile.lastName.length(), (size_t)12));

            memset(&pbuf[0x64], 0, 12);
            memcpy(&pbuf[0x64], sd.profile.hn.c_str(), std::min(sd.profile.hn.length(), (size_t)12));

            pbuf[0x84] = sd.profile.birthMonth;
            pbuf[0x85] = sd.profile.birthDay;
            pbuf[0x8C] = (uint8_t)sd.profile.blood;
            *(uint16_t *)&pbuf[0x90] = sd.profile.prefecture;
        }
        m_fileDirty[profFile] = true;

        if (sd.empty) return true;

        // 2. Stats (960B)
        if (m_files[statsFile].size() < 960) m_files[statsFile].resize(960, 0);
        uint8_t *sbuf = m_files[statsFile].data();
        *(uint16_t *)&sbuf[0x008] = std::min(sd.stats.fitness, (uint16_t)255);
        *(uint16_t *)&sbuf[0x016] = std::min(sd.stats.knowledge, (uint16_t)255);
        *(uint16_t *)&sbuf[0x024] = std::min(sd.stats.sensibility, (uint16_t)255);
        *(uint16_t *)&sbuf[0x032] = std::min(sd.stats.charm, (uint16_t)255);
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
        m_fileDirty[statsFile] = true;

        // 3. Girlfriend (1508B)
        if (m_files[gfFile].size() < 1508) m_files[gfFile].resize(1508, 0);
        uint8_t *gbuf = m_files[gfFile].data();
        gbuf[0x243] = sd.girlfriend.affection;
        *(uint32_t *)&gbuf[0x2AC] = std::min(sd.girlfriend.coupleExp, (uint32_t)99250);

        gbuf[0x466] = sd.girlfriend.hairLength;
        gbuf[0x468] = sd.girlfriend.hairStyleId;
        gbuf[0x469] = sd.girlfriend.reservedStyleId;
        gbuf[0x46A] = sd.girlfriend.hairTasteTarget;

        gbuf[0x50A] = sd.girlfriend.hairColorId;
        gbuf[0x50B] = sd.girlfriend.nextHairColorId;
        gbuf[0x4B3] = sd.girlfriend.hairR;
        gbuf[0x4B4] = sd.girlfriend.hairG;
        gbuf[0x4B5] = sd.girlfriend.hairB;
        gbuf[0x4B6] = sd.girlfriend.nextHairR;
        gbuf[0x4B7] = sd.girlfriend.nextHairG;
        gbuf[0x4B8] = sd.girlfriend.nextHairB;
        gbuf[0x54C] = sd.girlfriend.hairColorActive;

        *(uint16_t *)&gbuf[0x3C8] = std::min(sd.girlfriend.sliderX, (uint16_t)1000);
        *(uint16_t *)&gbuf[0x3DC] = std::min(sd.girlfriend.sliderY, (uint16_t)1000);
        *(uint16_t *)&gbuf[0x3F0] = std::min(sd.girlfriend.sliderZ, (uint16_t)4);
        m_fileDirty[gfFile] = true;

        // 4. Timeline (333B)
        if (m_files[timeFile].size() < 333) m_files[timeFile].resize(333, 0);
        uint8_t *tbuf = m_files[timeFile].data();
        *(uint32_t *)&tbuf[0x04] = sd.timeline.timelineDays;
        *(uint32_t *)&tbuf[0x39] = sd.timeline.datingDays;
        *(uint16_t *)&tbuf[0x14] = 0; // Clear max absence to avoid cold punishment
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

        // Copy all 25 files
        for (int i = 0; i < SLOT_STRIDE; ++i)
        {
            int srcF = 8 + srcSlot * SLOT_STRIDE + i;
            int dstF = 8 + dstSlot * SLOT_STRIDE + i;
            if (srcF < 81 && dstF < 81 && m_fileLoaded[srcF])
            {
                m_files[dstF] = m_files[srcF];
                m_fileLoaded[dstF] = true;
                m_fileDirty[dstF] = true;
            }
        }

        parseSlot(dstSlot);
        m_modified = true;
        return true;
    }

    void SaveManager::setProfile(int slot, const PlayerProfile &prof)
    {
        if (slot >= 0 && slot < NUM_SLOTS)
        {
            m_slots[slot].profile = prof;
            m_slots[slot].empty = prof.empty;
            m_modified = true;
        }
    }

    void SaveManager::setBoyfriendStats(int slot, const BoyfriendStats &stats)
    {
        if (slot >= 0 && slot < NUM_SLOTS)
        {
            m_slots[slot].stats = stats;
            m_modified = true;
        }
    }

    void SaveManager::setGirlfriendStatus(int slot, const GirlfriendStatus &gf)
    {
        if (slot >= 0 && slot < NUM_SLOTS)
        {
            m_slots[slot].girlfriend = gf;
            m_modified = true;
        }
    }

    void SaveManager::setTimelineData(int slot, const TimelineData &timeline)
    {
        if (slot >= 0 && slot < NUM_SLOTS)
        {
            m_slots[slot].timeline = timeline;
            m_modified = true;
        }
    }

    void SaveManager::maxBoyfriendStats(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        m_slots[slot].stats.fitness = 255;
        m_slots[slot].stats.knowledge = 255;
        m_slots[slot].stats.sensibility = 255;
        m_slots[slot].stats.charm = 255;
        m_modified = true;
    }

    void SaveManager::maxRichMoney(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        m_slots[slot].stats.rich = 999999;
        m_modified = true;
    }

    void SaveManager::maxTodoPoints(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        m_slots[slot].stats.todo = 9999;
        m_modified = true;
    }

    void SaveManager::removeCameraFilter(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        m_slots[slot].stats.cameraFilter = -1;
        m_slots[slot].stats.equipMask &= ~(0x04); // Clear bit 2
        m_modified = true;
    }

    void SaveManager::maxGirlfriendAffection(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        m_slots[slot].girlfriend.affection = 255;
        m_modified = true;
    }

    void SaveManager::setCoupleTitleRank(int slot, uint8_t rank)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        if (rank < 1) rank = 1;
        if (rank > 23) rank = 23;
        m_slots[slot].girlfriend.coupleExp = Dict::getCoupleTitle(rank).exp_min;
        m_modified = true;
    }

    void SaveManager::unlockAllWardrobe513(int slot)
    {
        if (slot < 0 || slot >= NUM_SLOTS) return;
        int wFile = getSlotFile(FILE_BASE_WARDROBE, slot);
        if (m_files[wFile].size() < 5120) m_files[wFile].resize(5120, 0);

        uint8_t *wbuf = m_files[wFile].data();
        *(uint16_t *)&wbuf[0x02] = 513; // 0x0201

        // Block 1 (items 0..279)
        memset(&wbuf[0x05], 0xFF, 35);
        // Clear Block 1 NEW red dots
        memset(&wbuf[0x28], 0x00, 50);

        // Block 2 (items 280..511)
        memset(&wbuf[0x5A], 0xFF, 29);
        // Item 512 only lowest bit
        wbuf[0x77] = 0x01;
        // Strictly zero out items 513..559 (boundary safety protection)
        memset(&wbuf[0x78], 0x00, 5);
        // Clear Block 2 NEW red dots
        memset(&wbuf[0x7D], 0x00, 50);

        m_fileDirty[wFile] = true;
        m_modified = true;
    }

    void SaveManager::unlockMasterGallery()
    {
        if (m_files[FILE_GLOBAL_GALLERY].size() < 24064)
        {
            m_files[FILE_GLOBAL_GALLERY].resize(24064, 0);
        }
        uint8_t *g = m_files[FILE_GLOBAL_GALLERY].data();

        // 1. Dreams (60B, 20 items per heroine)
        *(uint32_t *)&g[0x0004] = 1;
        *(uint32_t *)&g[0x0008] = 0x000FFFFF; // Manaka
        *(uint32_t *)&g[0x000C] = 0;          // Manaka NEW cleared
        *(uint32_t *)&g[0x0010] = 0x000FFFFF; // Rinko
        *(uint32_t *)&g[0x0014] = 0;          // Rinko NEW cleared
        *(uint32_t *)&g[0x0018] = 0x000FFFFF; // Nene
        *(uint32_t *)&g[0x001C] = 0;          // Nene NEW cleared

        // 2. After-Dreams (60B, 20 items per heroine)
        *(uint32_t *)&g[0x0040] = 1;
        *(uint32_t *)&g[0x0044] = 0x000FFFFF; // Manaka
        *(uint32_t *)&g[0x0048] = 0;
        *(uint32_t *)&g[0x004C] = 0x000FFFFF; // Rinko
        *(uint32_t *)&g[0x0050] = 0;
        *(uint32_t *)&g[0x0054] = 0x000FFFFF; // Nene
        *(uint32_t *)&g[0x0058] = 0;

        // 3. Memories (132B, 81 items per heroine: 10B 0xFF + 1B 0x01 + 5B 0x00)
        *(uint32_t *)&g[0x007C] = 1;
        // Manaka
        memset(&g[0x0080], 0xFF, 10);
        g[0x008A] = 0x01;
        memset(&g[0x008B], 0x00, 5);
        memset(&g[0x0090], 0x00, 16); // NEW cleared
        // Rinko
        memset(&g[0x00A0], 0xFF, 10);
        g[0x00AA] = 0x01;
        memset(&g[0x00AB], 0x00, 5);
        memset(&g[0x00B0], 0x00, 16);
        // Nene
        memset(&g[0x00C0], 0xFF, 10);
        g[0x00CA] = 0x01;
        memset(&g[0x00CB], 0x00, 5);
        memset(&g[0x00D0], 0x00, 16);

        // 4. Trip flags (132B: Manaka 39, Rinko 42, Nene 42)
        *(uint32_t *)&g[0x0100] = 1;
        // Manaka: 4B 0xFF + 1B 0x7F + 11B 0x00
        memset(&g[0x0104], 0xFF, 4);
        g[0x0108] = 0x7F;
        memset(&g[0x0109], 0x00, 11);
        memset(&g[0x0114], 0x00, 16); // NEW cleared
        // Rinko: 5B 0xFF + 1B 0x03 + 10B 0x00
        memset(&g[0x0124], 0xFF, 5);
        g[0x0129] = 0x03;
        memset(&g[0x012A], 0x00, 10);
        memset(&g[0x0134], 0x00, 16);
        // Nene: 5B 0xFF + 1B 0x03 + 10B 0x00
        memset(&g[0x0144], 0xFF, 5);
        g[0x0149] = 0x03;
        memset(&g[0x014A], 0x00, 10);
        memset(&g[0x0154], 0x00, 16);

        // 5. Contest (474B, 85 items per heroine: 10B 0xFF + 1B 0x1F + 5B 0x00)
        *(uint32_t *)&g[0x0184] = 1;
        // Manaka
        memset(&g[0x0188], 0xFF, 10);
        g[0x0192] = 0x1F;
        memset(&g[0x0193], 0x00, 5);
        memset(&g[0x0198], 0x00, 16);
        // Rinko
        memset(&g[0x01A8], 0xFF, 10);
        g[0x01B2] = 0x1F;
        memset(&g[0x01B3], 0x00, 5);
        memset(&g[0x01B8], 0x00, 16);
        // Nene
        memset(&g[0x01C8], 0xFF, 10);
        g[0x01D2] = 0x1F;
        memset(&g[0x01D3], 0x00, 5);
        memset(&g[0x01D8], 0x00, 16);

        // 6. Special (180B, 192 items per heroine: 24B 0xFF)
        *(uint32_t *)&g[0x035E] = 2; // version 2
        // Manaka
        memset(&g[0x0362], 0xFF, 24);
        memset(&g[0x037A], 0x00, 24);
        // Rinko
        memset(&g[0x0392], 0xFF, 24);
        memset(&g[0x03AA], 0x00, 24);
        // Nene
        memset(&g[0x03C2], 0xFF, 24);
        memset(&g[0x03DA], 0x00, 24);

        // NOTE: Trip replay records (0x0412..0x3A59) are strictly kept intact!
        m_fileDirty[FILE_GLOBAL_GALLERY] = true;
        m_modified = true;
    }

    bool SaveManager::createAutoBackup(std::string *outPath)
    {
        time_t rawtime;
        time(&rawtime);
        struct tm *ti = localtime(&rawtime);

        char prefix = (m_source == SOURCE_GAMECARD) ? 'C' : 'D';
        char dirName[128];
        snprintf(dirName, sizeof(dirName), "/NLPPSS/Saves/%c_%04d_%02d_%02d_%02d_%02d_%02d",
                 prefix,
                 ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
                 ti->tm_hour, ti->tm_min, ti->tm_sec);

        fs::createDirRec(fs::getSDMCArch(), util::toUtf16(dirName));

        for (int i = 0; i < 81; ++i)
        {
            if (m_fileLoaded[i] && !m_files[i].empty())
            {
                char filePath[128];
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
            if (entry.attributes & FS_ATTRIBUTE_DIRECTORY)
            {
                std::string name = util::toUtf8((char16_t *)entry.name);
                if (name != "." && name != "..")
                {
                    BackupInfo info;
                    info.dirName = name;
                    info.fullPath = root + "/" + name;
                    info.dateStr = name;
                    list.push_back(info);
                }
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

        // Auto backup before writing
        std::string backupPath;
        createAutoBackup(&backupPath);

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
            char buf[128];
            snprintf(buf, sizeof(buf), "已成功写回 %d 个文件并完成自动备份!\n备份至: %s", writtenCount, backupPath.c_str());
            *statusMsg = buf;
        }
        return true;
    }
}
