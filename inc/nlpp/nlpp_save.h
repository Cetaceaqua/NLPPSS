#pragma once

#include "nlpp_types.h"
#include <string>
#include <vector>

namespace nlpp
{
    enum SaveSource
    {
        SOURCE_NONE = 0,
        SOURCE_GAMECARD = 1,
        SOURCE_SD = 2
    };

    struct BackupInfo
    {
        std::string dirName; // e.g. "20261016_183000"
        std::string fullPath;
        std::string dateStr;
    };

    class SaveManager
    {
    public:
        SaveManager();
        ~SaveManager();

        bool init();
        void exit();

        // Source detection & switching
        bool detectSources();
        bool hasGameCard() const { return m_hasGameCard; }
        bool hasSD() const { return m_hasSD; }
        SaveSource getSource() const { return m_source; }
        bool switchSource(SaveSource source);

        // Archive lifecycle
        bool openSaveArchive();
        void closeSaveArchive();
        bool isArchiveOpen() const { return m_archOpen; }

        // Slot Management
        int getCurrentSlot() const { return m_currentSlot; }
        bool selectSlot(int slotIndex);
        SlotData &getSlotData(int slotIndex) { return m_slots[slotIndex]; }
        const SlotData &getSlotData(int slotIndex) const { return m_slots[slotIndex]; }
        bool isSlotEmpty(int slotIndex) const { return m_slots[slotIndex].empty; }

        // Slot Clone (clone srcSlot 0..2 into dstSlot 0..2)
        bool cloneSlot(int srcSlot, int dstSlot);

        // Update parsed data structures and mark dirty
        void setProfile(int slot, const PlayerProfile &prof);
        void setBoyfriendStats(int slot, const BoyfriendStats &stats);
        void setGirlfriendStatus(int slot, const GirlfriendStatus &gf);
        void setTimelineData(int slot, const TimelineData &timeline);

        // Quick modifier helpers
        void maxBoyfriendStats(int slot);
        void maxRichMoney(int slot);
        void maxTodoPoints(int slot);
        void removeCameraFilter(int slot);
        void maxGirlfriendAffection(int slot);
        void setCoupleTitleRank(int slot, uint8_t rank);
        void unlockAllWardrobe513(int slot);
        void unlockMasterGallery(); // savedata3 (all 1317 elements)

        // Backup & Restore
        bool createAutoBackup(std::string *outPath = nullptr);
        std::vector<BackupInfo> listBackups();
        bool restoreBackup(const std::string &backupDir);

        // Commit changes to 3DS save archive (auto-backup, commit, wipe secure value)
        bool commitChanges(std::string *statusMsg = nullptr);

        bool isModified() const { return m_modified; }

    private:
        bool loadAllFiles();
        bool parseSlot(int slot);
        bool serializeSlot(int slot);
        bool syncAppellations(int slot);

        SaveSource m_source = SOURCE_NONE;
        bool m_hasGameCard = false;
        bool m_hasSD = false;
        FS_Archive m_saveArch = 0;
        bool m_archOpen = false;

        int m_currentSlot = 0;
        SlotData m_slots[NUM_SLOTS];
        bool m_modified = false;

        // Raw file buffers for savedata0..80
        std::vector<uint8_t> m_files[81];
        bool m_fileLoaded[81];
        bool m_fileDirty[81];
    };

    extern SaveManager g_saveMgr;
}
