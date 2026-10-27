#include "nlpp_ui.h"
#include "nlpp_save.h"
#include "nlpp_dict.h"
#include "nlpp_portrait.h"
#include "gfx.h"
#include "ui.h"
#include "ui/keyboard.h"
#include "cfg.h"
#include "fs.h"
#include "util.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace ui
{
    // Main Menu - Save Slots & Backup Management
    static nlpp::SaveSource s_currentSource = nlpp::SOURCE_SD;
    static bool s_lastCardInserted = false;
    static ui::menu s_mediaMenu;
    static ui::menu s_slotMenu;
    static bool s_slotSelectOpen = false;

    // Submenu for slot operations (e.g. choose destination slot for copy)
    static bool s_slotCopyOpen = false;
    static int s_copySrcSlot = -1;
    static ui::menu s_copyDstMenu;

    static void refreshSlotMenu()
    {
        s_slotMenu.reset();
        for (int i = 0; i < nlpp::NUM_SLOTS; ++i)
        {
            char itemBuf[64];
            if (nlpp::g_saveMgr.isSlotEmpty(i))
            {
                snprintf(itemBuf, sizeof(itemBuf), "%s %d: %s", getTxt("槽位"), i + 1, getTxt("(空)"));
            }
            else
            {
                const auto &sd = nlpp::g_saveMgr.getSlotData(i);
                std::string playerName;
                if (!sd.profile.lastName.empty() && !sd.profile.firstName.empty())
                    playerName = sd.profile.lastName + " " + sd.profile.firstName;
                else if (!sd.profile.lastName.empty())
                    playerName = sd.profile.lastName;
                else if (!sd.profile.firstName.empty())
                    playerName = sd.profile.firstName;
                else if (!sd.profile.hn.empty())
                    playerName = sd.profile.hn;
                else
                    playerName = "---";

                snprintf(itemBuf, sizeof(itemBuf), "%s %d: %s", getTxt("槽位"), i + 1, playerName.c_str());
            }
            s_slotMenu.addOpt(itemBuf, 300);
        }
    }

    static void onConfirmSlotCopy_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        int dstSlot = s_copyDstMenu.getSelected();
        if (s_copySrcSlot >= 0 && dstSlot >= 0 && s_copySrcSlot != dstSlot)
        {
            // Auto-backup before copy
            if (cfg::config["autobackup"])
            {
                nlpp::g_saveMgr.createAutoBackup(nullptr);
            }
            nlpp::g_saveMgr.cloneSlot(s_copySrcSlot, dstSlot);
            nlpp::g_saveMgr.commitChanges();
            refreshSlotMenu();
            ui::showMessage(getTxt("槽位复制成功！"));
        }
        s_slotCopyOpen = false;
        t->finished = true;
    }

    static void onConfirmSlotCopy(void *a)
    {
        int dst = s_copyDstMenu.getSelected();
        char msg[128];
        snprintf(msg, sizeof(msg), getTxt("确定要将槽位 %d 复制到槽位 %d 吗？"), s_copySrcSlot + 1, dst + 1);
        ui::confirm(msg, onConfirmSlotCopy_t, nullptr, a);
    }

    static int s_clearTargetSlot = 0;
    static void onConfirmClearSlot_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        if (s_clearTargetSlot >= 0 && s_clearTargetSlot < nlpp::NUM_SLOTS)
        {
            // Auto-backup before clear
            if (cfg::config["autobackup"])
            {
                nlpp::g_saveMgr.createAutoBackup(nullptr);
            }
            if (nlpp::g_saveMgr.clearSlot(s_clearTargetSlot))
            {
                nlpp::g_saveMgr.commitChanges();
                refreshSlotMenu();
                ui::showMessage(getTxt("槽位已重置为空槽！"));
            }
            else
            {
                ui::showMessage(getTxt("重置槽位失败！"));
            }
        }
        t->finished = true;
    }

    static void onConfirmBackupSlot_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        std::string bakName;
        if (nlpp::g_saveMgr.createAutoBackup(&bakName))
        {
            char msg[128];
            snprintf(msg, sizeof(msg), getTxt("存档备份成功！\n%s"), bakName.c_str());
            ui::showMessage(msg);
        }
        else
        {
            ui::showMessage(getTxt("存档备份失败！"));
        }
        t->finished = true;
    }

    void saveSelectInit(void *)
    {
        nlpp::g_saveMgr.detectSources();
        s_lastCardInserted = nlpp::g_saveMgr.hasGameCard();
        nlpp::SaveSource defaultSrc = (cfg::config["default_type"] == 1) ? nlpp::SOURCE_GAMECARD : nlpp::SOURCE_SD;
        s_currentSource = defaultSrc;
        nlpp::g_saveMgr.switchSource(s_currentSource);

        s_mediaMenu.reset();
        s_mediaMenu.addOpt(getTxt("数字版"), 400);
        s_mediaMenu.addOpt(getTxt("实体版"), 400);
        s_mediaMenu.setSelected((s_currentSource == nlpp::SOURCE_GAMECARD) ? 1 : 0);

        s_slotSelectOpen = false;
        s_slotCopyOpen = false;
        s_slotMenu.setPaging(false);
        refreshSlotMenu();

        s_copyDstMenu.reset();
        s_copyDstMenu.setPaging(false);
        for (int i = 0; i < nlpp::NUM_SLOTS; ++i)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %d", getTxt("目标槽位"), i + 1);
            s_copyDstMenu.addOpt(buf, 300);
        }

        // Auto-load on boot logic if enabled
        if (cfg::config["autoload"] == 1)
        {
            bool srcAvailable = (defaultSrc == nlpp::SOURCE_GAMECARD) ? nlpp::g_saveMgr.hasGameCard() : nlpp::g_saveMgr.hasSD();

            if (!srcAvailable)
            {
                // 版本不可用，自动留在/回到版本选择菜单，不切换到另一个版本
                s_slotSelectOpen = false;
                s_mediaMenu.setSelected((defaultSrc == nlpp::SOURCE_GAMECARD) ? 1 : 0);
            }
            else
            {
                int defaultSlot = cfg::config["default_slot"];
                if (defaultSlot < 0 || defaultSlot >= nlpp::NUM_SLOTS) defaultSlot = 0;

                const auto &slotData = nlpp::g_saveMgr.getSlotData(defaultSlot);
                if (slotData.empty)
                {
                    // 槽位不可用（为空），自动进入当前版本的槽位选择菜单，而不是选择另一个版本
                    s_slotSelectOpen = true;
                    s_slotMenu.setSelected(defaultSlot);
                }
                else
                {
                    // 版本与槽位均可用，自动加载进入槽位编辑器
                    s_slotSelectOpen = true;
                    s_slotMenu.setSelected(defaultSlot);
                    nlpp::g_saveMgr.selectSlot(defaultSlot);
                    ui::editorInit(defaultSlot);
                    ui::state = SLOT_EDITOR;
                }
            }
        }
    }

    void saveSelectExit()
    {
        s_slotSelectOpen = false;
        s_slotCopyOpen = false;
    }

    static void onConfirmEnterFriendSlot_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        int slot = s_slotMenu.getSelected();
        nlpp::g_saveMgr.selectSlot(slot);
        ui::editorInit(slot);
        ui::state = SLOT_EDITOR;
        t->finished = true;
    }

    void saveSelectUpdate()
    {
        uint32_t down = ui::padKeysDown();

        bool cardIns = false;
        if (R_SUCCEEDED(FSUSER_CardSlotIsInserted(&cardIns)))
        {
            if (cardIns != s_lastCardInserted)
            {
                s_lastCardInserted = cardIns;
                nlpp::g_saveMgr.detectSources();
                if (s_slotSelectOpen && s_currentSource == nlpp::SOURCE_GAMECARD && !cardIns)
                {
                    nlpp::g_saveMgr.closeSaveArchive();
                    s_slotSelectOpen = false;
                    ui::showMessage(getTxt("游戏卡带已拔出！"));
                    return;
                }
            }
        }

        if (s_slotCopyOpen)
        {
            if (down & KEY_B)
            {
                s_slotCopyOpen = false;
                return;
            }
            if (down & KEY_A)
            {
                onConfirmSlotCopy(nullptr);
                return;
            }
            s_copyDstMenu.update();
            return;
        }

        if (!s_slotSelectOpen)
        {
            // Page switching (L / R / Circle Pad)
            if (down & KEY_PAGE_LEFT)
            {
                ui::state = SET;
                return;
            }
            else if (down & KEY_PAGE_RIGHT)
            {
                ui::state = BACKUP_MGR;
                backupMgrRefresh();
                return;
            }

            if (down & KEY_A)
            {
                nlpp::g_saveMgr.detectSources();
                int mediaSel = s_mediaMenu.getSelected();
                nlpp::SaveSource targetSrc = (mediaSel == 0) ? nlpp::SOURCE_SD : nlpp::SOURCE_GAMECARD;
                bool available = (targetSrc == nlpp::SOURCE_GAMECARD) ? nlpp::g_saveMgr.hasGameCard() : nlpp::g_saveMgr.hasSD();

                if (!available)
                {
                    const char *failMsg = (mediaSel == 0) ?
                        getTxt("未检测到数字版存档！") :
                        getTxt("未插入游戏卡带，或未检测到卡带存档！");
                    ui::showMessage(failMsg);
                    return;
                }

                nlpp::g_saveMgr.switchSource(targetSrc);
                s_currentSource = targetSrc;
                refreshSlotMenu();
                s_slotSelectOpen = true;
                s_slotMenu.setSelected(0);
                return;
            }

            s_mediaMenu.update();
        }
        else
        {
            // Lower screen slot selection
            if (down & KEY_B)
            {
                s_slotSelectOpen = false;
                return;
            }

            if (down & KEY_A)
            {
                int slot = s_slotMenu.getSelected();
                if (nlpp::g_saveMgr.isSlotEmpty(slot))
                {
                    ui::showMessage(getTxt("该槽位为空，无法编辑！"));
                    return;
                }
                const auto &slotData = nlpp::g_saveMgr.getSlotData(slot);
                if (slotData.isFriendMode())
                {
                    ui::confirm(getTxt("当前存档槽位处于朋友模式。\n修改功能主要为恋人模式设计，\n确定要继续编辑吗？"),
                                onConfirmEnterFriendSlot_t, nullptr, nullptr);
                    return;
                }
                nlpp::g_saveMgr.selectSlot(slot);
                ui::editorInit(slot);
                ui::state = SLOT_EDITOR;
                return;
            }

            if (down & KEY_X)
            {
                int slot = s_slotMenu.getSelected();
                if (nlpp::g_saveMgr.isSlotEmpty(slot))
                {
                    ui::showMessage(getTxt("该槽位为空，无法复制！"));
                    return;
                }
                s_copySrcSlot = slot;
                s_slotCopyOpen = true;
                s_copyDstMenu.setSelected((slot + 1) % nlpp::NUM_SLOTS);
                return;
            }

            if (down & KEY_Y)
            {
                ui::confirm(getTxt("确定要备份此存档吗？"), onConfirmBackupSlot_t, nullptr, nullptr);
                return;
            }

            if (down & KEY_SELECT)
            {
                int slot = s_slotMenu.getSelected();
                if (nlpp::g_saveMgr.isSlotEmpty(slot))
                {
                    ui::showMessage(getTxt("该槽位已为空槽！"));
                    return;
                }
                s_clearTargetSlot = slot;
                char msg[128];
                snprintf(msg, sizeof(msg), getTxt("确定要清空槽位 %d 吗？"), slot + 1);
                ui::confirm(msg, onConfirmClearSlot_t, nullptr, nullptr);
                return;
            }

            s_slotMenu.update();
        }
    }

    void saveSelectDrawTop()
    {
        ui::drawUIBar(TITLE_TEXT + " - " + getTxt("存档"), ui::SCREEN_TOP, true);
        s_mediaMenu.draw(0, 22, gfx::txtCont, 400);
    }

    void saveSelectDrawBot()
    {
        if (s_slotCopyOpen)
        {
            char prompt[64];
            snprintf(prompt, sizeof(prompt), "%s %d -> %s", getTxt("复制槽位"), s_copySrcSlot + 1, getTxt("选择目标槽位"));
            gfx::drawText(prompt, 20, 16, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            s_copyDstMenu.draw(10, 50, gfx::txtCont, 300, true);
            ui::drawUIBar(getTxt("\ue000 确定  \ue001 取消"), ui::SCREEN_BOT, true);
            return;
        }

        if (!s_slotSelectOpen)
        {
            // Clean bottom screen, no redundant elements
            ui::drawUIBar(getTxt("\ue000 选择  \ue01A\ue077\ue019 切换页面"), ui::SCREEN_BOT, true);
        }
        else
        {
            s_slotMenu.draw(10, 22, gfx::txtCont, 300, true);
            ui::drawUIBar(getTxt("\ue000 选择  \ue002 复制  \ue003 备份  SELECT 清除  \ue001 关闭"), ui::SCREEN_BOT, true);
        }
    }

    // Backup Management

    static ui::menu s_bakMenu;
    static std::vector<nlpp::BackupInfo> s_backups;
    static bool s_restoreMenuOpen = false;
    static ui::menu s_restoreMenu;

    void backupMgrRefresh()
    {
        s_backups = nlpp::g_saveMgr.listBackups();
        s_bakMenu.reset();
        for (const auto &b : s_backups)
        {
            s_bakMenu.addOpt(b.dirName, 400);
        }
    }

    void backupMgrInit(void *)
    {
        backupMgrRefresh();
        s_restoreMenu.reset();
        s_restoreMenu.setPaging(false);
        s_restoreMenu.addOpt(getTxt("恢复到数字版"), 300);
        s_restoreMenu.addOpt(getTxt("恢复到实体版"), 300);
        s_restoreMenuOpen = false;
    }

    void backupMgrExit()
    {
        s_restoreMenuOpen = false;
    }

    static void onConfirmRestore_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        int dstChoice = s_restoreMenu.getSelected();
        nlpp::SaveSource targetSource = (dstChoice == 0) ? nlpp::SOURCE_SD : nlpp::SOURCE_GAMECARD;

        if (targetSource == nlpp::SOURCE_GAMECARD)
        {
            nlpp::g_saveMgr.detectSources();
            if (!nlpp::g_saveMgr.hasGameCard())
            {
                ui::showMessage(getTxt("未插入游戏卡带，或未检测到卡带存档！"));
                s_restoreMenuOpen = false;
                t->finished = true;
                return;
            }
        }

        int sel = s_bakMenu.getSelected();
        if (sel >= 0 && sel < (int)s_backups.size())
        {
            nlpp::g_saveMgr.switchSource(targetSource);
            // Auto-backup current state before restoring over it
            if (cfg::config["autobackup"])
            {
                nlpp::g_saveMgr.createAutoBackup(nullptr);
            }
            if (nlpp::g_saveMgr.restoreBackup(s_backups[sel].fullPath) && nlpp::g_saveMgr.commitChanges())
            {
                nlpp::g_saveMgr.reloadAll();
                refreshSlotMenu();
                ui::showMessage(getTxt("存档恢复成功！"));
            }
            else
            {
                ui::showMessage(getTxt("存档恢复失败！"));
            }
        }
        s_restoreMenuOpen = false;
        t->finished = true;
    }

    static void onConfirmDelete_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        int sel = s_bakMenu.getSelected();
        if (sel >= 0 && sel < (int)s_backups.size())
        {
            if (util::endsWith(s_backups[sel].fullPath, std::string(".zip")))
            {
                FSUSER_DeleteFile(fs::getSDMCArch(), fsMakePath(PATH_ASCII, s_backups[sel].fullPath.c_str()));
            }
            else
            {
                fs::delDirRec(fs::getSDMCArch(), util::toUtf16(s_backups[sel].fullPath));
            }
            backupMgrRefresh();
            ui::showMessage(getTxt("备份已删除！"));
        }
        t->finished = true;
    }

    void backupMgrUpdate()
    {
        uint32_t down = ui::padKeysDown();

        if (s_restoreMenuOpen)
        {
            if (down & KEY_B)
            {
                s_restoreMenuOpen = false;
                return;
            }
            if (down & KEY_A)
            {
                std::string targetDesc = (s_restoreMenu.getSelected() == 0) ? getTxt("数字版") : getTxt("实体版");
                char msg[128];
                snprintf(msg, sizeof(msg), getTxt("确定要恢复此备份到 %s 吗？"), targetDesc.c_str());
                ui::confirm(msg, onConfirmRestore_t, nullptr, nullptr);
                return;
            }
            s_restoreMenu.update();
            return;
        }

        if (down & KEY_PAGE_LEFT)
        {
            ui::state = SAVE_SELECT;
            return;
        }
        else if (down & KEY_PAGE_RIGHT)
        {
            ui::state = SET;
            return;
        }

        if (s_backups.empty()) return;

        if (down & KEY_A)
        {
            s_restoreMenuOpen = true;
            return;
        }

        if (down & KEY_X)
        {
            int sel = s_bakMenu.getSelected();
            char msg[128];
            snprintf(msg, sizeof(msg), getTxt("确定要删除备份 %s 吗？"), s_backups[sel].dirName.c_str());
            ui::confirm(msg, onConfirmDelete_t, nullptr, nullptr);
            return;
        }

        s_bakMenu.update();
    }

    void backupMgrDrawTop()
    {
        ui::drawUIBar(TITLE_TEXT + " - " + getTxt("备份"), ui::SCREEN_TOP, true);

        if (s_backups.empty())
        {
            gfx::drawText(getTxt("未找到存档备份。"), 20, 40, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            return;
        }

        s_bakMenu.draw(0, 22, gfx::txtCont, 400, true);
    }

    void backupMgrDrawBot()
    {
        if (s_restoreMenuOpen)
        {
            gfx::drawText(getTxt("请选择恢复目标类型："), 20, 20, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            s_restoreMenu.draw(10, 60, gfx::txtCont, 300, true);
            ui::drawUIBar(getTxt("\ue000 确定  \ue001 取消"), ui::SCREEN_BOT, true);
            return;
        }

        if (!s_backups.empty())
        {
            int sel = s_bakMenu.getSelected();
            const auto &b = s_backups[sel];
            char infoBuf[256];
            const char *srcType = (b.dirName.rfind("D_", 0) == 0) ? "数字版" :
                                  (b.dirName.rfind("C_", 0) == 0) ? "实体版" : "未知";
            snprintf(infoBuf, sizeof(infoBuf), "%s: %s\n%s: %s\n%s: %s",
                     getTxt("备份名称"), b.dirName.c_str(),
                     getTxt("备份来源"), getTxt(srcType),
                     getTxt("存储路径"), b.fullPath.c_str());
            gfx::drawTextWrap(infoBuf, 16, 20, GFX_DEPTH_DEFAULT, 0.5f, 288, gfx::txtCont);
        }

        ui::drawUIBar(getTxt("\ue000 恢复  \ue002 删除  \ue01A\ue077\ue019 切换页面"), ui::SCREEN_BOT, true);
    }

    // Slot Editor
    static int s_editorSlot = 0;
    static int s_editorPage = 0; // 0: Profile, 1: Readings, 2: Hairstyle, 3: Custom Hair Color, 4: Misc

    // Custom hair color state (Page 3)
    static uint8_t s_customR = 38;
    static uint8_t s_customG = 38;
    static uint8_t s_customB = 41;
    static int s_customChannelSel = 0; // 0: R, 1: G, 2: B, 3: HEX, 4: ResetStd, 5: Reload, 6: Save
    static int s_dpadRepeatCount = 0;

    static ui::menu s_editorMenu;
    static bool s_editorSubMenuOpen = false;
    static ui::menu s_editorSubMenu;
    static int s_subAction = 0;

    // Submenu actions
    enum SubAction
    {
        ACTION_NONE = 0,
        ACTION_BIRTH_MONTH,
        ACTION_BIRTH_DAY,
        ACTION_BLOOD_TYPE,
        ACTION_PREFECTURE,
        ACTION_CALL_SELECTOR,
        ACTION_READING_PICKER,
        ACTION_HAIR_LENGTH,
        ACTION_HAIR_STYLE,
        ACTION_HAIR_COLOR,
        ACTION_COUPLE_TITLE
    };

    static std::vector<nlpp::HairStyleOption> s_curStyleOptions;
    static std::vector<nlpp::HairColorOption> s_curColorOptions;
    static std::vector<const nlpp::ReadingEntry *> s_curReadingMatches;
    static int s_targetCallIndex = 0;

    static int s_editorLastSel = 0;

    static std::string formatBirthday(uint8_t month, uint8_t day)
    {
        char buf[32];
        if (cfg::config["uilang"] == 1) // English: MM / DD
        {
            snprintf(buf, sizeof(buf), "%02d / %02d", month, day);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%d月%d日", month, day);
        }
        return std::string(buf);
    }

    static void refreshEditorPageMenu(int keepSel = -1)
    {
        int prevSel = (keepSel >= 0) ? keepSel : s_editorLastSel;
        s_editorMenu.reset();
        auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);

        if (s_editorPage == 0) // 1. 玩家信息
        {
            s_editorMenu.addOpt(std::string(getTxt("姓氏")) + ": " + sd.profile.lastName, 300);
            s_editorMenu.addOpt(std::string(getTxt("名字")) + ": " + sd.profile.firstName, 300);
            s_editorMenu.addOpt(std::string(getTxt("通讯昵称(HN)")) + ": " + sd.profile.hn, 300);

            char bdayBuf[64];
            snprintf(bdayBuf, sizeof(bdayBuf), "%s: %s", getTxt("生日"), formatBirthday(sd.profile.birthMonth, sd.profile.birthDay).c_str());
            s_editorMenu.addOpt(bdayBuf, 300);

            s_editorMenu.addOpt(std::string(getTxt("血型")) + ": " + nlpp::Dict::getBloodTypeName(sd.profile.blood), 300);

            s_editorMenu.addOpt(std::string(getTxt("出身地")) + ": " + nlpp::Dict::getPrefectureName(sd.profile.prefecture), 300);

            s_editorMenu.addOpt(getTxt("★ 重新加载"), 300);
            s_editorMenu.addOpt(getTxt("♥ 保存修改"), 300);
        }
        else if (s_editorPage == 1) // 2. 玩家读音
        {
            int curIdx = sd.stats.currentSelector < 5 ? sd.stats.currentSelector : 0;
            char curCallBuf[96];
            snprintf(curCallBuf, sizeof(curCallBuf), "%s: %d %s", getTxt("当前使用"), (int)sd.stats.currentSelector,
                     nlpp::Dict::getReadingSlotDisplay(curIdx, sd.stats.readings[curIdx]).c_str());
            s_editorMenu.addOpt(curCallBuf, 300);

            s_editorMenu.addOpt(std::string(getTxt("0 姓氏读音")) + ": " + nlpp::Dict::getReadingSlotDisplay(0, sd.stats.readings[0]), 300);
            s_editorMenu.addOpt(std::string(getTxt("1 名字读音")) + ": " + nlpp::Dict::getReadingSlotDisplay(1, sd.stats.readings[1]), 300);
            s_editorMenu.addOpt(std::string(getTxt("2 后缀读音")) + ": " + nlpp::Dict::getReadingSlotDisplay(2, sd.stats.readings[2]), 300);
            s_editorMenu.addOpt(std::string(getTxt("3 预设读音")) + ": " + nlpp::Dict::getReadingSlotDisplay(3, sd.stats.readings[3]), 300);
            s_editorMenu.addOpt(std::string(getTxt("4 特殊读音")) + ": " + nlpp::Dict::getReadingSlotDisplay(4, sd.stats.readings[4]), 300);

            s_editorMenu.addOpt(getTxt("★ 重新加载"), 300);
            s_editorMenu.addOpt(getTxt("♥ 保存修改"), 300);
        }
        else if (s_editorPage == 2) // 3. 女友发型
        {
            s_editorMenu.addOpt(std::string(getTxt("发长")) + ": " + nlpp::Dict::getHairLengthName(sd.girlfriend.heroine, sd.girlfriend.hairLength), 300);
            s_editorMenu.addOpt(std::string(getTxt("发型")) + ": " + nlpp::getCleanStyleName(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId), 300);
            s_editorMenu.addOpt(std::string(getTxt("发色")) + ": " + (nlpp::Dict::isCustomHairColor(sd.girlfriend.heroine, sd.girlfriend.hairR, sd.girlfriend.hairG, sd.girlfriend.hairB, sd.girlfriend.hairColorActive)
                     ? getTxt("自定义发色")
                     : nlpp::getCleanColorName(sd.girlfriend.heroine, sd.girlfriend.hairColorId)), 300);
            s_editorMenu.addOpt(getTxt("重置为初始发型"), 300);

            s_editorMenu.addOpt(getTxt("★ 重新加载"), 300);
            s_editorMenu.addOpt(getTxt("♥ 保存修改"), 300);

            // Live update portraits (style and color)
            nlpp::portraitLoadByStyle(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId);
            nlpp::portraitLoadByColor(sd.girlfriend.heroine, sd.girlfriend.hairColorId);
        }
        else if (s_editorPage == 3) // 4. 自定义发色
        {
            // Custom hair color page uses custom touch controls; ensure hairstyle portrait is loaded
            nlpp::portraitLoadByStyle(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId);
        }
        else if (s_editorPage == 4) // 5. 其他
        {
            char numBuf[96];
            snprintf(numBuf, sizeof(numBuf), "%s: %d %s", getTxt("总天数"), (int)sd.timeline.timelineDays, getTxt("天"));
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d %s", getTxt("上线天数"), (int)sd.timeline.datingDays, getTxt("天"));
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d", getTxt("运动属性值"), (int)sd.stats.fitness);
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d", getTxt("知识属性值"), (int)sd.stats.knowledge);
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d", getTxt("感性属性值"), (int)sd.stats.sensibility);
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d", getTxt("魅力属性值"), (int)sd.stats.charm);
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d G", getTxt("金钱"), (int)sd.stats.rich);
            s_editorMenu.addOpt(numBuf, 300);

            snprintf(numBuf, sizeof(numBuf), "%s: %d %s", getTxt("TODO"), (int)sd.stats.todo, getTxt("点"));
            s_editorMenu.addOpt(numBuf, 300);

            if (sd.isFriendMode())
            {
                s_editorMenu.addOpt(std::string(getTxt("称号")) + ": " + getTxt("无"), 300);
            }
            else
            {
                int rank = nlpp::Dict::getCoupleRank(sd.girlfriend.coupleExp);
                char titleBuf[128];
                snprintf(titleBuf, sizeof(titleBuf), "%s: Lv.%d %s", getTxt("称号"), rank, nlpp::Dict::getCoupleTitleName(rank).c_str());
                s_editorMenu.addOpt(titleBuf, 300);
            }

            s_editorMenu.addOpt(getTxt("移除滤镜"), 300);

            s_editorMenu.addOpt(getTxt("★ 重新加载"), 300);
            s_editorMenu.addOpt(getTxt("♥ 保存修改"), 300);
        }

        if (prevSel >= 0 && (unsigned)prevSel < s_editorMenu.getCount())
        {
            s_editorMenu.setSelected(prevSel);
        }
        s_editorLastSel = s_editorMenu.getSelected();
    }

    void onLanguageChanged()
    {
        // 1. Refresh media menu (Digital / Cartridge)
        int selMedia = s_mediaMenu.getSelected();
        s_mediaMenu.reset();
        s_mediaMenu.addOpt(getTxt("数字版"), 400);
        s_mediaMenu.addOpt(getTxt("实体版"), 400);
        if (selMedia >= 0 && selMedia < 2)
            s_mediaMenu.setSelected(selMedia);

        // 2. Refresh copy destination slot menu
        int selDst = s_copyDstMenu.getSelected();
        s_copyDstMenu.reset();
        s_copyDstMenu.setPaging(false);
        for (int i = 0; i < nlpp::NUM_SLOTS; ++i)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %d", getTxt("目标槽位"), i + 1);
            s_copyDstMenu.addOpt(buf, 300);
        }
        if (selDst >= 0 && selDst < (int)s_copyDstMenu.getCount())
            s_copyDstMenu.setSelected(selDst);

        // 3. Refresh slot menu
        refreshSlotMenu();

        // 4. Refresh backup restore destination menu
        int selRestore = s_restoreMenu.getSelected();
        s_restoreMenu.reset();
        s_restoreMenu.setPaging(false);
        s_restoreMenu.addOpt(getTxt("恢复到数字版"), 300);
        s_restoreMenu.addOpt(getTxt("恢复到实体版"), 300);
        if (selRestore >= 0 && selRestore < 2)
            s_restoreMenu.setSelected(selRestore);

        // 5. Refresh editor menu if slot is loaded
        if (s_editorSlot >= 0 && s_editorSlot < nlpp::NUM_SLOTS)
        {
            refreshEditorPageMenu(s_editorLastSel);
        }
    }

    void editorInit(int slot)
    {
        s_editorSlot = slot;
        s_editorPage = 0;
        s_editorSubMenuOpen = false;
        s_subAction = ACTION_NONE;
        s_editorMenu.setPaging(false);
        s_editorMenu.setMaxVisible(11);
        s_editorSubMenu.setPaging(true);
        s_editorSubMenu.setMaxVisible(10);

        const auto &sd = nlpp::g_saveMgr.getSlotData(slot);
        s_customR = sd.girlfriend.hairR;
        s_customG = sd.girlfriend.hairG;
        s_customB = sd.girlfriend.hairB;
        s_customChannelSel = 0;
        s_dpadRepeatCount = 0;

        nlpp::portraitInit();
        nlpp::portraitLoadByStyle(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId);
        nlpp::portraitLoadByColor(sd.girlfriend.heroine, sd.girlfriend.hairColorId);
        refreshEditorPageMenu(0);
    }

    void editorExit()
    {
        s_editorSubMenuOpen = false;
        s_subAction = ACTION_NONE;
    }

    static void onConfirmSaveSlot_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        if (cfg::config["autobackup"])
        {
            nlpp::g_saveMgr.createAutoBackup(nullptr);
        }
        nlpp::g_saveMgr.serializeSlot(s_editorSlot);
        if (nlpp::g_saveMgr.commitChanges())
        {
            nlpp::g_saveMgr.reloadAll();
            const auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);
            nlpp::portraitLoadByStyle(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId);
            nlpp::portraitLoadByColor(sd.girlfriend.heroine, sd.girlfriend.hairColorId);
            s_customR = sd.girlfriend.hairR;
            s_customG = sd.girlfriend.hairG;
            s_customB = sd.girlfriend.hairB;
            refreshSlotMenu();
            refreshEditorPageMenu(s_editorLastSel);
            ui::showMessage(getTxt("修改保存成功！"));
        }
        else
        {
            ui::showMessage(getTxt("修改保存失败！"));
        }
        t->finished = true;
    }

    static void onConfirmSaveSlot(void *)
    {
        if (nlpp::g_saveMgr.getSource() == nlpp::SOURCE_GAMECARD)
        {
            bool ins = false;
            if (R_FAILED(FSUSER_CardSlotIsInserted(&ins)) || !ins)
            {
                ui::showMessage(getTxt("未插入游戏卡带，或未检测到卡带存档！"));
                return;
            }
        }
        ui::confirm(getTxt("确定要保存当前槽位的修改吗？"), onConfirmSaveSlot_t, nullptr, nullptr);
    }

    static void onConfirmReloadSlot(void *)
    {
        nlpp::g_saveMgr.reloadAll();
        const auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);
        nlpp::portraitLoadByStyle(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId);
        nlpp::portraitLoadByColor(sd.girlfriend.heroine, sd.girlfriend.hairColorId);
        s_customR = sd.girlfriend.hairR;
        s_customG = sd.girlfriend.hairG;
        s_customB = sd.girlfriend.hairB;
        refreshSlotMenu();
        refreshEditorPageMenu(s_editorLastSel);
        ui::showMessage(getTxt("已重新加载当前存档数据！"));
    }

    static void onConfirmResetHair_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);

        // Default initial hair length and style per heroine:
        // Manaka: Long (2), Rinko: Short (0), Nene: Medium (1)
        if (sd.girlfriend.heroine == nlpp::HEROINE_MANAKA)
        {
            sd.girlfriend.hairLength = 2;
            sd.girlfriend.hairStyleId = 0;
            sd.girlfriend.hairColorId = 0;
            sd.girlfriend.hairTasteTarget = 0;
        }
        else if (sd.girlfriend.heroine == nlpp::HEROINE_RINKO)
        {
            sd.girlfriend.hairLength = 0;
            sd.girlfriend.hairStyleId = 0;
            sd.girlfriend.hairColorId = 0;
            sd.girlfriend.hairTasteTarget = 1;
        }
        else if (sd.girlfriend.heroine == nlpp::HEROINE_NENE)
        {
            sd.girlfriend.hairLength = 1;
            sd.girlfriend.hairStyleId = 0;
            sd.girlfriend.hairColorId = 0;
            sd.girlfriend.hairTasteTarget = 0;
        }

        uint8_t r = 0, g = 0, b = 0;
        nlpp::Dict::getDefaultHairRGB(sd.girlfriend.heroine, r, g, b);
        sd.girlfriend.hairR = r;
        sd.girlfriend.hairG = g;
        sd.girlfriend.hairB = b;
        sd.girlfriend.nextHairR = r;
        sd.girlfriend.nextHairG = g;
        sd.girlfriend.nextHairB = b;
        sd.girlfriend.nextHairColorId = 0;
        sd.girlfriend.hairColorActive = 0xFF; // 0xFF = default initial state (unactivated dyeing)
        sd.girlfriend.reservedStyleId = 0xFF; // 0xFF = unreserved

        s_customR = r;
        s_customG = g;
        s_customB = b;

        refreshEditorPageMenu();
        ui::showMessage(getTxt("已将女友的发型和发色重置为默认值！"));
        t->finished = true;
    }

    static void onConfirmExitEditor_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        editorExit();
        nlpp::g_saveMgr.selectSlot(s_editorSlot); // Discard uncommitted in-memory edits
        s_slotSelectOpen = true;
        refreshSlotMenu();
        s_slotMenu.setSelected(s_editorSlot);
        ui::state = SAVE_SELECT;
        t->finished = true;
    }

    static bool parseHexColor(const std::string &str, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        std::string s = str;
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        std::string clean;
        for (char c : s)
        {
            if (!isspace((unsigned char)c)) clean += c;
        }
        if (clean.length() != 6) return false;
        for (char c : clean)
        {
            if (!isxdigit((unsigned char)c)) return false;
        }
        unsigned long val = std::strtoul(clean.c_str(), nullptr, 16);
        r = (uint8_t)((val >> 16) & 0xFF);
        g = (uint8_t)((val >> 8) & 0xFF);
        b = (uint8_t)(val & 0xFF);
        return true;
    }

    static void promptHexInput()
    {
        char hexBuf[16];
        snprintf(hexBuf, sizeof(hexBuf), "%02X%02X%02X", s_customR, s_customG, s_customB);
        std::string in;
        bool ok = false;
        if (cfg::config["use_builtinkbd"])
        {
            ok = ui::showKeyboard(getTxt("输入 HEX 值 (#39C5BB 或 39C5BB)"), hexBuf, 10, in, ui::editorDrawTop, ui::KBD_MODE_EN);
        }
        else
        {
            in = util::getString(getTxt("输入 HEX 值 (#39C5BB 或 39C5BB)"), false);
            ok = !in.empty();
        }
        if (ok)
        {
            uint8_t r = 0, g = 0, b = 0;
            if (parseHexColor(in, r, g, b))
            {
                s_customR = r;
                s_customG = g;
                s_customB = b;
            }
            else
            {
                ui::showMessage(getTxt("无效的 HEX 值！"));
            }
        }
    }

    static void onConfirmReloadPresetColor_t(void *a)
    {
        threadInfo *t = (threadInfo *)a;
        const auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);
        uint8_t r = 0, g = 0, b = 0;
        if (sd.girlfriend.hairColorId >= 1 && sd.girlfriend.hairColorId <= 19)
        {
            const auto &def = nlpp::Dict::getHairColor(sd.girlfriend.hairColorId);
            r = def.r;
            g = def.g;
            b = def.b;
        }
        else
        {
            nlpp::Dict::getDefaultHairRGB(sd.girlfriend.heroine, r, g, b);
        }
        s_customR = r;
        s_customG = g;
        s_customB = b;
        ui::showMessage(getTxt("已重新加载预设发色的 RGB！"));
        t->finished = true;
    }

    static void saveCustomHairColor()
    {
        auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);
        uint8_t defR = 0, defG = 0, defB = 0;
        nlpp::Dict::getDefaultHairRGB(sd.girlfriend.heroine, defR, defG, defB);
        if (s_customR == defR && s_customG == defG && s_customB == defB)
        {
            sd.girlfriend.hairColorId = 0;
            sd.girlfriend.nextHairColorId = 0;
            sd.girlfriend.hairColorActive = 0xFF;
        }
        else
        {
            uint8_t matchedId = nlpp::Dict::matchClosestColorId(s_customR, s_customG, s_customB);
            sd.girlfriend.hairColorId = matchedId;
            sd.girlfriend.nextHairColorId = matchedId;
            sd.girlfriend.hairColorActive = 0x00;
        }

        sd.girlfriend.hairR = s_customR;
        sd.girlfriend.hairG = s_customG;
        sd.girlfriend.hairB = s_customB;
        sd.girlfriend.nextHairR = s_customR;
        sd.girlfriend.nextHairG = s_customG;
        sd.girlfriend.nextHairB = s_customB;

        nlpp::portraitLoadByColor(sd.girlfriend.heroine, sd.girlfriend.hairColorId);
        onConfirmSaveSlot(nullptr);
    }

    static void editorDrawCustomColorBot()
    {
        bool isLight = (bool)cfg::config["lightback"];
        u32 trackBg = isLight ? C2D_Color32(218, 218, 218, 255) : C2D_Color32(40, 40, 40, 255);
        u32 trackBorder = isLight ? C2D_Color32(185, 185, 185, 255) : C2D_Color32(70, 70, 70, 255);
        u32 knobBorder = isLight ? C2D_Color32(120, 120, 120, 255) : C2D_Color32(25, 25, 25, 255);
        u32 knobFill = isLight ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(240, 240, 240, 255);

        for (int i = 0; i < 7; ++i)
        {
            int y = 20 + i * 26;

            if (s_customChannelSel == i)
            {
                gfx::drawBoundingBox(10, y, 300, 22, GFX_DEPTH_DEFAULT, 0xFFC5FF00);
                C2D_DrawRectSolid(14, y + 4, GFX_DEPTH_DEFAULT, 2, 14, 0xFFC5FF00);
            }

            if (i < 3)
            {
                uint8_t val = (i == 0) ? s_customR : (i == 1) ? s_customG : s_customB;
                const char *cName = (i == 0) ? "R" : (i == 1) ? "G" : "B";
                gfx::drawText(cName, 24, y + 3, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);

                int trackX = 46;
                int trackW = 202;
                int trackY = y + 4;
                int trackH = 14;

                C2D_DrawRectSolid(trackX, trackY, GFX_DEPTH_DEFAULT, trackW, trackH, trackBg);
                C2D_DrawRectSolid(trackX, trackY, GFX_DEPTH_DEFAULT, trackW, 1, trackBorder);
                C2D_DrawRectSolid(trackX, trackY + trackH - 1, GFX_DEPTH_DEFAULT, trackW, 1, trackBorder);

                int fillW = (int)((val / 255.0f) * trackW);
                if (fillW > 0)
                {
                    u32 fillCol;
                    if (isLight)
                    {
                        fillCol = (i == 0) ? C2D_Color32(std::max(160, (int)val), 65, 75, 255) :
                                  (i == 1) ? C2D_Color32(50, std::max(150, (int)val), 70, 255) :
                                             C2D_Color32(55, 105, std::max(165, (int)val), 255);
                    }
                    else
                    {
                        fillCol = (i == 0) ? C2D_Color32(std::max(80, (int)val), 35, 45, 255) :
                                  (i == 1) ? C2D_Color32(35, std::max(80, (int)val), 45, 255) :
                                             C2D_Color32(35, 55, std::max(90, (int)val), 255);
                    }
                    C2D_DrawRectSolid(trackX, trackY, GFX_DEPTH_DEFAULT, fillW, trackH, fillCol);
                }

                int knobX = trackX + fillW - 2;
                if (knobX < trackX) knobX = trackX;
                if (knobX > trackX + trackW - 4) knobX = trackX + trackW - 4;

                C2D_DrawRectSolid(knobX - 1, y + 1, GFX_DEPTH_DEFAULT, 6, 20, knobBorder);
                C2D_DrawRectSolid(knobX, y + 2, GFX_DEPTH_DEFAULT, 4, 18, knobFill);

                char vBuf[16];
                snprintf(vBuf, sizeof(vBuf), "%3d", (int)val);
                gfx::drawText(vBuf, 260, y + 3, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            }
            else if (i == 3)
            {
                char hexStr[32];
                snprintf(hexStr, sizeof(hexStr), "%s: #%02X%02X%02X", getTxt("HEX"), s_customR, s_customG, s_customB);
                gfx::drawText(hexStr, 24, y + 3, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            }
            else if (i == 4)
            {
                gfx::drawText(getTxt("重新加载预设发色"), 24, y + 3, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            }
            else if (i == 5)
            {
                gfx::drawText(getTxt("★ 重新加载"), 24, y + 3, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            }
            else if (i == 6)
            {
                gfx::drawText(getTxt("♥ 保存修改"), 24, y + 3, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            }
        }
    }

    static void handleCustomColorUpdate(uint32_t down, uint32_t held)
    {
        if (down & KEY_UP)
        {
            if (s_customChannelSel > 0) s_customChannelSel--;
            else s_customChannelSel = 6;
        }
        else if (down & KEY_DOWN)
        {
            if (s_customChannelSel < 6) s_customChannelSel++;
            else s_customChannelSel = 0;
        }
        else if (down & KEY_DLEFT)
        {
            if (s_customChannelSel == 0 && s_customR > 0) s_customR--;
            else if (s_customChannelSel == 1 && s_customG > 0) s_customG--;
            else if (s_customChannelSel == 2 && s_customB > 0) s_customB--;
        }
        else if (down & KEY_DRIGHT)
        {
            if (s_customChannelSel == 0 && s_customR < 255) s_customR++;
            else if (s_customChannelSel == 1 && s_customG < 255) s_customG++;
            else if (s_customChannelSel == 2 && s_customB < 255) s_customB++;
        }

        if (held & (KEY_DLEFT | KEY_DRIGHT))
        {
            s_dpadRepeatCount++;
            if (s_dpadRepeatCount > 16 && (s_dpadRepeatCount % 3 == 0))
            {
                int step = (s_dpadRepeatCount > 50) ? 5 : 1;
                if (held & KEY_DLEFT)
                {
                    if (s_customChannelSel == 0) s_customR = (s_customR > step) ? s_customR - step : 0;
                    else if (s_customChannelSel == 1) s_customG = (s_customG > step) ? s_customG - step : 0;
                    else if (s_customChannelSel == 2) s_customB = (s_customB > step) ? s_customB - step : 0;
                }
                else if (held & KEY_DRIGHT)
                {
                    if (s_customChannelSel == 0) s_customR = (s_customR + step < 255) ? s_customR + step : 255;
                    else if (s_customChannelSel == 1) s_customG = (s_customG + step < 255) ? s_customG + step : 255;
                    else if (s_customChannelSel == 2) s_customB = (s_customB + step < 255) ? s_customB + step : 255;
                }
            }
        }
        else
        {
            s_dpadRepeatCount = 0;
        }

        if (down & KEY_A)
        {
            if (s_customChannelSel == 0)
            {
                int v = util::getInt(getTxt("输入 R 值 (0-255)"), s_customR, 255);
                if (v >= 0) s_customR = (uint8_t)v;
            }
            else if (s_customChannelSel == 1)
            {
                int v = util::getInt(getTxt("输入 G 值 (0-255)"), s_customG, 255);
                if (v >= 0) s_customG = (uint8_t)v;
            }
            else if (s_customChannelSel == 2)
            {
                int v = util::getInt(getTxt("输入 B 值 (0-255)"), s_customB, 255);
                if (v >= 0) s_customB = (uint8_t)v;
            }
            else if (s_customChannelSel == 3)
            {
                promptHexInput();
            }
            else if (s_customChannelSel == 4)
            {
                ui::confirm(getTxt("确定要重新加载预设发色吗？"), onConfirmReloadPresetColor_t, nullptr, nullptr);
            }
            else if (s_customChannelSel == 5)
            {
                onConfirmReloadSlot(nullptr);
            }
            else if (s_customChannelSel == 6)
            {
                saveCustomHairColor();
            }
        }
    }

    void editorUpdate()
    {
        uint32_t down = ui::padKeysDown();
        auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);

        // Submenu active
        if (s_editorSubMenuOpen)
        {
            if (down & KEY_B)
            {
                s_editorSubMenuOpen = false;
                s_subAction = ACTION_NONE;
                refreshEditorPageMenu();
                return;
            }

            // Real-time portrait preview while browsing style / color submenus
            if (s_subAction == ACTION_HAIR_STYLE)
            {
                int sel = s_editorSubMenu.getSelected();
                if (sel >= 0 && sel < (int)s_curStyleOptions.size())
                {
                    nlpp::portraitLoadStyleIndex(sd.girlfriend.heroine, s_curStyleOptions[sel].portraitIndex);
                }
            }
            else if (s_subAction == ACTION_HAIR_COLOR)
            {
                int sel = s_editorSubMenu.getSelected();
                if (sel >= 0 && sel < (int)s_curColorOptions.size())
                {
                    nlpp::portraitLoadColorIndex(sd.girlfriend.heroine, s_curColorOptions[sel].portraitIndex);
                }
            }

            if (down & KEY_A)
            {
                int sel = s_editorSubMenu.getSelected();
                if (s_subAction == ACTION_BIRTH_MONTH)
                {
                    sd.profile.birthMonth = sel + 1;
                    s_subAction = ACTION_BIRTH_DAY;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    static const int daysInMon[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                    int maxD = daysInMon[sd.profile.birthMonth - 1];
                    for (int d = 1; d <= maxD; ++d)
                    {
                        char dBuf[16];
                        if (cfg::config["uilang"] == 1)
                            snprintf(dBuf, sizeof(dBuf), "%02d", d);
                        else
                            snprintf(dBuf, sizeof(dBuf), "%d日", d);
                        s_editorSubMenu.addOpt(dBuf, 300);
                    }
                    s_editorSubMenu.setSelected(std::min((int)sd.profile.birthDay - 1, maxD - 1));
                    return;
                }
                else if (s_subAction == ACTION_BIRTH_DAY)
                {
                    sd.profile.birthDay = sel + 1;
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_BLOOD_TYPE)
                {
                    if (sel >= 0 && sel < 4)
                    {
                        sd.profile.blood = (nlpp::BloodType)sel;
                    }
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_PREFECTURE)
                {
                    sd.profile.prefecture = sel;
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_CALL_SELECTOR)
                {
                    sd.stats.currentSelector = sel;
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_READING_PICKER)
                {
                    if (sel >= 0 && sel < (int)s_curReadingMatches.size())
                    {
                        sd.stats.readings[s_targetCallIndex] = s_curReadingMatches[sel]->id;
                    }
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_HAIR_LENGTH)
                {
                    auto validStages = nlpp::Dict::getValidLengthStages(sd.girlfriend.heroine);
                    if (sel >= 0 && sel < (int)validStages.size())
                    {
                        sd.girlfriend.hairLength = validStages[sel];
                        // If styleId is no longer valid, fallback to 0
                        auto styles = nlpp::getStyleOptions(sd.girlfriend.heroine, sd.girlfriend.hairLength);
                        bool valid = false;
                        for (const auto &s : styles) { if (s.styleId == sd.girlfriend.hairStyleId) { valid = true; break; } }
                        if (!valid && !styles.empty()) sd.girlfriend.hairStyleId = styles[0].styleId;
                    }
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_HAIR_STYLE)
                {
                    if (sel >= 0 && sel < (int)s_curStyleOptions.size())
                    {
                        sd.girlfriend.hairStyleId = s_curStyleOptions[sel].styleId;
                    }
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_HAIR_COLOR)
                {
                    if (sel >= 0 && sel < (int)s_curColorOptions.size())
                    {
                        sd.girlfriend.hairColorId = s_curColorOptions[sel].colorId;
                        const auto &def = nlpp::Dict::getHairColor(sd.girlfriend.hairColorId);
                        sd.girlfriend.hairR = def.r;
                        sd.girlfriend.hairG = def.g;
                        sd.girlfriend.hairB = def.b;
                        sd.girlfriend.nextHairColorId = sd.girlfriend.hairColorId;
                        sd.girlfriend.nextHairR = def.r;
                        sd.girlfriend.nextHairG = def.g;
                        sd.girlfriend.nextHairB = def.b;
                        sd.girlfriend.hairColorActive = 0x00; // 0x00 = activate dyeing

                        s_customR = def.r;
                        s_customG = def.g;
                        s_customB = def.b;
                    }
                    s_editorSubMenuOpen = false;
                    s_subAction = ACTION_NONE;
                    refreshEditorPageMenu();
                    return;
                }
                else if (s_subAction == ACTION_COUPLE_TITLE)
                {
                    if (sel >= 0 && sel < 23)
                    {
                        sd.girlfriend.coupleExp = nlpp::Dict::getCoupleRankExp(sel + 1);
                        s_editorSubMenuOpen = false;
                        s_subAction = ACTION_NONE;
                        refreshEditorPageMenu();
                    }
                    return;
                }
            }
            s_editorSubMenu.update();
            return;
        }

        // B: Return to Slot Selection (with confirmation dialog)
        if (down & KEY_B)
        {
            ui::confirm(getTxt("确定要退出存档编辑吗？"), onConfirmExitEditor_t, nullptr, nullptr);
            return;
        }

        // L / R / Circle Pad: Page switching
        if (down & KEY_PAGE_LEFT)
        {
            s_editorPage = (s_editorPage + 4) % 5;
            refreshEditorPageMenu(0);
            return;
        }
        else if (down & KEY_PAGE_RIGHT)
        {
            s_editorPage = (s_editorPage + 1) % 5;
            refreshEditorPageMenu(0);
            return;
        }

        // Custom hair color page (Page 3)
        if (s_editorPage == 3)
        {
            uint32_t held = ui::padKeysHeld();
            handleCustomColorUpdate(down, held);
            return;
        }

        // Menu selection execution
        if (down & KEY_A)
        {
            int sel = s_editorMenu.getSelected();
            s_editorLastSel = sel;

            if (s_editorPage == 0) // 玩家信息
            {
                if (sel == 0) // 姓氏
                {
                    std::string in;
                    bool ok = false;
                    if (cfg::config["use_builtinkbd"])
                    {
                        ok = ui::showKeyboard(getTxt("输入姓氏"), sd.profile.lastName, 12, in, ui::editorDrawTop);
                    }
                    else
                    {
                        in = util::getString(getTxt("输入姓氏"), false);
                        ok = !in.empty();
                    }
                    if (ok) { sd.profile.lastName = in; refreshEditorPageMenu(); }
                }
                else if (sel == 1) // 名字
                {
                    std::string in;
                    bool ok = false;
                    if (cfg::config["use_builtinkbd"])
                    {
                        ok = ui::showKeyboard(getTxt("输入名字"), sd.profile.firstName, 12, in, ui::editorDrawTop);
                    }
                    else
                    {
                        in = util::getString(getTxt("输入名字"), false);
                        ok = !in.empty();
                    }
                    if (ok) { sd.profile.firstName = in; refreshEditorPageMenu(); }
                }
                else if (sel == 2) // 通讯昵称
                {
                    std::string in;
                    bool ok = false;
                    if (cfg::config["use_builtinkbd"])
                    {
                        ok = ui::showKeyboard(getTxt("输入通讯昵称(HN)"), sd.profile.hn, 12, in, ui::editorDrawTop);
                    }
                    else
                    {
                        in = util::getString(getTxt("输入通讯昵称(HN)"), false);
                        ok = !in.empty();
                    }
                    if (ok) { sd.profile.hn = in; refreshEditorPageMenu(); }
                }
                else if (sel == 3) // 生日
                {
                    s_subAction = ACTION_BIRTH_MONTH;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    for (int m = 1; m <= 12; ++m)
                    {
                        char mBuf[16];
                        if (cfg::config["uilang"] == 1)
                            snprintf(mBuf, sizeof(mBuf), "%02d", m);
                        else
                            snprintf(mBuf, sizeof(mBuf), "%d月", m);
                        s_editorSubMenu.addOpt(mBuf, 300);
                    }
                    s_editorSubMenu.setSelected(std::max(0, (int)sd.profile.birthMonth - 1));
                }
                else if (sel == 4) // 血型
                {
                    s_subAction = ACTION_BLOOD_TYPE;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(false);
                    s_editorSubMenu.setMaxVisible(10);
                    s_editorSubMenu.addOpt(getTxt("A型"), 300);
                    s_editorSubMenu.addOpt(getTxt("B型"), 300);
                    s_editorSubMenu.addOpt(getTxt("O型"), 300);
                    s_editorSubMenu.addOpt(getTxt("AB型"), 300);
                    s_editorSubMenu.setSelected(std::min((int)sd.profile.blood, 3));
                }
                else if (sel == 5) // 出身地
                {
                    s_subAction = ACTION_PREFECTURE;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    for (int p = 0; p < nlpp::Dict::getPrefectureCount(); ++p)
                    {
                        s_editorSubMenu.addOpt(nlpp::Dict::getPrefectureName(p), 300);
                    }
                    s_editorSubMenu.setSelected(std::min((int)sd.profile.prefecture, nlpp::Dict::getPrefectureCount() - 1));
                }
                else if (sel == 6) { onConfirmReloadSlot(nullptr); }
                else if (sel == 7) { onConfirmSaveSlot(nullptr); }
            }
            else if (s_editorPage == 1) // 玩家读音
            {
                if (sel == 0) // 当前使用槽位切换
                {
                    s_subAction = ACTION_CALL_SELECTOR;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(false);
                    s_editorSubMenu.setMaxVisible(10);
                    s_editorSubMenu.addOpt(getTxt("0 姓氏读音"), 300);
                    s_editorSubMenu.addOpt(getTxt("1 名字读音"), 300);
                    s_editorSubMenu.addOpt(getTxt("2 后缀读音"), 300);
                    s_editorSubMenu.addOpt(getTxt("3 预设读音"), 300);
                    s_editorSubMenu.addOpt(getTxt("4 特殊读音"), 300);
                    s_editorSubMenu.setSelected(std::min((int)sd.stats.currentSelector, 4));
                }
                else if (sel == 1 || sel == 2) // 0 姓氏称呼 / 1 名字称呼 (搜索 + 候选推荐)
                {
                    s_targetCallIndex = sel - 1;
                    uint8_t slotMask = 1 << s_targetCallIndex;

                    std::string query;
                    bool ok = false;
                    if (cfg::config["use_builtinkbd"])
                    {
                        ok = ui::showKeyboard(getTxt("搜索读音"), "", 24, query, ui::editorDrawTop, ui::KBD_MODE_KANA);
                    }
                    else
                    {
                        query = util::getString(getTxt("搜索读音"), false);
                        ok = true;
                    }
                    if (!ok) return;

                    s_curReadingMatches = nlpp::Dict::filterReadings(slotMask, 0, query);

                    if (s_curReadingMatches.empty())
                    {
                        ui::showMessage(getTxt("找不到匹配的读音！"));
                    }
                    else
                    {
                        s_subAction = ACTION_READING_PICKER;
                        s_editorSubMenuOpen = true;
                        s_editorSubMenu.reset();
                        s_editorSubMenu.setPaging(true);
                        s_editorSubMenu.setMaxVisible(10);
                        for (size_t i = 0; i < s_curReadingMatches.size(); ++i)
                        {
                            const auto *r = s_curReadingMatches[i];
                            std::string itemText = r->text;
                            if (!r->romaji.empty())
                            {
                                itemText += " (" + r->romaji + ")";
                            }
                            if (r->slot_mask & 0x80)
                            {
                                itemText += " ★";
                            }
                            s_editorSubMenu.addOpt(itemText, 300);
                        }
                    }
                }
                else if (sel == 3) // 2 后缀称呼 (SuffixReading)
                {
                    s_targetCallIndex = 2;
                    std::string query;
                    bool ok = false;
                    if (cfg::config["use_builtinkbd"])
                    {
                        ok = ui::showKeyboard(getTxt("搜索读音"), "", 24, query, ui::editorDrawTop, ui::KBD_MODE_KANA);
                    }
                    else
                    {
                        query = util::getString(getTxt("搜索读音"), false);
                        ok = true;
                    }
                    if (!ok) return;

                    s_curReadingMatches = nlpp::Dict::filterReadings(0x04, 0, query);

                    if (s_curReadingMatches.empty())
                    {
                        ui::showMessage(getTxt("找不到匹配的读音！"));
                    }
                    else
                    {
                        s_subAction = ACTION_READING_PICKER;
                        s_editorSubMenuOpen = true;
                        s_editorSubMenu.reset();
                        s_editorSubMenu.setPaging(true);
                        s_editorSubMenu.setMaxVisible(10);
                        for (const auto *r : s_curReadingMatches)
                        {
                            std::string itemText = r->text;
                            if (!r->romaji.empty()) itemText += " (" + r->romaji + ")";
                            s_editorSubMenu.addOpt(itemText, 300);
                        }
                    }
                }
                else if (sel == 4)
                {
                    s_targetCallIndex = 3;
                    s_curReadingMatches = nlpp::Dict::filterReadings(0x08, 0, "");
                    s_subAction = ACTION_READING_PICKER;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    int curSel = 0;
                    for (size_t i = 0; i < s_curReadingMatches.size(); ++i)
                    {
                        if (s_curReadingMatches[i]->id == sd.stats.readings[3]) curSel = (int)i;
                        std::string itemText = s_curReadingMatches[i]->text;
                        if (!s_curReadingMatches[i]->romaji.empty())
                            itemText += " (" + s_curReadingMatches[i]->romaji + ")";
                        s_editorSubMenu.addOpt(itemText, 300);
                    }
                    s_editorSubMenu.setSelected(curSel);
                }
                else if (sel == 5)
                {
                    s_targetCallIndex = 4;
                    uint8_t heroineMask = (sd.girlfriend.heroine >= 0 && sd.girlfriend.heroine <= 2) ?
                                          (1 << (uint8_t)sd.girlfriend.heroine) : 0;

                    std::string query;
                    bool ok = false;
                    if (cfg::config["use_builtinkbd"])
                    {
                        ok = ui::showKeyboard(getTxt("搜索读音"), "", 24, query, ui::editorDrawTop, ui::KBD_MODE_KANA);
                    }
                    else
                    {
                        query = util::getString(getTxt("搜索读音"), false);
                        ok = true;
                    }
                    if (!ok) return;

                    s_curReadingMatches = nlpp::Dict::filterReadings(0x10, heroineMask, query);

                    if (s_curReadingMatches.empty())
                    {
                        ui::showMessage(getTxt("找不到匹配的读音！"));
                        return;
                    }

                    s_subAction = ACTION_READING_PICKER;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    int curSel = 0;
                    for (size_t i = 0; i < s_curReadingMatches.size(); ++i)
                    {
                        if (s_curReadingMatches[i]->id == sd.stats.readings[4]) curSel = (int)i;
                        std::string itemText = s_curReadingMatches[i]->text;
                        if (!s_curReadingMatches[i]->romaji.empty())
                            itemText += " (" + s_curReadingMatches[i]->romaji + ")";
                        s_editorSubMenu.addOpt(itemText, 300);
                    }
                    s_editorSubMenu.setSelected(curSel);
                }
                else if (sel == 6) { onConfirmReloadSlot(nullptr); }
                else if (sel == 7) { onConfirmSaveSlot(nullptr); }
            }
            else if (s_editorPage == 2)
            {
                if (sel == 0) // 发长
                {
                    s_subAction = ACTION_HAIR_LENGTH;
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(false);
                    s_editorSubMenu.setMaxVisible(10);
                    auto validStages = nlpp::Dict::getValidLengthStages(sd.girlfriend.heroine);
                    for (uint8_t stage : validStages)
                    {
                        s_editorSubMenu.addOpt(nlpp::Dict::getHairLengthName(sd.girlfriend.heroine, stage), 300);
                    }
                }
                else if (sel == 1) // 发型
                {
                    s_subAction = ACTION_HAIR_STYLE;
                    s_curStyleOptions = nlpp::getStyleOptions(sd.girlfriend.heroine, sd.girlfriend.hairLength);
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    int curSel = 0;
                    for (size_t i = 0; i < s_curStyleOptions.size(); ++i)
                    {
                        s_editorSubMenu.addOpt(s_curStyleOptions[i].displayName, 300);
                        if (s_curStyleOptions[i].styleId == sd.girlfriend.hairStyleId) curSel = i;
                    }
                    s_editorSubMenu.setSelected(curSel);
                }
                else if (sel == 2) // 发色
                {
                    s_subAction = ACTION_HAIR_COLOR;
                    s_curColorOptions = nlpp::getColorOptions(sd.girlfriend.heroine);
                    s_editorSubMenuOpen = true;
                    s_editorSubMenu.reset();
                    s_editorSubMenu.setPaging(true);
                    s_editorSubMenu.setMaxVisible(10);
                    for (const auto &c : s_curColorOptions)
                    {
                        s_editorSubMenu.addOpt(c.displayName, 300);
                    }
                    s_editorSubMenu.setSelected(std::max(0, (int)sd.girlfriend.hairColorId - 1));
                }
                else if (sel == 3) // 重置为初始发型
                {
                    ui::confirm(getTxt("确定要将女友的发型和发色重置为默认值吗？"), onConfirmResetHair_t, nullptr, nullptr);
                }
                else if (sel == 4) { onConfirmReloadSlot(nullptr); }
                else if (sel == 5) { onConfirmSaveSlot(nullptr); }
            }
            else if (s_editorPage == 4) // 其他
            {
                if (sel == 0)
                {
                    int v = util::getInt(getTxt("输入总天数 (1-9999)"), sd.timeline.timelineDays, 9999);
                    if (v >= 0)
                    {
                        sd.timeline.timelineDays = v;
                        refreshEditorPageMenu();
                    }
                }
                else if (sel == 1) { int v = util::getInt(getTxt("输入上线天数 (1-9999)"), sd.timeline.datingDays, 9999); if (v >= 0) { sd.timeline.datingDays = v; refreshEditorPageMenu(); } }
                else if (sel == 2) { int v = util::getInt(getTxt("输入运动属性值 (0-255)"), sd.stats.fitness, 255); if (v >= 0) { sd.stats.fitness = v; refreshEditorPageMenu(); } }
                else if (sel == 3) { int v = util::getInt(getTxt("输入知识属性值 (0-255)"), sd.stats.knowledge, 255); if (v >= 0) { sd.stats.knowledge = v; refreshEditorPageMenu(); } }
                else if (sel == 4) { int v = util::getInt(getTxt("输入感性属性值 (0-255)"), sd.stats.sensibility, 255); if (v >= 0) { sd.stats.sensibility = v; refreshEditorPageMenu(); } }
                else if (sel == 5) { int v = util::getInt(getTxt("输入魅力属性值 (0-255)"), sd.stats.charm, 255); if (v >= 0) { sd.stats.charm = v; refreshEditorPageMenu(); } }
                else if (sel == 6) { int v = util::getInt(getTxt("输入金钱 (0-999999)"), sd.stats.rich, 999999); if (v >= 0) { sd.stats.rich = v; refreshEditorPageMenu(); } }
                else if (sel == 7) { int v = util::getInt(getTxt("输入TODO点数 (0-9999)"), sd.stats.todo, 9999); if (v >= 0) { sd.stats.todo = v; refreshEditorPageMenu(); } }
                else if (sel == 8)
                {
                    if (sd.isFriendMode())
                    {
                        ui::showMessage(getTxt("当前槽位处于朋友模式，无法修改称号！"));
                    }
                    else
                    {
                        s_subAction = ACTION_COUPLE_TITLE;
                        s_editorSubMenuOpen = true;
                        s_editorSubMenu.reset();
                        s_editorSubMenu.setPaging(true);
                        s_editorSubMenu.setMaxVisible(10);
                        for (int r = 1; r <= 23; ++r)
                        {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "Lv.%02d %s (%u pt)", r, nlpp::Dict::getCoupleTitleName(r).c_str(), (unsigned int)nlpp::Dict::getCoupleRankExp(r));
                            s_editorSubMenu.addOpt(buf, 300);
                        }
                        int curRank = nlpp::Dict::getCoupleRank(sd.girlfriend.coupleExp);
                        s_editorSubMenu.setSelected(curRank - 1);
                    }
                }
                else if (sel == 9)
                {
                    nlpp::g_saveMgr.removeCameraFilter(s_editorSlot);
                    refreshEditorPageMenu();
                    ui::showMessage(getTxt("已成功移除相机滤镜！"));
                }
                else if (sel == 10) { onConfirmReloadSlot(nullptr); }
                else if (sel == 11) { onConfirmSaveSlot(nullptr); }
            }
            return;
        }

        s_editorMenu.update();
    }

    void editorDrawTop()
    {
        static const char *pageTitles[] = { "玩家信息", "玩家读音", "女友发型", "自定义发色", "其他" };
        ui::drawUIBar(TITLE_TEXT + " - " + getTxt(pageTitles[s_editorPage]), ui::SCREEN_TOP, true);

        const auto &sd = nlpp::g_saveMgr.getSlotData(s_editorSlot);

        if (s_editorPage == 0)
        {
            std::string bdayStr = formatBirthday(sd.profile.birthMonth, sd.profile.birthDay);
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "%s: %s %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s",
                     getTxt("姓名"), sd.profile.lastName.c_str(), sd.profile.firstName.c_str(),
                     getTxt("通讯昵称(HN)"), sd.profile.hn.c_str(),
                     getTxt("生日"), bdayStr.c_str(),
                     getTxt("血型"), nlpp::Dict::getBloodTypeName(sd.profile.blood),
                     getTxt("出身地"), nlpp::Dict::getPrefectureName(sd.profile.prefecture));
            gfx::drawText(buf, 20, 36, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
        }
        else if (s_editorPage == 1)
        {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "%s: %d %s\n"
                     "%s: %s\n"
                     "%s: %s\n"
                     "%s: %s\n"
                     "%s: %s\n"
                     "%s: %s",
                     getTxt("当前使用"), (int)sd.stats.currentSelector, nlpp::Dict::getReadingSlotDisplay(sd.stats.currentSelector < 5 ? sd.stats.currentSelector : 0, sd.stats.readings[sd.stats.currentSelector < 5 ? sd.stats.currentSelector : 0]).c_str(),
                     getTxt("0 姓氏读音"), nlpp::Dict::getReadingSlotDisplay(0, sd.stats.readings[0]).c_str(),
                     getTxt("1 名字读音"), nlpp::Dict::getReadingSlotDisplay(1, sd.stats.readings[1]).c_str(),
                     getTxt("2 后缀读音"), nlpp::Dict::getReadingSlotDisplay(2, sd.stats.readings[2]).c_str(),
                     getTxt("3 预设读音"), nlpp::Dict::getReadingSlotDisplay(3, sd.stats.readings[3]).c_str(),
                     getTxt("4 特殊读音"), nlpp::Dict::getReadingSlotDisplay(4, sd.stats.readings[4]).c_str());
            gfx::drawText(buf, 20, 36, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
        }
        else if (s_editorPage == 2)
        {
            std::string curStyleName = nlpp::getCleanStyleName(sd.girlfriend.heroine, sd.girlfriend.hairLength, sd.girlfriend.hairStyleId);
            std::string curColorName = nlpp::Dict::isCustomHairColor(sd.girlfriend.heroine, sd.girlfriend.hairR, sd.girlfriend.hairG, sd.girlfriend.hairB, sd.girlfriend.hairColorActive)
                                       ? getTxt("自定义发色")
                                       : nlpp::getCleanColorName(sd.girlfriend.heroine, sd.girlfriend.hairColorId);

            if (s_editorSubMenuOpen && s_subAction == ACTION_HAIR_STYLE)
            {
                int sel = s_editorSubMenu.getSelected();
                if (sel >= 0 && sel < (int)s_curStyleOptions.size())
                    curStyleName = s_curStyleOptions[sel].displayName;
            }
            else if (s_editorSubMenuOpen && s_subAction == ACTION_HAIR_COLOR)
            {
                int sel = s_editorSubMenu.getSelected();
                if (sel >= 0 && sel < (int)s_curColorOptions.size())
                    curColorName = s_curColorOptions[sel].displayName;
            }

            // Line 1: Hair Length (top)
            char lenBuf[128];
            snprintf(lenBuf, sizeof(lenBuf), "%s: %s", getTxt("发长"), nlpp::Dict::getHairLengthName(sd.girlfriend.heroine, sd.girlfriend.hairLength).c_str());
            gfx::drawText(lenBuf, 36, 20, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);

            // Line 2: Hairstyle label above left portrait, Hair color label above right portrait
            char styleBuf[128], colorBuf[128];
            snprintf(styleBuf, sizeof(styleBuf), "%s: %s", getTxt("发型"), curStyleName.c_str());
            snprintf(colorBuf, sizeof(colorBuf), "%s: %s", getTxt("发色"), curColorName.c_str());
            gfx::drawText(styleBuf, 36, 36, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            gfx::drawText(colorBuf, 218, 36, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);

            // Draw both portraits 1:1 (146 x 170)
            nlpp::portraitDrawStyle(36, 54);
            nlpp::portraitDrawColor(218, 54);
        }
        else if (s_editorPage == 3)
        {
            bool isLight = (bool)cfg::config["lightback"];
            u32 swatchBorder = isLight ? C2D_Color32(185, 185, 185, 255) : C2D_Color32(80, 80, 80, 255);

            C2D_DrawRectSolid(19, 35, GFX_DEPTH_DEFAULT, 222, 48, swatchBorder);
            C2D_DrawRectSolid(20, 36, GFX_DEPTH_DEFAULT, 220, 46, C2D_Color32(s_customR, s_customG, s_customB, 255));

            char buf[128];
            snprintf(buf, sizeof(buf), "RGB: (%d, %d, %d)", (int)s_customR, (int)s_customG, (int)s_customB);
            gfx::drawText(buf, 20, 96, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);

            snprintf(buf, sizeof(buf), "HEX: #%02X%02X%02X", s_customR, s_customG, s_customB);
            gfx::drawText(buf, 20, 124, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);

            uint8_t matchedId = nlpp::Dict::matchClosestColorId(s_customR, s_customG, s_customB);
            std::string matchedColorName = nlpp::getCleanColorName(sd.girlfriend.heroine, matchedId);
            snprintf(buf, sizeof(buf), "%s: %s", getTxt("匹配发色"), matchedColorName.c_str());
            gfx::drawText(buf, 20, 152, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);

            std::string hlDesc = nlpp::Dict::getColorHighlightDesc(matchedId);
            snprintf(buf, sizeof(buf), "%s: %s", getTxt("匹配高光"), hlDesc.c_str());
            gfx::drawText(buf, 20, 180, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
        }
        else if (s_editorPage == 4)
        {
            char buf[512];
            bool hasFilter = (sd.stats.cameraFilter != -1) && ((sd.stats.equipMask & 0x04) != 0);
            std::string fStr = hasFilter ? getTxt("使用中") : getTxt("无滤镜");

            std::string titleStr;
            if (sd.isFriendMode())
            {
                titleStr = getTxt("无");
            }
            else
            {
                int rank = nlpp::Dict::getCoupleRank(sd.girlfriend.coupleExp);
                char tBuf[128];
                snprintf(tBuf, sizeof(tBuf), "Lv.%d %s (%u pt)", rank, nlpp::Dict::getCoupleTitleName(rank).c_str(), (unsigned int)sd.girlfriend.coupleExp);
                titleStr = tBuf;
            }

            snprintf(buf, sizeof(buf),
                     "%s: %d %s\n"
                     "%s: %d %s\n"
                     "%s: %d\n"
                     "%s: %d\n"
                     "%s: %d\n"
                     "%s: %d\n"
                     "%s: %d G\n"
                     "%s: %d %s\n"
                     "%s: %s\n"
                     "%s: %s",
                     getTxt("总天数"), (int)sd.timeline.timelineDays, getTxt("天"),
                     getTxt("上线天数"), (int)sd.timeline.datingDays, getTxt("天"),
                     getTxt("运动属性值"), (int)sd.stats.fitness,
                     getTxt("知识属性值"), (int)sd.stats.knowledge,
                     getTxt("感性属性值"), (int)sd.stats.sensibility,
                     getTxt("魅力属性值"), (int)sd.stats.charm,
                     getTxt("金钱"), (int)sd.stats.rich,
                     getTxt("TODO"), (int)sd.stats.todo, getTxt("点"),
                     getTxt("称号"), titleStr.c_str(),
                     getTxt("滤镜"), fStr.c_str());
            gfx::drawText(buf, 20, 24, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);
        }
    }

    void editorDrawBot()
    {
        if (s_editorSubMenuOpen)
        {
            s_editorSubMenu.draw(10, 16, gfx::txtCont, 300, true);
            ui::drawUIBar(getTxt("\ue000 确定  \ue001 取消"), ui::SCREEN_BOT, true);
            return;
        }

        if (s_editorPage == 3)
        {
            editorDrawCustomColorBot();
            ui::drawUIBar(getTxt("\ue000 修改/执行  \ue001 返回  \ue01A\ue077\ue019 切换页面"), ui::SCREEN_BOT, true);
            return;
        }

        s_editorMenu.draw(10, 16, gfx::txtCont, 300, true);
        ui::drawUIBar(getTxt("\ue000 修改/执行  \ue001 返回  \ue01A\ue077\ue019 切换页面"), ui::SCREEN_BOT, true);
    }
}
