#include "nlpp_ui.h"
#include "nlpp_save.h"
#include "nlpp_dict.h"
#include "gfx.h"
#include "ui.h"
#include "util.h"
#include "cfg.h"
#include "fs.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace ui
{
    // =========================================================================
    // Core Constants & Types
    // =========================================================================

    enum AppMode
    {
        MODE_MAIN_MENU = 0, // 存档选择 / 备份管理 / 设置
        MODE_SAVE_EDIT = 1  // 存档编辑模式
    };

    static AppMode s_appMode = MODE_MAIN_MENU;

    // Mode 1: 3 Views
    static const int NUM_MAIN_VIEWS = 3;
    static int s_mainView = 0; // 0: 存档选择, 1: 备份管理, 2: 设置
    static bool s_slotSelectOpen = false; // When true on View 0, bottom screen shows slots

    // Mode 2: 7 Edit Pages
    static const int NUM_EDIT_PAGES = 7;
    static int s_editPage = 0; // 0: 玩家信息, 1: 发型, 2: 呼称, 3: 彼氏力, 4: 女友, 5: 日程, 6: 收集

    static const char *s_editPageTitles[NUM_EDIT_PAGES] = {
        "玩家信息", "发型形象", "呼称称呼", "彼氏能力", "恋人关系", "日程生活", "全量收集"
    };

    // Focus & Input
    static int s_selectedItem = 0;
    static bool s_cpadNeutral = true;

    // Blue Bounding Box Color (Dodger / 3DS System Blue)
    static const u32 COLOR_FOCUS_BLUE = C2D_Color32(0, 140, 255, 255);

    // Toast message for operations
    static std::string s_toastMessage = "";
    static int s_toastTimer = 0;

    // Secondary Confirmation Dialog state (strictly TYSS style)
    struct ConfirmState
    {
        bool active = false;
        std::string title;
        std::string message;
        std::function<void()> onConfirm;
        std::function<void()> onCancel;
        int selectedBtn = 0; // 0: 是, 1: 否
    };
    static ConfirmState s_confirm;

    // Alert Dialog state (strictly TYSS style)
    struct AlertState
    {
        bool active = false;
        std::string title;
        std::string message;
    };
    static AlertState s_alert;

    // Value Picker Dialog state
    struct PickerItem
    {
        int id;
        std::string label;
        std::string detail;
        u32 colorRgb = 0;
        bool hasColor = false;
    };

    struct PickerState
    {
        bool active = false;
        std::string title;
        std::vector<PickerItem> items;
        int selected = 0;
        int scroll = 0;
        std::function<void(const PickerItem &item)> onSelect;
    };
    static PickerState s_picker;

    // =========================================================================
    // Dialog Triggers
    // =========================================================================

    static void requestConfirm(const std::string &title, const std::string &message,
                               std::function<void()> onConfirm, std::function<void()> onCancel = nullptr)
    {
        s_confirm.active = true;
        s_confirm.title = title;
        s_confirm.message = message;
        s_confirm.onConfirm = onConfirm;
        s_confirm.onCancel = onCancel;
        s_confirm.selectedBtn = 0; // Default focus to "是"
    }

    static void showAlert(const std::string &title, const std::string &message)
    {
        s_alert.active = true;
        s_alert.title = title;
        s_alert.message = message;
    }

    static void openPicker(const std::string &title, const std::vector<PickerItem> &items, int initialId,
                           std::function<void(const PickerItem &item)> onSelect)
    {
        s_picker.active = true;
        s_picker.title = title;
        s_picker.items = items;
        s_picker.selected = 0;
        s_picker.scroll = 0;
        s_picker.onSelect = onSelect;

        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].id == initialId)
            {
                s_picker.selected = (int)i;
                s_picker.scroll = std::max(0, (int)i - 3);
                break;
            }
        }
    }

    static std::string inputStringSwkbd(const std::string &hint, const std::string &initial, int maxLen = 12)
    {
        SwkbdState state;
        char input[128] = {0};
        swkbdInit(&state, SWKBD_TYPE_NORMAL, 2, maxLen);
        swkbdSetHintText(&state, hint.c_str());
        swkbdSetInitialText(&state, initial.c_str());
        swkbdSetFeatures(&state, SWKBD_PREDICTIVE_INPUT);
        swkbdInputText(&state, input, sizeof(input));
        return std::string(input);
    }

    // =========================================================================
    // Drawing Helpers (Strictly reusing TYSS Theme Palette & Bounding Box)
    // =========================================================================

    static void drawCard(float x, float y, float w, float h, u32 bg = gfx::rectLt, u32 border = gfx::divClr)
    {
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, h, bg);
        // 1px border
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, 1, border);
        C2D_DrawRectSolid(x, y + h - 1, GFX_DEPTH_DEFAULT, w, 1, border);
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, 1, h, border);
        C2D_DrawRectSolid(x + w - 1, y, GFX_DEPTH_DEFAULT, 1, h, border);
    }

    static void drawButton(float x, float y, float w, float h, const std::string &text, bool focused, const std::string &badge = "")
    {
        u32 bgClr = focused ? gfx::btnSel : gfx::btnClr;
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, h, bgClr);

        // 1px border
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, 1, gfx::divClr);
        C2D_DrawRectSolid(x, y + h - 1, GFX_DEPTH_DEFAULT, w, 1, gfx::divClr);
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, 1, h, gfx::divClr);
        C2D_DrawRectSolid(x + w - 1, y, GFX_DEPTH_DEFAULT, 1, h, gfx::divClr);

        if (focused)
        {
            // Focused button with vivid blue bounding box
            gfx::drawBoundingBox(x, y, w, h, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);
        }

        // Text rendering
        float textY = y + (h - 14.0f) / 2.0f;
        gfx::drawText(text, x + 8, textY, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        if (!badge.empty())
        {
            size_t bw = gfx::getTextWidth(badge);
            float badgeX = x + w - bw - 10;
            gfx::drawText(badge, badgeX, textY, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        }
    }

    static void drawProgressBar(float x, float y, float w, float h, float ratio, u32 fillClr = gfx::btnSel)
    {
        ratio = std::max(0.0f, std::min(1.0f, ratio));
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, h, gfx::rectSh);
        if (ratio > 0.0f)
        {
            C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w * ratio, h, fillClr);
        }
        // Border
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, 1, gfx::divClr);
        C2D_DrawRectSolid(x, y + h - 1, GFX_DEPTH_DEFAULT, w, 1, gfx::divClr);
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, 1, h, gfx::divClr);
        C2D_DrawRectSolid(x + w - 1, y, GFX_DEPTH_DEFAULT, 1, h, gfx::divClr);
    }

    // =========================================================================
    // Lifecycle & Auto-Load on Boot
    // =========================================================================

    void nlppInit()
    {
        s_appMode = MODE_MAIN_MENU;
        s_mainView = 0;
        s_slotSelectOpen = false;
        s_editPage = 0;
        s_selectedItem = 0;
        s_cpadNeutral = true;
        s_confirm.active = false;
        s_alert.active = false;
        s_picker.active = false;
        s_toastMessage = "";
        s_toastTimer = 0;

        // Auto-load on boot logic
        if (cfg::config["autoload"] == 1)
        {
            nlpp::g_saveMgr.detectSources();
            nlpp::SaveSource defaultSrc = (cfg::config["default_type"] == 1) ? nlpp::SOURCE_GAMECARD : nlpp::SOURCE_SD;

            bool srcAvailable = (defaultSrc == nlpp::SOURCE_GAMECARD) ? nlpp::g_saveMgr.hasGameCard() : nlpp::g_saveMgr.hasSD();

            if (!srcAvailable)
            {
                // Fall back to Save Type selection
                s_appMode = MODE_MAIN_MENU;
                s_mainView = 0;
                s_slotSelectOpen = false;
                showAlert("启动加载提示", "默认存档类型不可用，已自动回退到存档选择界面。");
            }
            else
            {
                nlpp::g_saveMgr.switchSource(defaultSrc);
                int defaultSlot = cfg::config["default_slot"];
                if (defaultSlot < 0 || defaultSlot >= 3) defaultSlot = 0;

                const auto &slotData = nlpp::g_saveMgr.getSlotData(defaultSlot);
                if (slotData.empty)
                {
                    // Fall back to Slot selection
                    s_appMode = MODE_MAIN_MENU;
                    s_mainView = 0;
                    s_slotSelectOpen = true;
                    s_selectedItem = defaultSlot;
                    showAlert("启动加载提示", "默认存档槽位为空存档，已自动回退到槽位选择界面。");
                }
                else
                {
                    // Direct entry into Save Edit Mode!
                    nlpp::g_saveMgr.selectSlot(defaultSlot);
                    s_appMode = MODE_SAVE_EDIT;
                    s_editPage = 0;
                    s_selectedItem = 0;
                }
            }
        }
    }

    void nlppExit()
    {
        s_confirm.active = false;
        s_alert.active = false;
        s_picker.active = false;
    }

    void nlppSaveAndCommit()
    {
        std::string statusMsg;
        bool ok = nlpp::g_saveMgr.commitChanges(&statusMsg);
        if (ok)
        {
            s_toastMessage = "提交保存成功! 已写入介质并在 SD 备份";
        }
        else
        {
            s_toastMessage = "保存失败: " + statusMsg;
        }
        s_toastTimer = 220;
    }

    // =========================================================================
    // Mode 1: Top Screen Drawing (3 Views)
    // =========================================================================

    static void drawMode1Top_SaveSelect()
    {
        ui::drawUIBar(TITLE_TEXT + "－存档 / 设置", ui::SCREEN_TOP, true);

        // Display two large buttons on top screen
        float btnW = 320.0f;
        float btnH = 64.0f;
        float btnX = 40.0f;

        // Card 1: 数字版 (SD)
        float y1 = 44.0f;
        bool f1 = (s_selectedItem == 0 && !s_slotSelectOpen);
        u32 bg1 = f1 ? gfx::btnSel : gfx::btnClr;
        C2D_DrawRectSolid(btnX, y1, GFX_DEPTH_DEFAULT, btnW, btnH, bg1);
        C2D_DrawRectSolid(btnX, y1, GFX_DEPTH_DEFAULT, btnW, 1, gfx::divClr);
        C2D_DrawRectSolid(btnX, y1 + btnH - 1, GFX_DEPTH_DEFAULT, btnW, 1, gfx::divClr);
        C2D_DrawRectSolid(btnX, y1, GFX_DEPTH_DEFAULT, 1, btnH, gfx::divClr);
        C2D_DrawRectSolid(btnX + btnW - 1, y1, GFX_DEPTH_DEFAULT, 1, btnH, gfx::divClr);

        if (f1) gfx::drawBoundingBox(btnX, y1, btnW, btnH, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);

        gfx::drawText("【数字版 (SD Card)】", btnX + 16, y1 + 12, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);
        bool hasSD = nlpp::g_saveMgr.hasSD();
        const char *stSD = hasSD ? "[已检测到存档]" : "[未检测到存档]";
        gfx::drawText("Title ID: 00040000000F4E00  " + std::string(stSD), btnX + 16, y1 + 36, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);

        // Card 2: 实体版 (GameCard)
        float y2 = 132.0f;
        bool f2 = (s_selectedItem == 1 && !s_slotSelectOpen);
        u32 bg2 = f2 ? gfx::btnSel : gfx::btnClr;
        C2D_DrawRectSolid(btnX, y2, GFX_DEPTH_DEFAULT, btnW, btnH, bg2);
        C2D_DrawRectSolid(btnX, y2, GFX_DEPTH_DEFAULT, btnW, 1, gfx::divClr);
        C2D_DrawRectSolid(btnX, y2 + btnH - 1, GFX_DEPTH_DEFAULT, btnW, 1, gfx::divClr);
        C2D_DrawRectSolid(btnX, y2, GFX_DEPTH_DEFAULT, 1, btnH, gfx::divClr);
        C2D_DrawRectSolid(btnX + btnW - 1, y2, GFX_DEPTH_DEFAULT, 1, btnH, gfx::divClr);

        if (f2) gfx::drawBoundingBox(btnX, y2, btnW, btnH, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);

        gfx::drawText("【实体版 (GameCard)】", btnX + 16, y2 + 12, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);
        bool hasCart = nlpp::g_saveMgr.hasGameCard();
        const char *stCart = hasCart ? "[已检测到卡带]" : "[未插入卡带]";
        gfx::drawText("卡带代号: CTR-P-AKNJ  " + std::string(stCart), btnX + 16, y2 + 36, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
    }

    static void drawMode1Top_Backup()
    {
        ui::drawUIBar(TITLE_TEXT + "－备份管理", ui::SCREEN_TOP, true);

        auto bList = nlpp::g_saveMgr.listBackups();
        drawCard(20, 24, 360, 204);

        if (bList.empty())
        {
            gfx::drawText("暂无任何历史备份文件", 130, 110, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);
            return;
        }

        int visibleCount = 7;
        float itemY = 28.0f;
        float itemH = 26.0f;

        int scroll = std::max(0, s_selectedItem - 3);

        for (int i = 0; i < visibleCount; ++i)
        {
            int idx = scroll + i;
            if (idx >= (int)bList.size()) break;

            float iy = itemY + i * itemH;
            bool sel = (idx == s_selectedItem);

            C2D_DrawRectSolid(24, iy, GFX_DEPTH_DEFAULT, 352, itemH - 2, sel ? gfx::btnSel : gfx::btnClr);

            if (sel)
            {
                gfx::drawBoundingBox(24, iy, 352, itemH - 2, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);
            }

            gfx::drawText(bList[idx].dirName, 32, iy + 4, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
            gfx::drawText("[镜像备份]", 290, iy + 4, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
        }
    }

    static void drawMode1Top_Settings()
    {
        ui::drawUIBar(TITLE_TEXT + "－设置", ui::SCREEN_TOP, true);

        drawCard(20, 24, 360, 204);

        static const char *settingLabels[] = {
            "自动备份存档",
            "导出到 ZIP",
            "ZIP 压缩等级",
            "界面主题色",
            "界面语言",
            "启动时加载存档",
            "默认存档类型",
            "默认存档槽位"
        };

        float startY = 28.0f;
        float itemH = 23.0f;

        for (int i = 0; i < 8; ++i)
        {
            float iy = startY + i * itemH;
            bool sel = (i == s_selectedItem);

            C2D_DrawRectSolid(24, iy, GFX_DEPTH_DEFAULT, 352, itemH - 2, sel ? gfx::btnSel : gfx::btnClr);

            if (sel)
            {
                gfx::drawBoundingBox(24, iy, 352, itemH - 2, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);
            }

            gfx::drawText(settingLabels[i], 32, iy + 3, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

            // Setting value
            std::string valStr;
            switch (i)
            {
                case 0: valStr = cfg::config["autobackup"] ? "开" : "关"; break;
                case 1: valStr = cfg::config["zip"] ? "开" : "关"; break;
                case 2:
                {
                    int lvl = cfg::config["deflateLevel"];
                    if (lvl == 1) valStr = "1 (最快速度)";
                    else if (lvl == 6) valStr = "6 (标准压缩)";
                    else if (lvl == 9) valStr = "9 (极限压缩)";
                    else valStr = std::to_string(lvl);
                    break;
                }
                case 3: valStr = cfg::config["lightback"] ? "浅色系" : "深色系"; break;
                case 4:
                {
                    int l = cfg::config["uilang"];
                    valStr = (l == 1) ? "English" : ((l == 2) ? "繁體中文" : "简体中文");
                    break;
                }
                case 5: valStr = cfg::config["autoload"] ? "开" : "关"; break;
                case 6: valStr = (cfg::config["default_type"] == 1) ? "实体版" : "数字版"; break;
                case 7: valStr = "槽位 " + std::to_string(cfg::config["default_slot"] + 1); break;
            }

            size_t tw = gfx::getTextWidth(valStr);
            gfx::drawText(valStr, 360 - tw, iy + 3, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        }
    }

    // =========================================================================
    // Mode 2: Top Screen Drawing (7 Edit Pages)
    // =========================================================================

    static void drawMode2Top_Profile(const nlpp::SlotData &slotData)
    {
        char buf[128];

        // Left Card: 玩家档案
        drawCard(8, 22, 190, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【玩家基础档案】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "玩家: %s %s", slotData.profile.lastName.c_str(), slotData.profile.firstName.c_str());
        gfx::drawText(buf, 14, 52, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "通信昵称(HN): %s", slotData.profile.hn.c_str());
        gfx::drawText(buf, 14, 76, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "生日: %d月%d日", slotData.profile.birthMonth, slotData.profile.birthDay);
        gfx::drawText(buf, 14, 100, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "血型: %s", nlpp::Dict::getBloodTypeName(slotData.profile.blood));
        gfx::drawText(buf, 14, 124, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "出身: %s", nlpp::Dict::getPrefectureName(slotData.profile.prefecture));
        gfx::drawText(buf, 14, 148, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        uint32_t sel = slotData.stats.currentSelector;
        uint32_t rId = slotData.stats.readings[sel < 5 ? sel : 0];
        std::string rText = nlpp::Dict::getReadingText(rId);
        snprintf(buf, sizeof(buf), "当前呼称: 「%s」", rText.c_str());
        gfx::drawText(buf, 14, 176, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        // Right Card: 槽位与恋爱状态
        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【当前槽位与恋爱状态】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        const char *srcStr = (nlpp::g_saveMgr.getSource() == nlpp::SOURCE_GAMECARD) ? "实体卡带版" : "数字下载版";
        snprintf(buf, sizeof(buf), "介质: %s", srcStr);
        gfx::drawText(buf, 208, 52, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "操作槽位: 槽位 %d", nlpp::g_saveMgr.getCurrentSlot() + 1);
        gfx::drawText(buf, 208, 76, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        const char *hName = "未结识恋人";
        switch (slotData.girlfriend.heroine)
        {
            case nlpp::HEROINE_MANAKA: hName = "♥ 高嶺 愛花 (Manaka)"; break;
            case nlpp::HEROINE_RINKO:  hName = "♥ 小早川 凛子 (Rinko)"; break;
            case nlpp::HEROINE_NENE:   hName = "♥ 姉崎 寧々 (Nene)"; break;
            default: break;
        }
        gfx::drawText(hName, 208, 100, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "实机交往: %u 天", (unsigned)slotData.timeline.datingDays);
        gfx::drawText(buf, 208, 126, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "总历天数: %u 日", (unsigned)slotData.timeline.timelineDays);
        gfx::drawText(buf, 208, 150, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        const auto &ctitle = nlpp::Dict::getCoupleTitleByExp(slotData.girlfriend.coupleExp);
        snprintf(buf, sizeof(buf), "称号: Lv.%d %s", ctitle.rank, ctitle.name_jp);
        gfx::drawText(buf, 208, 176, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
    }

    static void drawMode2Top_Hairstyle(const nlpp::SlotData &slotData)
    {
        char buf[128];

        // Left Card: 发型发色参数
        drawCard(8, 22, 190, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【发型发色参数】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        const char *stName = nlpp::Dict::getStyleName(slotData.girlfriend.hairStyleId);
        snprintf(buf, sizeof(buf), "款式: %s", stName);
        gfx::drawText(buf, 14, 52, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);
        snprintf(buf, sizeof(buf), "款式 ID: %d", slotData.girlfriend.hairStyleId);
        gfx::drawText(buf, 14, 72, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);

        const char *lenName = nlpp::Dict::getHairLengthName(slotData.girlfriend.heroine, slotData.girlfriend.hairLength);
        snprintf(buf, sizeof(buf), "发长: %s", lenName);
        gfx::drawText(buf, 14, 94, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        const auto &cdef = nlpp::Dict::getHairColor(slotData.girlfriend.hairColorId);
        snprintf(buf, sizeof(buf), "发色: %s - %s", cdef.category, cdef.tone);
        gfx::drawText(buf, 14, 118, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "RGB: (%d, %d, %d)", cdef.r, cdef.g, cdef.b);
        gfx::drawText(buf, 14, 140, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

        // Hair Color swatch rectangle
        gfx::drawText("实机发色预览:", 14, 164, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        u32 swatchClr = C2D_Color32(cdef.r, cdef.g, cdef.b, 255);
        C2D_DrawRectSolid(14, 184, GFX_DEPTH_DEFAULT, 176, 22, swatchClr);
        drawCard(14, 184, 176, 22, swatchClr, gfx::divClr);

        // Right Card: 攻略书发型图鉴预览位
        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【攻略本原画图鉴相框】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        // Placeholder frame for official guidebook illustrations
        drawCard(222, 54, 150, 110, gfx::rectSh, gfx::divClr);
        snprintf(buf, sizeof(buf), "图鉴编号: #%02d", slotData.girlfriend.hairStyleId);
        gfx::drawText(buf, 245, 66, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

        gfx::drawText(stName, 240, 92, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        gfx::drawText("【攻略本插画预览区】", 232, 122, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
        gfx::drawText("后续直接载入高清插图", 230, 140, GFX_DEPTH_DEFAULT, 0.34f, gfx::txtCont);

        gfx::drawText("★ 可在下屏按键修改发型", 214, 184, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
    }

    static void drawMode2Top_Readings(const nlpp::SlotData &slotData)
    {
        char buf[128];

        drawCard(8, 22, 384, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 384, 22, gfx::rectSh);
        gfx::drawText("【呼称称呼与三位一体读音】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        static const char *slotLabels[] = {
            "槽0: 姓氏读音 (LastName)",
            "槽1: 名字读音 (FirstName)",
            "槽2: 后缀组合 (Suffix)",
            "槽3: 预设称呼 (Preset)",
            "槽4: 专属爱称 (Custom)"
        };

        uint32_t sel = slotData.stats.currentSelector;
        snprintf(buf, sizeof(buf), "当前生效呼称槽: [%s]", slotLabels[sel < 5 ? sel : 0]);
        gfx::drawText(buf, 18, 54, GFX_DEPTH_DEFAULT, 0.46f, gfx::txtCont);

        for (int r = 0; r < 5; ++r)
        {
            uint32_t rid = slotData.stats.readings[r];
            std::string rtxt = nlpp::Dict::getReadingText(rid);
            snprintf(buf, sizeof(buf), "%s: 「%s」 (ID:%u)", slotLabels[r], rtxt.c_str(), (unsigned)rid);
            gfx::drawText(buf, 18, 82 + r * 22, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        }

        gfx::drawText("★ 称呼修改自动执行三位一体同步 (主档 savedata9 / 待机 savedata18 / 日志 savedata21)", 18, 196, GFX_DEPTH_DEFAULT, 0.36f, gfx::txtCont);
    }

    static void drawMode2Top_Stats(const nlpp::SlotData &slotData)
    {
        char buf[128];

        // Left Card: 彼氏力四维
        drawCard(8, 22, 190, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【彼氏力四维属性】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "运动能力: %3d / 255", slotData.stats.fitness);
        gfx::drawText(buf, 14, 50, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 68, 176, 8, slotData.stats.fitness / 255.0f);

        snprintf(buf, sizeof(buf), "知识学力: %3d / 255", slotData.stats.knowledge);
        gfx::drawText(buf, 14, 86, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 104, 176, 8, slotData.stats.knowledge / 255.0f);

        snprintf(buf, sizeof(buf), "感性艺术: %3d / 255", slotData.stats.sensibility);
        gfx::drawText(buf, 14, 122, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 140, 176, 8, slotData.stats.sensibility / 255.0f);

        snprintf(buf, sizeof(buf), "魅力交际: %3d / 255", slotData.stats.charm);
        gfx::drawText(buf, 14, 158, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 176, 176, 8, slotData.stats.charm / 255.0f);

        // Right Card: 资产与装备
        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【资产与装备状态】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "所持金钱: %u G", (unsigned)slotData.stats.rich);
        gfx::drawText(buf, 208, 54, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "TODO点数: %u 点", (unsigned)slotData.stats.todo);
        gfx::drawText(buf, 208, 82, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        if (slotData.stats.cameraFilter == -1 || !(slotData.stats.equipMask & 0x04))
            gfx::drawText("相机滤镜: [无 - 纯净取景]", 208, 114, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        else
        {
            snprintf(buf, sizeof(buf), "相机滤镜: [Item %d 生效中]", slotData.stats.cameraFilter);
            gfx::drawText(buf, 208, 114, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        }

        snprintf(buf, sizeof(buf), "最大离线: %u 日", slotData.timeline.maxAbsence);
        gfx::drawText(buf, 208, 146, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        gfx::drawText("（离线天数已防冷战惩罚）", 208, 168, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
    }

    static void drawMode2Top_Girlfriend(const nlpp::SlotData &slotData)
    {
        char buf[128];

        // Left Card: 恋人关系与好感
        drawCard(8, 22, 190, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【恋人羁绊与好感】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        const char *hName = "高嶺 愛花";
        if (slotData.girlfriend.heroine == nlpp::HEROINE_RINKO) hName = "小早川 凛子";
        else if (slotData.girlfriend.heroine == nlpp::HEROINE_NENE) hName = "姉崎 寧々";

        snprintf(buf, sizeof(buf), "女友: %s", hName);
        gfx::drawText(buf, 14, 52, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "好感度: %d / 255", slotData.girlfriend.affection);
        gfx::drawText(buf, 14, 76, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        drawProgressBar(14, 96, 176, 8, slotData.girlfriend.affection / 255.0f);

        const auto &title = nlpp::Dict::getCoupleTitleByExp(slotData.girlfriend.coupleExp);
        snprintf(buf, sizeof(buf), "称号: Lv.%d %s", title.rank, title.name_jp);
        gfx::drawText(buf, 14, 116, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "勋章: %d 枚 🥇", title.medals);
        gfx::drawText(buf, 14, 140, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "情侣EXP: %u", (unsigned)slotData.girlfriend.coupleExp);
        gfx::drawText(buf, 14, 164, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

        // Right Card: 性格坐标
        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【女友性格坐标】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "横轴 X: %d / 1000", slotData.girlfriend.sliderX);
        gfx::drawText(buf, 208, 52, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        gfx::drawText("(文静 <--------> 活泼)", 208, 70, GFX_DEPTH_DEFAULT, 0.36f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "纵轴 Y: %d / 1000", slotData.girlfriend.sliderY);
        gfx::drawText(buf, 208, 94, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        gfx::drawText("(理性 <--------> 感性)", 208, 112, GFX_DEPTH_DEFAULT, 0.36f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "性格形态 Z: %d / 4", slotData.girlfriend.sliderZ);
        gfx::drawText(buf, 208, 136, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        // Miniature 2D coordinate box
        float cx = 250, cy = 162, csize = 46;
        drawCard(cx, cy, csize, csize, gfx::rectSh, gfx::divClr);
        C2D_DrawRectSolid(cx, cy + csize / 2, GFX_DEPTH_DEFAULT, csize, 1, gfx::divClr);
        C2D_DrawRectSolid(cx + csize / 2, cy, GFX_DEPTH_DEFAULT, 1, csize, gfx::divClr);
        float px = cx + (slotData.girlfriend.sliderX / 1000.0f) * csize;
        float py = cy + (1.0f - slotData.girlfriend.sliderY / 1000.0f) * csize;
        C2D_DrawRectSolid(px - 2, py - 2, GFX_DEPTH_DEFAULT, 4, 4, gfx::txtCont);
    }

    static void drawMode2Top_Schedule(const nlpp::SlotData &slotData)
    {
        char buf[128];

        drawCard(8, 22, 384, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 384, 22, gfx::rectSh);
        gfx::drawText("【日程生活与纪念天数】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "实机陪伴交往天数: %u 天", (unsigned)slotData.timeline.datingDays);
        gfx::drawText(buf, 20, 60, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);
        gfx::drawText("（决定百日、一周年、两周年等重要专属纪念对白）", 20, 84, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "日历累计总天数: %u 日", (unsigned)slotData.timeline.timelineDays);
        gfx::drawText(buf, 20, 116, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);
        gfx::drawText("（日历主屏幕显示的累计陪伴时间）", 20, 140, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "最大离线天数: %u 日", slotData.timeline.maxAbsence);
        gfx::drawText(buf, 20, 172, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);
    }

    static void drawMode2Top_Collection()
    {
        drawCard(8, 22, 384, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 384, 22, gfx::rectSh);
        gfx::drawText("【全量收集与画廊点亮】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        gfx::drawText("【513件全套衣柜全量解锁】", 20, 56, GFX_DEPTH_DEFAULT, 0.46f, gfx::txtCont);
        gfx::drawText("精准数据边界保护，严格封锁 513~559 越界溢出区，杜绝坏档", 20, 80, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);

        gfx::drawText("【1,317 项全局主画廊全要素收集点亮】", 20, 110, GFX_DEPTH_DEFAULT, 0.46f, gfx::txtCont);
        gfx::drawText("梦境/特选/回忆/旅行/比赛/名场面全点亮，并批量消除全部 NEW 提示红点", 20, 134, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);

        gfx::drawText("★ 严格完好保留真实旅行录像包 (0x0412~0x3A59)，确保全景回忆流畅播放", 20, 168, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
    }

    void nlppDrawTop()
    {
        int curSlot = nlpp::g_saveMgr.getCurrentSlot();
        const auto &slotData = nlpp::g_saveMgr.getSlotData(curSlot);

        if (s_appMode == MODE_MAIN_MENU)
        {
            switch (s_mainView)
            {
                case 0: drawMode1Top_SaveSelect(); break;
                case 1: drawMode1Top_Backup(); break;
                case 2: drawMode1Top_Settings(); break;
            }
        }
        else // MODE_SAVE_EDIT
        {
            // Header bar strictly TYSS style: TITLE_TEXT + "－" + page name
            std::string editTitle = TITLE_TEXT + "－" + s_editPageTitles[s_editPage];
            ui::drawUIBar(editTitle, ui::SCREEN_TOP, true);

            switch (s_editPage)
            {
                case 0: drawMode2Top_Profile(slotData); break;
                case 1: drawMode2Top_Hairstyle(slotData); break;
                case 2: drawMode2Top_Readings(slotData); break;
                case 3: drawMode2Top_Stats(slotData); break;
                case 4: drawMode2Top_Girlfriend(slotData); break;
                case 5: drawMode2Top_Schedule(slotData); break;
                case 6: drawMode2Top_Collection(); break;
            }
        }
    }

    // =========================================================================
    // Dialog Drawing (Strictly TYSS Style)
    // =========================================================================

    static void drawConfirmDialog()
    {
        // Darken backdrop
        C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.1f, 320, 240, C2D_Color32(0, 0, 0, 160));

        // Centered window matching TYSS confirmDrawFunc: x=24, y=24, w=272, h=200
        float dx = 24, dy = 24, dw = 272, dh = 200;
        C2D_DrawRectSolid(dx, dy, GFX_DEPTH_DEFAULT + 0.15f, dw, dh, gfx::clearClr);

        // 1px border
        C2D_DrawRectSolid(dx, dy, GFX_DEPTH_DEFAULT + 0.15f, dw, 1, gfx::divClr);
        C2D_DrawRectSolid(dx, dy + dh - 1, GFX_DEPTH_DEFAULT + 0.15f, dw, 1, gfx::divClr);
        C2D_DrawRectSolid(dx, dy, GFX_DEPTH_DEFAULT + 0.15f, 1, dh, gfx::divClr);
        C2D_DrawRectSolid(dx + dw - 1, dy, GFX_DEPTH_DEFAULT + 0.15f, 1, dh, gfx::divClr);

        // Message text
        gfx::drawTextWrap(s_confirm.message, 32, 32, GFX_DEPTH_DEFAULT + 0.2f, 0.44f, 256, gfx::txtCont);

        // Buttons: Yes (32, 184, 120, 32) and No (168, 184, 120, 32)
        bool fYes = (s_confirm.selectedBtn == 0);
        bool fNo = (s_confirm.selectedBtn == 1);

        C2D_DrawRectSolid(32, 184, GFX_DEPTH_DEFAULT + 0.2f, 120, 32, fYes ? gfx::btnSel : gfx::btnClr);
        if (fYes) gfx::drawBoundingBox(32, 184, 120, 32, GFX_DEPTH_DEFAULT + 0.22f, COLOR_FOCUS_BLUE);
        gfx::drawText("是 \ue000", 68, 192, GFX_DEPTH_DEFAULT + 0.25f, 0.48f, gfx::txtCont);

        C2D_DrawRectSolid(168, 184, GFX_DEPTH_DEFAULT + 0.2f, 120, 32, fNo ? gfx::btnSel : gfx::btnClr);
        if (fNo) gfx::drawBoundingBox(168, 184, 120, 32, GFX_DEPTH_DEFAULT + 0.22f, COLOR_FOCUS_BLUE);
        gfx::drawText("否 \ue001", 204, 192, GFX_DEPTH_DEFAULT + 0.25f, 0.48f, gfx::txtCont);
    }

    static void drawAlertDialog()
    {
        // Darken backdrop
        C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.1f, 320, 240, C2D_Color32(0, 0, 0, 160));

        // Centered window matching TYSS messageDrawFunc: x=24, y=24, w=272, h=200
        float dx = 24, dy = 24, dw = 272, dh = 200;
        C2D_DrawRectSolid(dx, dy, GFX_DEPTH_DEFAULT + 0.15f, dw, dh, gfx::clearClr);

        // 1px border
        C2D_DrawRectSolid(dx, dy, GFX_DEPTH_DEFAULT + 0.15f, dw, 1, gfx::divClr);
        C2D_DrawRectSolid(dx, dy + dh - 1, GFX_DEPTH_DEFAULT + 0.15f, dw, 1, gfx::divClr);
        C2D_DrawRectSolid(dx, dy, GFX_DEPTH_DEFAULT + 0.15f, 1, dh, gfx::divClr);
        C2D_DrawRectSolid(dx + dw - 1, dy, GFX_DEPTH_DEFAULT + 0.15f, 1, dh, gfx::divClr);

        // Title and message
        gfx::drawText(s_alert.title, 32, 28, GFX_DEPTH_DEFAULT + 0.2f, 0.46f, gfx::txtCont);
        gfx::drawTextWrap(s_alert.message, 32, 54, GFX_DEPTH_DEFAULT + 0.2f, 0.42f, 256, gfx::txtCont);

        // OK button: (96, 184, 128, 32)
        C2D_DrawRectSolid(96, 184, GFX_DEPTH_DEFAULT + 0.2f, 128, 32, gfx::btnSel);
        gfx::drawBoundingBox(96, 184, 128, 32, GFX_DEPTH_DEFAULT + 0.22f, COLOR_FOCUS_BLUE);
        gfx::drawText("确定 \ue000", 132, 192, GFX_DEPTH_DEFAULT + 0.25f, 0.48f, gfx::txtCont);
    }

    static void drawPickerDialog()
    {
        // Darken backdrop
        C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.1f, 320, 240, C2D_Color32(0, 0, 0, 160));

        float px = 20, py = 16, pw = 280, ph = 200;
        drawCard(px, py, pw, ph, gfx::rectLt, gfx::divClr);

        // Header
        C2D_DrawRectSolid(px, py, GFX_DEPTH_DEFAULT + 0.12f, pw, 22, gfx::rectSh);
        gfx::drawText(s_picker.title, px + 10, py + 4, GFX_DEPTH_DEFAULT + 0.15f, 0.42f, gfx::txtCont);

        int visibleCount = 7;
        float itemY = py + 26;
        float itemH = 22.0f;

        for (int i = 0; i < visibleCount; ++i)
        {
            int idx = s_picker.scroll + i;
            if (idx >= (int)s_picker.items.size()) break;

            const auto &item = s_picker.items[idx];
            bool sel = (idx == s_picker.selected);
            float iy = itemY + i * itemH;

            C2D_DrawRectSolid(px + 4, iy, GFX_DEPTH_DEFAULT + 0.14f, pw - 8, itemH - 2, sel ? gfx::btnSel : gfx::btnClr);

            if (sel)
            {
                gfx::drawBoundingBox(px + 4, iy, pw - 8, itemH - 2, GFX_DEPTH_DEFAULT + 0.16f, COLOR_FOCUS_BLUE);
            }

            float textOffset = px + 12;
            if (item.hasColor)
            {
                C2D_DrawRectSolid(textOffset, iy + 3, GFX_DEPTH_DEFAULT + 0.18f, 15, 15, item.colorRgb);
                textOffset += 20;
            }

            gfx::drawText(item.label, textOffset, iy + 4, GFX_DEPTH_DEFAULT + 0.18f, 0.40f, gfx::txtCont);

            if (!item.detail.empty())
            {
                size_t dw = gfx::getTextWidth(item.detail);
                gfx::drawText(item.detail, px + pw - dw - 14, iy + 4, GFX_DEPTH_DEFAULT + 0.18f, 0.38f, gfx::txtCont);
            }
        }
    }

    // =========================================================================
    // Mode 1: Bottom Screen Drawing
    // =========================================================================

    static void drawMode1Bot_SaveSelect()
    {
        if (!s_slotSelectOpen)
        {
            // View 0 (Save type selected): Explain current selection
            drawCard(10, 10, 300, 204);
            C2D_DrawRectSolid(10, 10, GFX_DEPTH_DEFAULT, 300, 24, gfx::rectSh);

            if (s_selectedItem == 0)
            {
                gfx::drawText("【数字版 (SD Card) 存档说明】", 16, 14, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);
                gfx::drawText("存储于 3DS 机身 SD 卡 Nintendo 3DS 文件夹", 16, 44, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
                gfx::drawText("Title ID: 00040000000F4E00", 16, 68, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

                bool hasSD = nlpp::g_saveMgr.hasSD();
                if (hasSD)
                    gfx::drawText("检测状态: 已就绪，按 \ue000 键进入槽位选择", 16, 100, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
                else
                    gfx::drawText("检测状态: 未找到该游戏安装或存档！", 16, 100, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
            }
            else
            {
                gfx::drawText("【实体版 (GameCard) 存档说明】", 16, 14, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);
                gfx::drawText("存储于 3DS 游戏卡带内部 EEPROM/Flash 介质", 16, 44, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
                gfx::drawText("卡带代号: CTR-P-AKNJ (NEWラブプラス+)", 16, 68, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

                bool hasCart = nlpp::g_saveMgr.hasGameCard();
                if (hasCart)
                    gfx::drawText("检测状态: 卡带已就绪，按 \ue000 键进入槽位选择", 16, 100, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
                else
                    gfx::drawText("检测状态: 游戏卡槽未插入对应卡带！", 16, 100, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
            }

            ui::drawUIBar("\ue000 选择  \ue01A\ue077\ue019 切换视图", ui::SCREEN_BOT, true);
        }
        else
        {
            // View 0 Sub-state: Slot selection
            drawCard(8, 6, 304, 212);
            C2D_DrawRectSolid(8, 6, GFX_DEPTH_DEFAULT, 304, 22, gfx::rectSh);
            gfx::drawText("【选择存档槽位】", 14, 9, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

            float slotY = 32.0f;
            float slotH = 56.0f;

            for (int s = 0; s < 3; ++s)
            {
                float sy = slotY + s * (slotH + 4);
                bool sel = (s == s_selectedItem);

                C2D_DrawRectSolid(12, sy, GFX_DEPTH_DEFAULT, 296, slotH, sel ? gfx::btnSel : gfx::btnClr);

                if (sel)
                {
                    gfx::drawBoundingBox(12, sy, 296, slotH, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);
                }

                const auto &sd = nlpp::g_saveMgr.getSlotData(s);
                char sbuf[128];
                if (sd.empty)
                {
                    snprintf(sbuf, sizeof(sbuf), "槽位 %d: [空存档位] (未建档)", s + 1);
                    gfx::drawText(sbuf, 20, sy + 18, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);
                }
                else
                {
                    const char *hName = (sd.girlfriend.heroine == nlpp::HEROINE_MANAKA) ? "高嶺 愛花" :
                                        ((sd.girlfriend.heroine == nlpp::HEROINE_RINKO) ? "小早川 凛子" : "姉崎 寧々");
                    snprintf(sbuf, sizeof(sbuf), "槽位 %d: 玩家: %s %s  (有效档案)", s + 1, sd.profile.lastName.c_str(), sd.profile.firstName.c_str());
                    gfx::drawText(sbuf, 20, sy + 8, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

                    snprintf(sbuf, sizeof(sbuf), "恋人: %s  交往第 %u 天", hName, (unsigned)sd.timeline.datingDays);
                    gfx::drawText(sbuf, 20, sy + 30, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
                }
            }

            ui::drawUIBar("\ue000 选择  \ue002 复制  \ue003 备份  \ue001 关闭", ui::SCREEN_BOT, true);
        }
    }

    static void drawMode1Bot_Backup()
    {
        drawCard(10, 10, 300, 204);
        C2D_DrawRectSolid(10, 10, GFX_DEPTH_DEFAULT, 300, 24, gfx::rectSh);
        gfx::drawText("【备份详情】", 16, 14, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        auto bList = nlpp::g_saveMgr.listBackups();
        if (bList.empty() || s_selectedItem >= (int)bList.size())
        {
            gfx::drawText("未选中备份镜像", 16, 50, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        }
        else
        {
            const auto &b = bList[s_selectedItem];
            gfx::drawText("镜像名称: " + b.dirName, 16, 44, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
            gfx::drawText("存储位置: sdmc:/NLPP_BACKUP/" + b.dirName, 16, 70, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
            gfx::drawText("操作提示: 按 \ue003 键执行还原覆写当前介质", 16, 110, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
            gfx::drawText("按 \ue002 键可永久删除该备份", 16, 134, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        }

        ui::drawUIBar("\ue002 删除  \ue003 恢复  \ue01A\ue077\ue019 切换视图", ui::SCREEN_BOT, true);
    }

    static void drawMode1Bot_Settings()
    {
        drawCard(10, 10, 300, 204);
        C2D_DrawRectSolid(10, 10, GFX_DEPTH_DEFAULT, 300, 24, gfx::rectSh);

        static const char *titles[] = {
            "【自动备份存档】", "【导出到 ZIP】", "【ZIP 压缩等级】", "【界面主题色】",
            "【界面语言】", "【启动时加载存档】", "【默认存档类型】", "【默认存档槽位】"
        };
        static const char *descs[] = {
            "每次提交保存或覆写修改时，首先在 SD 卡\nsdmc:/NLPP_BACKUP/ 自动生成带时间戳的安全备份。",
            "备份存档时是否生成单一 .zip 压缩文件包\n（方便导出到电脑与冷存储）。",
            "设置 ZIP 压缩等级 (1~9)。\n数值越高压缩率越高，耗费时间略微增加。",
            "切换应用界面主题配色。\n支持浅色系 (Light) 与深色系 (Dark)。",
            "切换软件显示语言。\n支持简体中文、English 与繁體中文。",
            "应用启动时直接载入默认存档并跳过主菜单。\n若默认介质不可用或槽位为空则平滑安全降级。",
            "设置启动时自动载入的默认存档介质。\n（数字版 SD 或 实体卡带版）。",
            "设置启动时自动载入的默认存档槽位 (1~3)。"
        };

        int idx = (s_selectedItem >= 0 && s_selectedItem < 8) ? s_selectedItem : 0;
        gfx::drawText(titles[idx], 16, 14, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);
        gfx::drawTextWrap(descs[idx], 16, 48, GFX_DEPTH_DEFAULT, 0.40f, 280, gfx::txtCont);

        ui::drawUIBar("\ue000 选择/切换  \ue01A\ue077\ue019 切换视图", ui::SCREEN_BOT, true);
    }

    // =========================================================================
    // Mode 2: Bottom Screen Drawing (7 Edit Pages + Large Save Button)
    // =========================================================================

    static void drawMode2Bot_Edit()
    {
        int curSlot = nlpp::g_saveMgr.getCurrentSlot();
        auto &slotData = nlpp::g_saveMgr.getSlotData(curSlot);

        // Subheader: Page title & pagination
        char subH[64];
        snprintf(subH, sizeof(subH), "【%d/7 %s】 (\ue01A\ue077\ue019 翻页)", s_editPage + 1, s_editPageTitles[s_editPage]);
        gfx::drawText(subH, 12, 4, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        float startY = 22.0f;
        float btnH = 21.0f;
        float strideY = 23.0f;
        float btnW = 300.0f;
        float btnX = 10.0f;

        char buf[128];

        auto renderBtn = [&](int idx, const std::string &lbl, const std::string &bdg = "") {
            bool focused = (idx == s_selectedItem && !s_confirm.active && !s_picker.active);
            drawButton(btnX, startY + idx * strideY, btnW, btnH, lbl, focused, bdg);
        };

        int saveBtnIdx = 7;

        switch (s_editPage)
        {
            // Page 0: 玩家信息
            case 0:
            {
                saveBtnIdx = 7;
                snprintf(buf, sizeof(buf), "玩家姓氏: %s", slotData.profile.lastName.c_str());
                renderBtn(0, buf, "修改");

                snprintf(buf, sizeof(buf), "玩家名字: %s", slotData.profile.firstName.c_str());
                renderBtn(1, buf, "修改");

                snprintf(buf, sizeof(buf), "通信昵称(HN): %s", slotData.profile.hn.c_str());
                renderBtn(2, buf, "修改");

                snprintf(buf, sizeof(buf), "出生月份: %d月", slotData.profile.birthMonth);
                renderBtn(3, buf, "调节");

                snprintf(buf, sizeof(buf), "出生日期: %d日", slotData.profile.birthDay);
                renderBtn(4, buf, "调节");

                snprintf(buf, sizeof(buf), "玩家血型: %s", nlpp::Dict::getBloodTypeName(slotData.profile.blood));
                renderBtn(5, buf, "选择");

                snprintf(buf, sizeof(buf), "出身地域: %s", nlpp::Dict::getPrefectureName(slotData.profile.prefecture));
                renderBtn(6, buf, "选择");
                break;
            }

            // Page 1: 发型形象
            case 1:
            {
                saveBtnIdx = 5;
                const char *stName = nlpp::Dict::getStyleName(slotData.girlfriend.hairStyleId);
                snprintf(buf, sizeof(buf), "发型款式: %s (ID: %d)", stName, slotData.girlfriend.hairStyleId);
                renderBtn(0, buf, "更换");

                const char *lenName = nlpp::Dict::getHairLengthName(slotData.girlfriend.heroine, slotData.girlfriend.hairLength);
                snprintf(buf, sizeof(buf), "头发长度: %s", lenName);
                renderBtn(1, buf, "更换");

                const auto &cdef = nlpp::Dict::getHairColor(slotData.girlfriend.hairColorId);
                snprintf(buf, sizeof(buf), "发色方案: %s - %s", cdef.category, cdef.tone);
                renderBtn(2, buf, "更换");

                renderBtn(3, "重置为该女主官方初始形象", "重置");
                renderBtn(4, "解除发型锁定 (自由尝试全部发型)", "自由变换");
                break;
            }

            // Page 2: 呼称称呼
            case 2:
            {
                saveBtnIdx = 6;
                static const char *s_slotNames[] = {"0:姓氏", "1:名字", "2:后缀", "3:预设", "4:爱称"};
                uint32_t sel = slotData.stats.currentSelector;
                snprintf(buf, sizeof(buf), "生效呼称槽: [%s]", s_slotNames[sel < 5 ? sel : 0]);
                renderBtn(0, buf, "切换");

                for (int r = 0; r < 5; ++r)
                {
                    uint32_t rid = slotData.stats.readings[r];
                    std::string rtxt = nlpp::Dict::getReadingText(rid);
                    snprintf(buf, sizeof(buf), "槽%d [%s]: 「%s」", r, s_slotNames[r], rtxt.c_str());
                    renderBtn(r + 1, buf, "选择读音");
                }
                break;
            }

            // Page 3: 彼氏能力
            case 3:
            {
                saveBtnIdx = 7;
                snprintf(buf, sizeof(buf), "运动能力: %3d / 255", slotData.stats.fitness);
                renderBtn(0, buf, "调节");

                snprintf(buf, sizeof(buf), "知识学力: %3d / 255", slotData.stats.knowledge);
                renderBtn(1, buf, "调节");

                snprintf(buf, sizeof(buf), "感性艺术: %3d / 255", slotData.stats.sensibility);
                renderBtn(2, buf, "调节");

                snprintf(buf, sizeof(buf), "魅力交际: %3d / 255", slotData.stats.charm);
                renderBtn(3, buf, "调节");

                snprintf(buf, sizeof(buf), "所持金钱: %u G", (unsigned)slotData.stats.rich);
                renderBtn(4, buf, "MAX");

                snprintf(buf, sizeof(buf), "TODO点数: %u 点", (unsigned)slotData.stats.todo);
                renderBtn(5, buf, "MAX");

                renderBtn(6, "【一键彼氏力全要素全满】 (四维+金钱+TODO+卸滤镜)", "一键全满");
                break;
            }

            // Page 4: 恋人关系
            case 4:
            {
                saveBtnIdx = 6;
                snprintf(buf, sizeof(buf), "女友好感度: %d / 255", slotData.girlfriend.affection);
                renderBtn(0, buf, "升满");

                snprintf(buf, sizeof(buf), "性格横轴 X (文静<->活泼): %d", slotData.girlfriend.sliderX);
                renderBtn(1, buf, "调整");

                snprintf(buf, sizeof(buf), "性格纵轴 Y (理性<->感性): %d", slotData.girlfriend.sliderY);
                renderBtn(2, buf, "调整");

                snprintf(buf, sizeof(buf), "性格形态 Z: %d / 4", slotData.girlfriend.sliderZ);
                renderBtn(3, buf, "切换");

                const auto &title = nlpp::Dict::getCoupleTitleByExp(slotData.girlfriend.coupleExp);
                snprintf(buf, sizeof(buf), "情侣称号: Lv.%d %s", title.rank, title.name_jp);
                renderBtn(4, buf, "修改");

                renderBtn(5, "【一键直达满级情侣称号】 Lv.23 (23枚黄金勋章)", "一键满级");
                break;
            }

            // Page 5: 日程生活
            case 5:
            {
                saveBtnIdx = 3;
                snprintf(buf, sizeof(buf), "实机陪伴天数 (周年对白): %u 天", (unsigned)slotData.timeline.datingDays);
                renderBtn(0, buf, "修改");

                snprintf(buf, sizeof(buf), "日历总历天数 (累积时间): %u 日", (unsigned)slotData.timeline.timelineDays);
                renderBtn(1, buf, "修改");

                renderBtn(2, "清除离线天数 (彻底解除冷战惩罚)", "清除冷战");
                break;
            }

            // Page 6: 全量收集
            case 6:
            {
                saveBtnIdx = 3;
                renderBtn(0, "【一键全量解锁 513 件全套衣柜】", "全衣柜");
                renderBtn(1, "【一键全要素点亮 1,317 项全局主画廊】", "全画廊");
                renderBtn(2, "【一键批量清除全图鉴与画廊 NEW 红点】", "清红点");
                break;
            }
        }

        // Permanent Large Save Button at bottom of every edit page
        float saveY = 196.0f;
        float saveH = 24.0f;
        bool fSave = (s_selectedItem == saveBtnIdx && !s_confirm.active && !s_picker.active);

        C2D_DrawRectSolid(btnX, saveY, GFX_DEPTH_DEFAULT, btnW, saveH, fSave ? gfx::btnSel : gfx::btnClr);
        C2D_DrawRectSolid(btnX, saveY, GFX_DEPTH_DEFAULT, btnW, 1, gfx::divClr);
        C2D_DrawRectSolid(btnX, saveY + saveH - 1, GFX_DEPTH_DEFAULT, btnW, 1, gfx::divClr);
        C2D_DrawRectSolid(btnX, saveY, GFX_DEPTH_DEFAULT, 1, saveH, gfx::divClr);
        C2D_DrawRectSolid(btnX + btnW - 1, saveY, GFX_DEPTH_DEFAULT, 1, saveH, gfx::divClr);

        if (fSave)
        {
            gfx::drawBoundingBox(btnX, saveY, btnW, saveH, GFX_DEPTH_DEFAULT, COLOR_FOCUS_BLUE);
        }

        gfx::drawText("【★ 保存写入存档】 (提交全部修改至 3DS 介质)", btnX + 24, saveY + 4, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        ui::drawUIBar("\ue000 选择  \ue001 退出  \ue01A\ue077\ue019 切换视图", ui::SCREEN_BOT, true);
    }

    void nlppDrawBot()
    {
        if (s_appMode == MODE_MAIN_MENU)
        {
            switch (s_mainView)
            {
                case 0: drawMode1Bot_SaveSelect(); break;
                case 1: drawMode1Bot_Backup(); break;
                case 2: drawMode1Bot_Settings(); break;
            }
        }
        else // MODE_SAVE_EDIT
        {
            drawMode2Bot_Edit();
        }

        // Draw Dialog Overlays if active
        if (s_picker.active)
        {
            drawPickerDialog();
        }
        else if (s_confirm.active)
        {
            drawConfirmDialog();
        }
        else if (s_alert.active)
        {
            drawAlertDialog();
        }

        // Toast overlay at very bottom if active
        if (!s_toastMessage.empty())
        {
            ui::drawUIBar(s_toastMessage, ui::SCREEN_BOT, true);
            if (--s_toastTimer <= 0) s_toastMessage = "";
        }
    }

    // =========================================================================
    // Input Handling (Pure Physical Keys, Zero Touch)
    // =========================================================================

    static void updateConfirmDialog(uint32_t down)
    {
        if (down & (KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_LEFT | KEY_CPAD_RIGHT))
        {
            s_confirm.selectedBtn = 1 - s_confirm.selectedBtn;
        }

        if (down & KEY_A)
        {
            if (s_confirm.selectedBtn == 0)
            {
                auto cb = s_confirm.onConfirm;
                s_confirm.active = false;
                if (cb) cb();
            }
            else
            {
                auto cb = s_confirm.onCancel;
                s_confirm.active = false;
                if (cb) cb();
            }
        }
        else if (down & KEY_B)
        {
            auto cb = s_confirm.onCancel;
            s_confirm.active = false;
            if (cb) cb();
        }
    }

    static void updateAlertDialog(uint32_t down)
    {
        if (down & (KEY_A | KEY_B))
        {
            s_alert.active = false;
        }
    }

    static void updatePickerDialog(uint32_t down)
    {
        int count = (int)s_picker.items.size();
        if (count == 0)
        {
            if (down & (KEY_A | KEY_B)) s_picker.active = false;
            return;
        }

        if (down & KEY_DUP)
        {
            s_picker.selected = (s_picker.selected + count - 1) % count;
            if (s_picker.selected < s_picker.scroll) s_picker.scroll = s_picker.selected;
            if (s_picker.selected >= s_picker.scroll + 7) s_picker.scroll = s_picker.selected - 6;
        }
        if (down & KEY_DDOWN)
        {
            s_picker.selected = (s_picker.selected + 1) % count;
            if (s_picker.selected >= s_picker.scroll + 7) s_picker.scroll = s_picker.selected - 6;
            if (s_picker.selected < s_picker.scroll) s_picker.scroll = s_picker.selected;
        }

        if (down & KEY_A)
        {
            auto cb = s_picker.onSelect;
            auto item = s_picker.items[s_picker.selected];
            s_picker.active = false;
            if (cb) cb(item);
        }
        else if (down & KEY_B)
        {
            s_picker.active = false;
        }
    }

    static void handleMode1Action(uint32_t down)
    {
        // View 0: 存档选择
        if (s_mainView == 0)
        {
            if (!s_slotSelectOpen)
            {
                // Move between 数字版 (0) and 实体版 (1)
                if (down & KEY_DUP) s_selectedItem = (s_selectedItem + 1) % 2;
                if (down & KEY_DDOWN) s_selectedItem = (s_selectedItem + 1) % 2;

                if (down & KEY_A)
                {
                    nlpp::g_saveMgr.detectSources();
                    nlpp::SaveSource targetSrc = (s_selectedItem == 0) ? nlpp::SOURCE_SD : nlpp::SOURCE_GAMECARD;
                    bool available = (targetSrc == nlpp::SOURCE_GAMECARD) ? nlpp::g_saveMgr.hasGameCard() : nlpp::g_saveMgr.hasSD();

                    if (!available)
                    {
                        const char *failMsg = (s_selectedItem == 0) ?
                            "未检测到《新爱相随+》数字版存档数据！\n请确认游戏已安装或模拟器 SD 卡已正确挂载。" :
                            "未检测到《新爱相随+》游戏卡带！\n请插入卡带后重新检测。";
                        showAlert("介质未就绪", failMsg);
                    }
                    else
                    {
                        nlpp::g_saveMgr.switchSource(targetSrc);
                        s_slotSelectOpen = true;
                        s_selectedItem = 0;
                    }
                }
            }
            else
            {
                // In slot selection on bottom screen
                if (down & KEY_DUP) s_selectedItem = (s_selectedItem + 2) % 3;
                if (down & KEY_DDOWN) s_selectedItem = (s_selectedItem + 1) % 3;

                if (down & KEY_B)
                {
                    s_slotSelectOpen = false;
                    s_selectedItem = (nlpp::g_saveMgr.getSource() == nlpp::SOURCE_GAMECARD) ? 1 : 0;
                    return;
                }

                if (down & KEY_X) // 复制/克隆
                {
                    int srcSlot = s_selectedItem;
                    std::vector<PickerItem> opts;
                    for (int s = 0; s < 3; ++s)
                    {
                        if (s == srcSlot) continue;
                        opts.push_back({s, "覆盖至 槽位 " + std::to_string(s + 1), "克隆槽位全数据"});
                    }
                    openPicker("选择克隆目标覆盖槽位", opts, (srcSlot + 1) % 3, [srcSlot](const PickerItem &item) {
                        char msg[160];
                        snprintf(msg, sizeof(msg), "警告：确定要将槽位 %d 的数据克隆覆盖至槽位 %d 吗？\n（目标槽位原有数据将被覆盖！）", srcSlot + 1, item.id + 1);
                        requestConfirm("槽位克隆二次确认", msg, [srcSlot, item]() {
                            nlpp::g_saveMgr.cloneSlot(srcSlot, item.id);
                            s_toastMessage = "槽位数据克隆成功!";
                            s_toastTimer = 180;
                        });
                    });
                    return;
                }

                if (down & KEY_Y) // 立即备份
                {
                    requestConfirm("自主备份确认", "确定立即创建当前存档完整镜像并导出至 SD 卡吗？\n（保存至 sdmc:/NLPP_BACKUP/）", []() {
                        std::string outPath;
                        nlpp::g_saveMgr.createAutoBackup(&outPath);
                        s_toastMessage = "备份成功! 位于 sdmc:/NLPP_BACKUP/";
                        s_toastTimer = 180;
                    });
                    return;
                }

                if (down & KEY_A)
                {
                    const auto &slotData = nlpp::g_saveMgr.getSlotData(s_selectedItem);
                    if (slotData.empty)
                    {
                        showAlert("槽位为空", "该槽位为空存档，无法进入编辑！\n请先在游戏中建档或使用 \ue002 复制功能。");
                    }
                    else
                    {
                        nlpp::g_saveMgr.selectSlot(s_selectedItem);
                        s_appMode = MODE_SAVE_EDIT;
                        s_editPage = 0;
                        s_selectedItem = 0;
                    }
                }
            }
        }
        // View 1: 备份管理
        else if (s_mainView == 1)
        {
            auto bList = nlpp::g_saveMgr.listBackups();
            int count = (int)bList.size();
            if (count > 0)
            {
                if (down & KEY_DUP) s_selectedItem = (s_selectedItem + count - 1) % count;
                if (down & KEY_DDOWN) s_selectedItem = (s_selectedItem + 1) % count;

                if (down & KEY_X) // 删除
                {
                    std::string dirName = bList[s_selectedItem].dirName;
                    requestConfirm("删除备份确认", "确定要永久删除备份镜像 [" + dirName + "] 吗？", [dirName]() {
                        std::string path = "sdmc:/NLPP_BACKUP/" + dirName;
                        fs::delDirRec(fs::getSDMCArch(), util::toUtf16(path));
                        s_toastMessage = "备份已删除!";
                        s_toastTimer = 180;
                    });
                }
                else if (down & KEY_Y) // 恢复
                {
                    std::string dirName = bList[s_selectedItem].dirName;
                    requestConfirm("恢复备份确认", "确定要从镜像 [" + dirName + "] 恢复覆盖当前存档吗？\n（当前内存中的修改将被替换！）", [dirName]() {
                        nlpp::g_saveMgr.restoreBackup(dirName);
                        s_toastMessage = "存档已从备份镜像还原!";
                        s_toastTimer = 180;
                    });
                }
            }
        }
        // View 2: 设置
        else if (s_mainView == 2)
        {
            if (down & KEY_DUP) s_selectedItem = (s_selectedItem + 7) % 8;
            if (down & KEY_DDOWN) s_selectedItem = (s_selectedItem + 1) % 8;

            if (down & KEY_A)
            {
                switch (s_selectedItem)
                {
                    case 0: // 自动备份
                        cfg::config["autobackup"] = 1 - cfg::config["autobackup"];
                        cfg::saveCommon();
                        break;
                    case 1: // 导出到 ZIP
                        cfg::config["zip"] = 1 - cfg::config["zip"];
                        cfg::saveCommon();
                        break;
                    case 2: // ZIP 压缩等级 1..9
                    {
                        int lvl = cfg::config["deflateLevel"];
                        lvl = (lvl % 9) + 1;
                        cfg::config["deflateLevel"] = lvl;
                        cfg::saveCommon();
                        break;
                    }
                    case 3: // 界面主题色
                    {
                        bool curLight = (cfg::config["lightback"] != 0);
                        cfg::config["lightback"] = curLight ? 0 : 1;
                        gfx::setColor(cfg::config["lightback"]);
                        cfg::saveCommon();
                        break;
                    }
                    case 4: // 界面语言
                    {
                        int l = (cfg::config["uilang"] + 1) % 3;
                        cfg::config["uilang"] = l;
                        cfg::setUILanguage(l);
                        cfg::saveCommon();
                        break;
                    }
                    case 5: // 启动时加载存档
                        cfg::config["autoload"] = 1 - cfg::config["autoload"];
                        cfg::saveCommon();
                        break;
                    case 6: // 默认存档类型
                        cfg::config["default_type"] = 1 - cfg::config["default_type"];
                        cfg::saveCommon();
                        break;
                    case 7: // 默认存档槽位
                        cfg::config["default_slot"] = (cfg::config["default_slot"] + 1) % 3;
                        cfg::saveCommon();
                        break;
                }
            }
        }
    }

    static void handleMode2Action(uint32_t down)
    {
        int curSlot = nlpp::g_saveMgr.getCurrentSlot();
        auto &slotData = nlpp::g_saveMgr.getSlotData(curSlot);

        // Maximum buttons per page (including the last permanent Save button)
        int maxItems = 8;
        if (s_editPage == 0) maxItems = 8;
        else if (s_editPage == 1) maxItems = 6;
        else if (s_editPage == 2) maxItems = 7;
        else if (s_editPage == 3) maxItems = 8;
        else if (s_editPage == 4) maxItems = 7;
        else if (s_editPage == 5) maxItems = 4;
        else if (s_editPage == 6) maxItems = 4;

        if (down & KEY_DUP) s_selectedItem = (s_selectedItem + maxItems - 1) % maxItems;
        if (down & KEY_DDOWN) s_selectedItem = (s_selectedItem + 1) % maxItems;

        // B key exits Edit Mode back to Slot Select
        if (down & KEY_B)
        {
            requestConfirm("退出编辑模式", "确定退出存档编辑模式吗？\n未保存写回的修改将会丢失。", []() {
                s_appMode = MODE_MAIN_MENU;
                s_mainView = 0;
                s_slotSelectOpen = true;
                s_selectedItem = nlpp::g_saveMgr.getCurrentSlot();
            });
            return;
        }

        // Check if user pressed A on the last button: 【★ 保存写入存档】
        if (down & KEY_A && s_selectedItem == maxItems - 1)
        {
            requestConfirm("提交保存确认", "确定将当前全部修改提交写入 3DS 介质吗？\n（写回前将在 SD 卡自动生成安全备份，防坏档）", []() {
                nlppSaveAndCommit();
            });
            return;
        }

        // Handle specific page button actions
        if (down & KEY_A)
        {
            switch (s_editPage)
            {
                // Page 0: 玩家信息
                case 0:
                {
                    if (s_selectedItem == 0) // 姓氏
                    {
                        std::string input = inputStringSwkbd("请输入玩家姓氏 (支持汉字/假名)", slotData.profile.lastName, 8);
                        if (!input.empty() && input != slotData.profile.lastName)
                        {
                            requestConfirm("修改姓氏确认", "确定将玩家姓氏修改为「" + input + "」吗？", [curSlot, input]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.lastName = input;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "玩家姓氏修改成功!";
                                s_toastTimer = 180;
                            });
                        }
                    }
                    else if (s_selectedItem == 1) // 名字
                    {
                        std::string input = inputStringSwkbd("请输入玩家名字 (支持汉字/假名)", slotData.profile.firstName, 8);
                        if (!input.empty() && input != slotData.profile.firstName)
                        {
                            requestConfirm("修改名字确认", "确定将玩家名字修改为「" + input + "」吗？", [curSlot, input]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.firstName = input;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "玩家名字修改成功!";
                                s_toastTimer = 180;
                            });
                        }
                    }
                    else if (s_selectedItem == 2) // HN
                    {
                        std::string input = inputStringSwkbd("请输入通信昵称 (HN)", slotData.profile.hn, 8);
                        if (!input.empty() && input != slotData.profile.hn)
                        {
                            requestConfirm("修改通信昵称确认", "确定将通信昵称(HN)修改为「" + input + "」吗？", [curSlot, input]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.hn = input;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "通信昵称修改成功!";
                                s_toastTimer = 180;
                            });
                        }
                    }
                    else if (s_selectedItem == 3) // 月份
                    {
                        std::vector<PickerItem> opts;
                        for (int m = 1; m <= 12; ++m) opts.push_back({m, std::to_string(m) + "月", ""});
                        openPicker("选择出生月份", opts, slotData.profile.birthMonth, [curSlot](const PickerItem &item) {
                            requestConfirm("修改出生月份确认", "确定将出生月份修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.birthMonth = (uint8_t)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "出生月份已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 4) // 日期
                    {
                        std::vector<PickerItem> opts;
                        for (int d = 1; d <= 31; ++d) opts.push_back({d, std::to_string(d) + "日", ""});
                        openPicker("选择出生日期", opts, slotData.profile.birthDay, [curSlot](const PickerItem &item) {
                            requestConfirm("修改出生日期确认", "确定将出生日期修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.birthDay = (uint8_t)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "出生日期已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 5) // 血型
                    {
                        std::vector<PickerItem> opts = {
                            {0, "A型", ""}, {1, "B型", ""}, {2, "AB型", ""}, {3, "O型", ""}
                        };
                        openPicker("选择玩家血型", opts, (int)slotData.profile.blood, [curSlot](const PickerItem &item) {
                            requestConfirm("修改血型确认", "确定将玩家血型修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.blood = (nlpp::BloodType)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "血型已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 6) // 出身地
                    {
                        std::vector<PickerItem> opts;
                        int count = nlpp::Dict::getPrefectureCount();
                        for (int p = 0; p < count; ++p)
                            opts.push_back({p, nlpp::Dict::getPrefectureName(p), "ID: " + std::to_string(p)});
                        openPicker("选择出身地域", opts, slotData.profile.prefecture, [curSlot](const PickerItem &item) {
                            requestConfirm("修改出身地确认", "确定将出身地修改为「" + item.label + "」吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.prefecture = (uint16_t)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                                s_toastMessage = "出身地域已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    break;
                }

                // Page 1: 发型形象
                case 1:
                {
                    if (s_selectedItem == 0) // 发型款式
                    {
                        auto styles = nlpp::Dict::getAvailableStyles(slotData.girlfriend.heroine, slotData.girlfriend.hairLength);
                        std::vector<PickerItem> opts;
                        if (!styles.empty())
                        {
                            for (const auto &st : styles)
                                opts.push_back({st.style_id, st.name, "ID: " + std::to_string(st.style_id)});
                        }
                        else
                        {
                            for (int s = 0; s < 24; ++s)
                                opts.push_back({s, nlpp::Dict::getStyleName(s), "ID: " + std::to_string(s)});
                        }
                        openPicker("选择发型款式", opts, slotData.girlfriend.hairStyleId, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将发型款式修改为「%s (ID:%d)」吗？", item.label.c_str(), item.id);
                            requestConfirm("发型款式修改确认", msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.hairStyleId = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                                s_toastMessage = "发型款式修改成功!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 1) // 头发长度
                    {
                        auto lengths = nlpp::Dict::getValidLengthStages(slotData.girlfriend.heroine);
                        std::vector<PickerItem> opts;
                        for (uint8_t l : lengths)
                            opts.push_back({l, nlpp::Dict::getHairLengthName(slotData.girlfriend.heroine, l), "阶段 " + std::to_string(l)});
                        openPicker("选择头发长度阶段", opts, slotData.girlfriend.hairLength, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将头发长度阶段调整为「%s」吗？", item.label.c_str());
                            requestConfirm("发长调整确认", msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.hairLength = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                                s_toastMessage = "头发长度阶段已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 2) // 发色方案
                    {
                        std::vector<PickerItem> opts;
                        int colorCount = nlpp::Dict::getHairColorCount();
                        for (int c = 0; c < colorCount; ++c)
                        {
                            const auto &cdef = nlpp::Dict::getHairColor(c);
                            u32 rgb = C2D_Color32(cdef.r, cdef.g, cdef.b, 255);
                            PickerItem pi;
                            pi.id = c;
                            pi.label = std::string(cdef.category) + " - " + cdef.tone;
                            pi.detail = "ID: " + std::to_string(c);
                            pi.colorRgb = rgb;
                            pi.hasColor = true;
                            opts.push_back(pi);
                        }
                        openPicker("选择发色方案", opts, slotData.girlfriend.hairColorId, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将发色方案修改为「%s」吗？", item.label.c_str());
                            requestConfirm("发色修改确认", msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.hairColorId = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                                s_toastMessage = "发色方案修改成功!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 3) // 重置形象
                    {
                        requestConfirm("重置发型形象确认", "确定将发型款式、发长和发色重置为该女友的官方初始默认形象吗？", [curSlot]() {
                            auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                            sd.girlfriend.hairStyleId = 0;
                            sd.girlfriend.hairColorId = 0;
                            if (sd.girlfriend.heroine == nlpp::HEROINE_MANAKA) sd.girlfriend.hairLength = 2;
                            else if (sd.girlfriend.heroine == nlpp::HEROINE_RINKO) sd.girlfriend.hairLength = 1;
                            else if (sd.girlfriend.heroine == nlpp::HEROINE_NENE) sd.girlfriend.hairLength = 2;
                            nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            s_toastMessage = "已重置为官方默认形象!";
                            s_toastTimer = 180;
                        });
                    }
                    else if (s_selectedItem == 4) // 解除限制
                    {
                        requestConfirm("自由发型确认", "确定开启发型自由变换，允许女友自由尝试全部发型吗？", [curSlot]() {
                            auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                            sd.girlfriend.reservedStyleId = 0xFF;
                            sd.girlfriend.hairTasteTarget = 0;
                            nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            s_toastMessage = "已解除发型限制, 开启自由变换!";
                            s_toastTimer = 180;
                        });
                    }
                    break;
                }

                // Page 2: 呼称称呼
                case 2:
                {
                    if (s_selectedItem == 0) // 生效呼称槽
                    {
                        std::vector<PickerItem> opts = {
                            {0, "槽0: 姓氏 (LastName)", ""},
                            {1, "槽1: 名字 (FirstName)", ""},
                            {2, "槽2: 后缀 (Suffix)", ""},
                            {3, "槽3: 预设 (Preset)", ""},
                            {4, "槽4: 爱称 (Custom)", ""}
                        };
                        openPicker("选择当前生效呼称槽", opts, slotData.stats.currentSelector, [curSlot](const PickerItem &item) {
                            requestConfirm("生效呼称切换确认", "确定将生效呼称槽切换为 [" + item.label + "] 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.stats.currentSelector = (uint32_t)item.id;
                                nlpp::g_saveMgr.setBoyfriendStats(curSlot, sd.stats);
                                s_toastMessage = "生效呼称槽已切换!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem >= 1 && s_selectedItem <= 5)
                    {
                        int rslot = s_selectedItem - 1;
                        uint8_t slotMask = (1 << rslot);
                        uint8_t heroMask = (1 << slotData.girlfriend.heroine);
                        auto list = nlpp::Dict::filterReadings(slotMask, heroMask);
                        if (list.empty()) list = nlpp::Dict::filterReadings(slotMask, 0);

                        std::vector<PickerItem> opts;
                        for (const auto *e : list)
                            opts.push_back({(int)e->id, e->text, "ID: " + std::to_string(e->id)});

                        openPicker("选择槽" + std::to_string(rslot) + " 呼称读音", opts, slotData.stats.readings[rslot], [curSlot, rslot](const PickerItem &item) {
                            char msg[160];
                            snprintf(msg, sizeof(msg), "确定将槽%d呼称修改为「%s」吗？\n（将执行三位一体同步写入主档、待机与日志）", rslot, item.label.c_str());
                            requestConfirm("呼称读音修改确认", msg, [curSlot, rslot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.stats.readings[rslot] = (uint32_t)item.id;
                                nlpp::g_saveMgr.setBoyfriendStats(curSlot, sd.stats);
                                s_toastMessage = "呼称修改成功并已三位一体同步!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    break;
                }

                // Page 3: 彼氏能力
                case 3:
                {
                    if (s_selectedItem >= 0 && s_selectedItem <= 3)
                    {
                        static const char *statNames[] = {"运动能力", "知识学力", "感性艺术", "魅力交际"};
                        std::vector<PickerItem> opts = {
                            {255, "满值 (255)", "MAX"},
                            {200, "优秀 (200)", ""},
                            {150, "良好 (150)", ""},
                            {100, "普通 (100)", ""},
                            {50,  "基础 (50)",  ""}
                        };
                        int itemIdx = s_selectedItem;
                        openPicker(std::string("调节") + statNames[itemIdx], opts, 255, [curSlot, itemIdx](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将【%s】修改为 %d 吗？", statNames[itemIdx], item.id);
                            requestConfirm("属性修改确认", msg, [curSlot, itemIdx, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                if (itemIdx == 0) sd.stats.fitness = (uint8_t)item.id;
                                else if (itemIdx == 1) sd.stats.knowledge = (uint8_t)item.id;
                                else if (itemIdx == 2) sd.stats.sensibility = (uint8_t)item.id;
                                else if (itemIdx == 3) sd.stats.charm = (uint8_t)item.id;
                                nlpp::g_saveMgr.setBoyfriendStats(curSlot, sd.stats);
                                s_toastMessage = "属性值修改成功!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 4) // 金钱
                    {
                        requestConfirm("修改所持金确认", "确定将所持金钱修改为最大值 999,999 G 吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxRichMoney(curSlot);
                            s_toastMessage = "所持金钱已设为 999,999 G!";
                            s_toastTimer = 180;
                        });
                    }
                    else if (s_selectedItem == 5) // TODO点数
                    {
                        requestConfirm("修改TODO点数确认", "确定将 TODO 点数修改为最大值 99,999 点吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxTodoPoints(curSlot);
                            s_toastMessage = "TODO 点数已设为 99,999 点!";
                            s_toastTimer = 180;
                        });
                    }
                    else if (s_selectedItem == 6) // 一键彼氏力全满
                    {
                        requestConfirm("彼氏力全满确认", "确定将彼氏力四维(255)、金钱(999,999G)、TODO(99,999点)全部升满并卸除滤镜吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxBoyfriendStats(curSlot);
                            nlpp::g_saveMgr.maxRichMoney(curSlot);
                            nlpp::g_saveMgr.maxTodoPoints(curSlot);
                            nlpp::g_saveMgr.removeCameraFilter(curSlot);
                            s_toastMessage = "彼氏力四维、金钱、TODO已全满, 滤镜已卸除!";
                            s_toastTimer = 180;
                        });
                    }
                    break;
                }

                // Page 4: 恋人关系
                case 4:
                {
                    if (s_selectedItem == 0) // 好感度升满
                    {
                        requestConfirm("女友好感度确认", "确定将女友好感度升满至 255 吗？\n（同时将消除冷战惩罚与心情低落）", [curSlot]() {
                            nlpp::g_saveMgr.maxGirlfriendAffection(curSlot);
                            s_toastMessage = "女友好感度已升至满值 255!";
                            s_toastTimer = 180;
                        });
                    }
                    else if (s_selectedItem == 1) // 性格横轴 X
                    {
                        std::vector<PickerItem> opts = {
                            {100, "文静典雅 (100)", "极文静"},
                            {300, "偏向内向 (300)", ""},
                            {500, "中庸平衡 (500)", "平衡"},
                            {700, "偏向活泼 (700)", ""},
                            {900, "元气活泼 (900)", "极活泼"}
                        };
                        openPicker("选择性格横轴 X 倾向", opts, slotData.girlfriend.sliderX, [curSlot](const PickerItem &item) {
                            requestConfirm("性格修改确认", "确定将性格横轴 X 修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.sliderX = item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                                s_toastMessage = "性格横轴 X 已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 2) // 性格纵轴 Y
                    {
                        std::vector<PickerItem> opts = {
                            {100, "严谨理性 (100)", "极理性"},
                            {300, "偏向理性 (300)", ""},
                            {500, "中庸平衡 (500)", "平衡"},
                            {700, "偏向感性 (700)", ""},
                            {900, "温柔感性 (900)", "极感性"}
                        };
                        openPicker("选择性格纵轴 Y 倾向", opts, slotData.girlfriend.sliderY, [curSlot](const PickerItem &item) {
                            requestConfirm("性格修改确认", "确定将性格纵轴 Y 修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.sliderY = item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                                s_toastMessage = "性格纵轴 Y 已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 3) // 性格形态 Z
                    {
                        std::vector<PickerItem> opts = {
                            {0, "形态 0 (默认形态)", ""},
                            {1, "形态 1 (第二形态)", ""},
                            {2, "形态 2 (第三形态)", ""},
                            {3, "形态 3 (第四形态)", ""}
                        };
                        openPicker("选择性格形态 Z", opts, slotData.girlfriend.sliderZ, [curSlot](const PickerItem &item) {
                            requestConfirm("形态切换确认", "确定将性格形态 Z 切换为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.sliderZ = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                                s_toastMessage = "性格形态 Z 已修改!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 4) // 情侣称号
                    {
                        std::vector<PickerItem> opts;
                        for (int r = 1; r <= 23; ++r)
                        {
                            const auto &t = nlpp::Dict::getCoupleTitle(r);
                            opts.push_back({r, "Lv." + std::to_string(t.rank) + " " + t.name_jp, "EXP: " + std::to_string(t.exp_min)});
                        }
                        const auto &curT = nlpp::Dict::getCoupleTitleByExp(slotData.girlfriend.coupleExp);
                        openPicker("选择情侣称号等级", opts, curT.rank, [curSlot](const PickerItem &item) {
                            requestConfirm("情侣称号修改确认", "确定将情侣称号修改为「" + item.label + "」吗？", [curSlot, item]() {
                                nlpp::g_saveMgr.setCoupleTitleRank(curSlot, (uint8_t)item.id);
                                s_toastMessage = "情侣称号已修改成功!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 5) // 一键满级称号
                    {
                        requestConfirm("一键满级称号确认", "确定直达满级情侣称号「Lv.23 最愛のふたり」，并获得全部 23 枚黄金勋章吗？", [curSlot]() {
                            nlpp::g_saveMgr.setCoupleTitleRank(curSlot, 23);
                            s_toastMessage = "已直达 Lv.23 最愛のふたり (23枚黄金勋章)!";
                            s_toastTimer = 180;
                        });
                    }
                    break;
                }

                // Page 5: 日程生活
                case 5:
                {
                    if (s_selectedItem == 0) // 实机陪伴天数
                    {
                        std::vector<PickerItem> opts = {
                            {30,   "30 天 (满月纪念)", ""},
                            {100,  "100 天 (百日纪念)", "专属百日对白"},
                            {365,  "365 天 (一周年)",   "一周年专属纪念"},
                            {730,  "730 天 (两周年)",   "两周年专属纪念"},
                            {1000, "1000 天 (千日结缘)", "传奇纪念"}
                        };
                        openPicker("选择实机交往陪伴天数", opts, (int)slotData.timeline.datingDays, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将实机交往陪伴天数修改为 %d 天吗？", item.id);
                            requestConfirm("修改交往天数确认", msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.timeline.datingDays = (uint16_t)item.id;
                                nlpp::g_saveMgr.setTimelineData(curSlot, sd.timeline);
                                s_toastMessage = "实机陪伴天数修改成功!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 1) // 日历总天数
                    {
                        std::vector<PickerItem> opts = {
                            {100,  "100 日", ""},
                            {365,  "365 日 (1年)", ""},
                            {730,  "730 日 (2年)", ""},
                            {1095, "1095 日 (3年)", ""}
                        };
                        openPicker("选择日历累计总天数", opts, (int)slotData.timeline.timelineDays, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将日历总天数修改为 %d 日吗？", item.id);
                            requestConfirm("修改日历总天数确认", msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.timeline.timelineDays = (uint16_t)item.id;
                                nlpp::g_saveMgr.setTimelineData(curSlot, sd.timeline);
                                s_toastMessage = "日历总历天数修改成功!";
                                s_toastTimer = 180;
                            });
                        });
                    }
                    else if (s_selectedItem == 2) // 清除冷战
                    {
                        requestConfirm("清除冷战惩罚确认", "确定将离线天数重置为 0，彻底消除冷战惩罚吗？", [curSlot]() {
                            auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                            sd.timeline.maxAbsence = 0;
                            nlpp::g_saveMgr.setTimelineData(curSlot, sd.timeline);
                            s_toastMessage = "冷战惩罚已彻底消除!";
                            s_toastTimer = 180;
                        });
                    }
                    break;
                }

                // Page 6: 全量收集
                case 6:
                {
                    if (s_selectedItem == 0) // 513件全衣柜
                    {
                        requestConfirm("解锁全衣柜确认", "确定要全量解锁 513 件全套服装与配件吗？\n（包含严格边界校验，杜绝越界坏档风险）", [curSlot]() {
                            nlpp::g_saveMgr.unlockAllWardrobe513(curSlot);
                            s_toastMessage = "513件全衣柜已全量解锁!";
                            s_toastTimer = 200;
                        });
                    }
                    else if (s_selectedItem == 1) // 1317项全画廊
                    {
                        requestConfirm("解锁全主画廊确认", "确定要点亮全部 1,317 项主画廊梦境/回忆/特选/名场面吗？\n（严格完好保留真实旅行录像包）", []() {
                            nlpp::g_saveMgr.unlockMasterGallery();
                            s_toastMessage = "1,317 项全局主画廊全要素已全部点亮!";
                            s_toastTimer = 220;
                        });
                    }
                    else if (s_selectedItem == 2) // 清除NEW红点
                    {
                        requestConfirm("清除NEW红点确认", "确定批量清除所有画廊与图鉴条目的 NEW 提示红点吗？", [curSlot]() {
                            nlpp::g_saveMgr.unlockAllWardrobe513(curSlot);
                            s_toastMessage = "全画廊与图鉴 NEW 提示红点已全部清除!";
                            s_toastTimer = 180;
                        });
                    }
                    break;
                }
            }
        }
    }

    void nlppUpdate()
    {
        uint32_t down = ui::padKeysDown();

        // 1. If Dialog is active, route input exclusively to modal
        if (s_confirm.active)
        {
            updateConfirmDialog(down);
            return;
        }
        if (s_alert.active)
        {
            updateAlertDialog(down);
            return;
        }
        if (s_picker.active)
        {
            updatePickerDialog(down);
            return;
        }

        // 2. Circle Pad (摇杆) switches Top Screen Views / Edit Pages (Strictly No Touch)
        circlePosition cpad;
        hidCircleRead(&cpad);

        if (s_cpadNeutral)
        {
            if (cpad.dx > 80 || (down & (KEY_CPAD_RIGHT | KEY_CSTICK_RIGHT)))
            {
                if (s_appMode == MODE_MAIN_MENU)
                {
                    if (!s_slotSelectOpen)
                    {
                        s_mainView = (s_mainView + 1) % NUM_MAIN_VIEWS;
                        s_selectedItem = 0;
                    }
                }
                else
                {
                    s_editPage = (s_editPage + 1) % NUM_EDIT_PAGES;
                    s_selectedItem = 0;
                }
                s_cpadNeutral = false;
            }
            else if (cpad.dx < -80 || (down & (KEY_CPAD_LEFT | KEY_CSTICK_LEFT)))
            {
                if (s_appMode == MODE_MAIN_MENU)
                {
                    if (!s_slotSelectOpen)
                    {
                        s_mainView = (s_mainView + NUM_MAIN_VIEWS - 1) % NUM_MAIN_VIEWS;
                        s_selectedItem = 0;
                    }
                }
                else
                {
                    s_editPage = (s_editPage + NUM_EDIT_PAGES - 1) % NUM_EDIT_PAGES;
                    s_selectedItem = 0;
                }
                s_cpadNeutral = false;
            }
        }
        else
        {
            if (std::abs(cpad.dx) < 40)
            {
                s_cpadNeutral = true;
            }
        }

        // 3. Route Actions based on App Mode
        if (s_appMode == MODE_MAIN_MENU)
        {
            handleMode1Action(down);
        }
        else
        {
            handleMode2Action(down);
        }
    }
}
