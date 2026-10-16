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
    static int s_mainView = 0; // 0: 存档, 1: 备份, 2: 设置
    static bool s_slotSelectOpen = false; // When true on View 0, bottom screen shows slots
    static int s_slotIndex = 0;

    // Mode 2: 7 Edit Pages
    static const int NUM_EDIT_PAGES = 7;
    static int s_editPage = 0; // 0: 玩家信息, 1: 发型, 2: 呼称, 3: 彼氏力, 4: 女友, 5: 日程, 6: 收集

    static const char *s_editPageTitles[NUM_EDIT_PAGES] = {
        "玩家信息", "发型", "呼称", "彼氏力", "女友", "日程", "收集"
    };

    const std::string APP_TITLE = "NLPP Save Studio - v1.0.0";

    // Focus & Input
    static int s_selectedItem = 0;
    static bool s_cpadNeutral = true;

    // TYSS Pulsating Cyan-Blue Highlight Color State
    static uint8_t s_clrSh = 0x88;
    static bool s_clrAdd = true;

    static void updatePulseColor()
    {
        if (s_clrAdd)
        {
            s_clrSh += 6;
            if (s_clrSh >= 0xFA) s_clrAdd = false;
        }
        else
        {
            s_clrSh -= 3;
            if (s_clrSh <= 0x88) s_clrAdd = true;
        }
    }

    static inline u32 getPulseBoundingBoxColor()
    {
        return (0xFF << 24) | (0xC5 << 16) | (s_clrSh << 8) | 0x00;
    }

    // Secondary Confirmation Dialog state (strictly TYSS style)
    struct ConfirmState
    {
        bool active = false;
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
        std::string message;
    };
    static AlertState s_alert;

    // Value Picker Dialog state (using TYSS list style)
    struct PickerItem
    {
        int id;
        std::string label;
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

    static void requestConfirm(const std::string &message,
                               std::function<void()> onConfirm, std::function<void()> onCancel = nullptr)
    {
        s_confirm.active = true;
        s_confirm.message = message;
        s_confirm.onConfirm = onConfirm;
        s_confirm.onCancel = onCancel;
        s_confirm.selectedBtn = 0; // Default focus to "是"
    }

    static void showAlert(const std::string &message)
    {
        s_alert.active = true;
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
                s_picker.scroll = std::max(0, (int)i - 4);
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
    // Drawing Helpers (Strictly TYSS Menu & Card Styles)
    // =========================================================================

    static void drawCard(float x, float y, float w, float h, u32 bg = gfx::rectLt, u32 border = gfx::divClr)
    {
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, h, bg);
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, 1, border);
        C2D_DrawRectSolid(x, y + h - 1, GFX_DEPTH_DEFAULT, w, 1, border);
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, 1, h, border);
        C2D_DrawRectSolid(x + w - 1, y, GFX_DEPTH_DEFAULT, 1, h, border);
    }

    // TYSS standard menu list item drawer (18px height per item)
    static void drawMenuItem(int x, int y, int width, const std::string &text, bool selected)
    {
        if (selected)
        {
            uint32_t rectClr = getPulseBoundingBoxColor();
            gfx::drawBoundingBox(x, y - 2, width, 18, GFX_DEPTH_DEFAULT, rectClr);
            C2D_DrawRectSolid(x + 4, y + 1, GFX_DEPTH_DEFAULT, 2, 12, 0xFFC5FF00);
        }

        gfx::drawText(text, x + 8, y - 1, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
    }

    static void drawProgressBar(float x, float y, float w, float h, float ratio)
    {
        ratio = std::max(0.0f, std::min(1.0f, ratio));
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w, h, gfx::rectSh);
        if (ratio > 0.0f)
        {
            C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT, w * ratio, h, gfx::btnSel);
        }
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
        s_slotIndex = 0;
        s_editPage = 0;
        s_selectedItem = 0;
        s_cpadNeutral = true;
        s_confirm.active = false;
        s_alert.active = false;
        s_picker.active = false;

        // Auto-load on boot logic
        if (cfg::config["autoload"] == 1)
        {
            nlpp::g_saveMgr.detectSources();
            nlpp::SaveSource defaultSrc = (cfg::config["default_type"] == 1) ? nlpp::SOURCE_GAMECARD : nlpp::SOURCE_SD;

            bool srcAvailable = (defaultSrc == nlpp::SOURCE_GAMECARD) ? nlpp::g_saveMgr.hasGameCard() : nlpp::g_saveMgr.hasSD();

            if (!srcAvailable)
            {
                s_appMode = MODE_MAIN_MENU;
                s_mainView = 0;
                s_slotSelectOpen = false;
                showAlert("默认存档类型不可用，已自动回退到存档选择界面。");
            }
            else
            {
                nlpp::g_saveMgr.switchSource(defaultSrc);
                int defaultSlot = cfg::config["default_slot"];
                if (defaultSlot < 0 || defaultSlot >= 3) defaultSlot = 0;

                const auto &slotData = nlpp::g_saveMgr.getSlotData(defaultSlot);
                if (slotData.empty)
                {
                    s_appMode = MODE_MAIN_MENU;
                    s_mainView = 0;
                    s_slotSelectOpen = true;
                    s_slotIndex = defaultSlot;
                    showAlert("默认存档槽位为空存档，已自动回退到槽位选择界面。");
                }
                else
                {
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
            showAlert("存档保存成功！已写回介质并保留安全备份。");
        }
        else
        {
            showAlert("保存失败: " + statusMsg);
        }
    }

    // =========================================================================
    // Mode 1: Top Screen Drawing (3 Views)
    // =========================================================================

    static void drawMode1Top_SaveSelect()
    {
        ui::drawUIBar(APP_TITLE + " - 存档", ui::SCREEN_TOP, true);

        // TYSS standard menu list on Top Screen (x = 0, y = 22, width = 400)
        drawMenuItem(0, 24, 400, "数字版", (s_selectedItem == 0 && !s_slotSelectOpen));
        drawMenuItem(0, 42, 400, "实体版", (s_selectedItem == 1 && !s_slotSelectOpen));
    }

    static void drawMode1Top_Backup()
    {
        ui::drawUIBar(APP_TITLE + " - 备份", ui::SCREEN_TOP, true);

        auto bList = nlpp::g_saveMgr.listBackups();
        if (bList.empty())
        {
            gfx::drawText("暂无备份", 160, 110, GFX_DEPTH_DEFAULT, 0.5f, gfx::txtCont);
            return;
        }

        int visibleCount = 11;
        int scroll = std::max(0, s_selectedItem - 5);

        for (int i = 0; i < visibleCount; ++i)
        {
            int idx = scroll + i;
            if (idx >= (int)bList.size()) break;

            int iy = 24 + i * 18;
            bool sel = (idx == s_selectedItem);
            drawMenuItem(0, iy, 400, bList[idx].dirName, sel);
        }
    }

    static void drawMode1Top_Settings()
    {
        ui::drawUIBar(APP_TITLE + " - 设置", ui::SCREEN_TOP, true);

        std::string opts[8];
        opts[0] = "自动备份存档: " + std::string(cfg::config["autobackup"] ? "开" : "关");
        opts[1] = "导出到 ZIP: " + std::string(cfg::config["zip"] ? "开" : "关");

        int lvl = cfg::config["deflateLevel"];
        std::string lvlStr = (lvl == 1) ? "1 (最快速度)" : ((lvl == 6) ? "6 (标准压缩)" : ((lvl == 9) ? "9 (极限压缩)" : std::to_string(lvl)));
        opts[2] = "ZIP 压缩等级: " + lvlStr;

        opts[3] = "界面主题色: " + std::string(cfg::config["lightback"] ? "浅色系" : "深色系");

        int l = cfg::config["uilang"];
        std::string lStr = (l == 1) ? "English" : ((l == 2) ? "繁體中文" : "简体中文");
        opts[4] = "界面语言: " + lStr;

        opts[5] = "启动时加载存档: " + std::string(cfg::config["autoload"] ? "开" : "关");
        opts[6] = "默认存档类型: " + std::string(cfg::config["default_type"] == 1 ? "实体版" : "数字版");
        opts[7] = "默认存档槽位: 槽位 " + std::to_string(cfg::config["default_slot"] + 1);

        for (int i = 0; i < 8; ++i)
        {
            drawMenuItem(0, 24 + i * 18, 400, opts[i], (i == s_selectedItem));
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
        gfx::drawText("【玩家档案】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "玩家: %s %s", slotData.profile.lastName.c_str(), slotData.profile.firstName.c_str());
        gfx::drawText(buf, 14, 54, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "通信昵称(HN): %s", slotData.profile.hn.c_str());
        gfx::drawText(buf, 14, 80, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "生日: %d月%d日", slotData.profile.birthMonth, slotData.profile.birthDay);
        gfx::drawText(buf, 14, 106, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "血型: %s", nlpp::Dict::getBloodTypeName(slotData.profile.blood));
        gfx::drawText(buf, 14, 132, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "出身: %s", nlpp::Dict::getPrefectureName(slotData.profile.prefecture));
        gfx::drawText(buf, 14, 158, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        // Right Card: 槽位与恋爱状态
        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【槽位状态】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        const char *srcStr = (nlpp::g_saveMgr.getSource() == nlpp::SOURCE_GAMECARD) ? "实体版" : "数字版";
        snprintf(buf, sizeof(buf), "介质: %s", srcStr);
        gfx::drawText(buf, 208, 54, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "槽位: 槽位 %d", nlpp::g_saveMgr.getCurrentSlot() + 1);
        gfx::drawText(buf, 208, 80, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        const char *hName = "未结识";
        switch (slotData.girlfriend.heroine)
        {
            case nlpp::HEROINE_MANAKA: hName = "高嶺 愛花"; break;
            case nlpp::HEROINE_RINKO:  hName = "小早川 凛子"; break;
            case nlpp::HEROINE_NENE:   hName = "姉崎 寧々"; break;
            default: break;
        }
        snprintf(buf, sizeof(buf), "恋人: %s", hName);
        gfx::drawText(buf, 208, 106, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        uint32_t sel = slotData.stats.currentSelector;
        uint32_t rId = slotData.stats.readings[sel < 5 ? sel : 0];
        snprintf(buf, sizeof(buf), "呼称: 「%s」", nlpp::Dict::getReadingText(rId).c_str());
        gfx::drawText(buf, 208, 132, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "交往: %u 天", (unsigned)slotData.timeline.datingDays);
        gfx::drawText(buf, 208, 158, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
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
        u32 swatchClr = C2D_Color32(cdef.r, cdef.g, cdef.b, 255);
        C2D_DrawRectSolid(14, 172, GFX_DEPTH_DEFAULT, 176, 26, swatchClr);
        drawCard(14, 172, 176, 26, swatchClr, gfx::divClr);

        // Right Card: 攻略书发型图鉴预览位
        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【攻略本原画图鉴相框】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        drawCard(222, 54, 150, 110, gfx::rectSh, gfx::divClr);
        snprintf(buf, sizeof(buf), "图鉴编号: #%02d", slotData.girlfriend.hairStyleId);
        gfx::drawText(buf, 245, 66, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);

        gfx::drawText(stName, 240, 92, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        gfx::drawText("【攻略本插画预览区】", 232, 122, GFX_DEPTH_DEFAULT, 0.38f, gfx::txtCont);
        gfx::drawText("后续直接载入高清插图", 230, 140, GFX_DEPTH_DEFAULT, 0.34f, gfx::txtCont);
    }

    static void drawMode2Top_Readings(const nlpp::SlotData &slotData)
    {
        char buf[128];

        drawCard(8, 22, 384, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 384, 22, gfx::rectSh);
        gfx::drawText("【呼称称呼与读音】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        static const char *slotLabels[] = {
            "槽0 (姓氏)", "槽1 (名字)", "槽2 (后缀)", "槽3 (预设)", "槽4 (爱称)"
        };

        uint32_t sel = slotData.stats.currentSelector;
        snprintf(buf, sizeof(buf), "当前生效: %s", slotLabels[sel < 5 ? sel : 0]);
        gfx::drawText(buf, 18, 54, GFX_DEPTH_DEFAULT, 0.46f, gfx::txtCont);

        for (int r = 0; r < 5; ++r)
        {
            uint32_t rid = slotData.stats.readings[r];
            std::string rtxt = nlpp::Dict::getReadingText(rid);
            snprintf(buf, sizeof(buf), "%s: 「%s」", slotLabels[r], rtxt.c_str());
            gfx::drawText(buf, 18, 82 + r * 22, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        }
    }

    static void drawMode2Top_Stats(const nlpp::SlotData &slotData)
    {
        char buf[128];

        drawCard(8, 22, 190, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【四维属性】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "运动: %3d / 255", slotData.stats.fitness);
        gfx::drawText(buf, 14, 50, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 68, 176, 8, slotData.stats.fitness / 255.0f);

        snprintf(buf, sizeof(buf), "知识: %3d / 255", slotData.stats.knowledge);
        gfx::drawText(buf, 14, 86, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 104, 176, 8, slotData.stats.knowledge / 255.0f);

        snprintf(buf, sizeof(buf), "感性: %3d / 255", slotData.stats.sensibility);
        gfx::drawText(buf, 14, 122, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 140, 176, 8, slotData.stats.sensibility / 255.0f);

        snprintf(buf, sizeof(buf), "魅力: %3d / 255", slotData.stats.charm);
        gfx::drawText(buf, 14, 158, GFX_DEPTH_DEFAULT, 0.40f, gfx::txtCont);
        drawProgressBar(14, 176, 176, 8, slotData.stats.charm / 255.0f);

        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【资产与状态】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "金钱: %u G", (unsigned)slotData.stats.rich);
        gfx::drawText(buf, 208, 54, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "TODO: %u 点", (unsigned)slotData.stats.todo);
        gfx::drawText(buf, 208, 82, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);

        if (slotData.stats.cameraFilter == -1 || !(slotData.stats.equipMask & 0x04))
            gfx::drawText("滤镜: [无滤镜]", 208, 114, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
        else
            gfx::drawText("滤镜: [佩戴中]", 208, 114, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
    }

    static void drawMode2Top_Girlfriend(const nlpp::SlotData &slotData)
    {
        char buf[128];

        drawCard(8, 22, 190, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【恋人羁绊】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        const char *hName = (slotData.girlfriend.heroine == nlpp::HEROINE_MANAKA) ? "高嶺 愛花" :
                            ((slotData.girlfriend.heroine == nlpp::HEROINE_RINKO) ? "小早川 凛子" : "姉崎 寧々");
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

        drawCard(202, 22, 190, 212);
        C2D_DrawRectSolid(202, 22, GFX_DEPTH_DEFAULT, 190, 22, gfx::rectSh);
        gfx::drawText("【性格坐标】", 208, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "横轴 X: %d / 1000", slotData.girlfriend.sliderX);
        gfx::drawText(buf, 208, 52, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "纵轴 Y: %d / 1000", slotData.girlfriend.sliderY);
        gfx::drawText(buf, 208, 76, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "形态 Z: %d / 4", slotData.girlfriend.sliderZ);
        gfx::drawText(buf, 208, 100, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);

        // Miniature 2D coordinate box
        float cx = 250, cy = 135, csize = 56;
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
        gfx::drawText("【日程与纪念天数】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "实机陪伴交往天数: %u 天", (unsigned)slotData.timeline.datingDays);
        gfx::drawText(buf, 20, 60, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "日历累计总天数: %u 日", (unsigned)slotData.timeline.timelineDays);
        gfx::drawText(buf, 20, 100, GFX_DEPTH_DEFAULT, 0.48f, gfx::txtCont);

        snprintf(buf, sizeof(buf), "最大离线天数: %u 日", slotData.timeline.maxAbsence);
        gfx::drawText(buf, 20, 140, GFX_DEPTH_DEFAULT, 0.44f, gfx::txtCont);
    }

    static void drawMode2Top_Collection()
    {
        drawCard(8, 22, 384, 212);
        C2D_DrawRectSolid(8, 22, GFX_DEPTH_DEFAULT, 384, 22, gfx::rectSh);
        gfx::drawText("【收集进度】", 14, 25, GFX_DEPTH_DEFAULT, 0.45f, gfx::txtCont);

        gfx::drawText("全套衣柜收集: 513 / 513 件", 20, 60, GFX_DEPTH_DEFAULT, 0.46f, gfx::txtCont);
        gfx::drawText("全局主画廊收集: 1,317 / 1,317 项", 20, 100, GFX_DEPTH_DEFAULT, 0.46f, gfx::txtCont);
        gfx::drawText("真实旅行录像包: 完好保留", 20, 140, GFX_DEPTH_DEFAULT, 0.42f, gfx::txtCont);
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
            // Title strictly format: "NLPP Save Studio - v1.0.0 - (分页名)"
            std::string editTitle = APP_TITLE + " - " + s_editPageTitles[s_editPage];
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

        if (s_confirm.active || s_alert.active || s_picker.active)
        {
            C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.9f, 400, 240, 0xBB000000);
        }
    }

    // =========================================================================
    // Dialog Drawing (Strictly TYSS Style)
    // =========================================================================

    static void drawDialogBtn(int x, int y, int w, int h, const std::string &text, bool selected)
    {
        C2D_DrawRectSolid(x, y, GFX_DEPTH_DEFAULT + 0.18f, w, h, selected ? gfx::btnSel : gfx::btnClr);
        unsigned tw = gfx::getTextWidth(text);
        unsigned th = 14;
        unsigned tx = x + (w / 2) - (tw / 2);
        unsigned ty = y + (h / 2) - (th / 2);
        gfx::drawText(text, tx, ty, GFX_DEPTH_DEFAULT + 0.2f, 0.5f, gfx::txtCont);
    }

    static void drawConfirmDialog()
    {
        // Darken backdrop (0xBB000000 strictly matching TYSS thrdMgr)
        C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.1f, 320, 240, 0xBB000000);

        // Window matching TYSS confirmDrawFunc: x=24, y=24, w=272, h=200
        C2D_DrawRectSolid(24, 24, GFX_DEPTH_DEFAULT + 0.15f, 272, 200, gfx::clearClr);

        // Wrapped message text
        gfx::drawTextWrap(s_confirm.message, 32, 32, GFX_DEPTH_DEFAULT + 0.2f, 0.5f, 256, gfx::txtCont);

        // Buttons matching TYSS: Yes (32, 184, 120, 32) and No (168, 184, 120, 32)
        // Strictly NO blue bounding box and NO accent bar, only solid color btnSel vs btnClr
        drawDialogBtn(32, 184, 120, 32, "是 \ue000", s_confirm.selectedBtn == 0);
        drawDialogBtn(168, 184, 120, 32, "否 \ue001", s_confirm.selectedBtn == 1);
    }

    static void drawAlertDialog()
    {
        // Darken backdrop (0xBB000000 strictly matching TYSS thrdMgr)
        C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.1f, 320, 240, 0xBB000000);

        // Window matching TYSS messageDrawFunc: x=24, y=24, w=272, h=200
        C2D_DrawRectSolid(24, 24, GFX_DEPTH_DEFAULT + 0.15f, 272, 200, gfx::clearClr);

        // Message text
        gfx::drawTextWrap(s_alert.message, 32, 32, GFX_DEPTH_DEFAULT + 0.2f, 0.5f, 256, gfx::txtCont);

        // OK button: (96, 184, 128, 32) - Strictly NO bounding box
        drawDialogBtn(96, 184, 128, 32, "确定 \ue000", true);
    }

    static void drawPickerDialog()
    {
        C2D_DrawRectSolid(0, 0, GFX_DEPTH_DEFAULT + 0.1f, 320, 240, 0xBB000000);

        // Window: x=24, y=24, w=272, h=200
        C2D_DrawRectSolid(24, 24, GFX_DEPTH_DEFAULT + 0.15f, 272, 200, gfx::clearClr);

        // Header Title
        gfx::drawText(s_picker.title, 32, 28, GFX_DEPTH_DEFAULT + 0.2f, 0.5f, gfx::txtCont);

        int visibleCount = 8;
        float itemY = 50.0f;

        for (int i = 0; i < visibleCount; ++i)
        {
            int idx = s_picker.scroll + i;
            if (idx >= (int)s_picker.items.size()) break;

            const auto &item = s_picker.items[idx];
            bool sel = (idx == s_picker.selected);
            int iy = itemY + i * 18;

            if (sel)
            {
                uint32_t rectClr = getPulseBoundingBoxColor();
                gfx::drawBoundingBox(28, iy - 2, 264, 18, GFX_DEPTH_DEFAULT + 0.18f, rectClr);
                C2D_DrawRectSolid(32, iy + 1, GFX_DEPTH_DEFAULT + 0.2f, 2, 12, 0xFFC5FF00);
            }

            int textOffset = 36;
            if (item.hasColor)
            {
                C2D_DrawRectSolid(textOffset, iy + 2, GFX_DEPTH_DEFAULT + 0.2f, 12, 12, item.colorRgb);
                textOffset += 16;
            }

            gfx::drawText(item.label, textOffset, iy - 1, GFX_DEPTH_DEFAULT + 0.2f, 0.5f, gfx::txtCont);
        }
    }

    // =========================================================================
    // Mode 1: Bottom Screen Drawing
    // =========================================================================

    static void drawMode1Bot_SaveSelect()
    {
        if (!s_slotSelectOpen)
        {
            // Bottom screen is clean, no extra redundant text
            ui::drawUIBar(getTxt("\ue000 选择 \ue01A\ue077\ue019 切换视图"), ui::SCREEN_BOT, true);
        }
        else
        {
            // In slot selection on bottom screen (using TYSS list style at x = 0, y = 2, width = 320)
            for (int s = 0; s < 3; ++s)
            {
                const auto &sd = nlpp::g_saveMgr.getSlotData(s);
                std::string sText;
                if (sd.empty)
                {
                    sText = "槽位 " + std::to_string(s + 1) + ": (空)";
                }
                else
                {
                    std::string playerName;
                    if (!sd.profile.lastName.empty() && !sd.profile.firstName.empty())
                    {
                        playerName = sd.profile.lastName + " " + sd.profile.firstName;
                    }
                    else if (!sd.profile.lastName.empty())
                    {
                        playerName = sd.profile.lastName;
                    }
                    else if (!sd.profile.firstName.empty())
                    {
                        playerName = sd.profile.firstName;
                    }
                    else
                    {
                        playerName = "无名氏";
                    }
                    sText = "槽位 " + std::to_string(s + 1) + ": " + playerName;
                }

                drawMenuItem(0, 4 + s * 18, 320, sText, (s == s_selectedItem));
            }

            ui::drawUIBar(getTxt("\ue000 选择 \ue002 复制 \ue003 备份 \ue001 关闭"), ui::SCREEN_BOT, true);
        }
    }

    static void drawMode1Bot_Backup()
    {
        ui::drawUIBar(getTxt("\ue002 删除 \ue003 恢复 \ue01A\ue077\ue019 切换视图"), ui::SCREEN_BOT, true);
    }

    static void drawMode1Bot_Settings()
    {
        static const char *descs[] = {
            "每次保存时自动在 SD 卡创建备份。",
            "备份存档时是否使用 ZIP 压缩存储。",
            "选择 ZIP 压缩等级 (1~9)。",
            "选择 UI 界面主题色 (浅色系/深色系)。",
            "选择 UI 界面显示语言。",
            "启动时跳过菜单直接载入默认存档。",
            "设置开机直接载入的存档类型。",
            "设置开机直接载入的存档槽位。"
        };

        int idx = (s_selectedItem >= 0 && s_selectedItem < 8) ? s_selectedItem : 0;
        gfx::drawTextWrap(descs[idx], 8, 8, GFX_DEPTH_DEFAULT, 0.5f, 304, gfx::txtCont);

        ui::drawUIBar(getTxt("\ue000 选择/切换 \ue01A\ue077\ue019 切换视图"), ui::SCREEN_BOT, true);
    }

    // =========================================================================
    // Mode 2: Bottom Screen Drawing (7 Edit Pages using TYSS Menu List Style)
    // =========================================================================

    static void drawMode2Bot_Edit()
    {
        int curSlot = nlpp::g_saveMgr.getCurrentSlot();
        auto &slotData = nlpp::g_saveMgr.getSlotData(curSlot);

        std::vector<std::string> items;

        switch (s_editPage)
        {
            // Page 0: 玩家信息
            case 0:
            {
                items.push_back("玩家姓氏: " + slotData.profile.lastName);
                items.push_back("玩家名字: " + slotData.profile.firstName);
                items.push_back("通信昵称(HN): " + slotData.profile.hn);
                items.push_back("出生月份: " + std::to_string(slotData.profile.birthMonth) + "月");
                items.push_back("出生日期: " + std::to_string(slotData.profile.birthDay) + "日");
                items.push_back("玩家血型: " + std::string(nlpp::Dict::getBloodTypeName(slotData.profile.blood)));
                items.push_back("出身地域: " + std::string(nlpp::Dict::getPrefectureName(slotData.profile.prefecture)));
                items.push_back("★ 保存写入存档");
                break;
            }

            // Page 1: 发型
            case 1:
            {
                const char *stName = nlpp::Dict::getStyleName(slotData.girlfriend.hairStyleId);
                items.push_back("修改发型款式: " + std::string(stName));
                const char *lenName = nlpp::Dict::getHairLengthName(slotData.girlfriend.heroine, slotData.girlfriend.hairLength);
                items.push_back("修改头发长度: " + std::string(lenName));
                const auto &cdef = nlpp::Dict::getHairColor(slotData.girlfriend.hairColorId);
                items.push_back("修改发色方案: " + std::string(cdef.category) + " - " + cdef.tone);
                items.push_back("重置为初始发型与发色");
                items.push_back("解除发型限制");
                items.push_back("★ 保存写入存档");
                break;
            }

            // Page 2: 呼称
            case 2:
            {
                static const char *s_slotNames[] = {"槽0 (姓氏)", "槽1 (名字)", "槽2 (后缀)", "槽3 (预设)", "槽4 (爱称)"};
                uint32_t sel = slotData.stats.currentSelector;
                items.push_back("当前生效呼称槽: " + std::string(s_slotNames[sel < 5 ? sel : 0]));

                for (int r = 0; r < 5; ++r)
                {
                    uint32_t rid = slotData.stats.readings[r];
                    std::string rtxt = nlpp::Dict::getReadingText(rid);
                    items.push_back("修改" + std::string(s_slotNames[r]) + "读音: " + rtxt);
                }
                items.push_back("★ 保存写入存档");
                break;
            }

            // Page 3: 彼氏力
            case 3:
            {
                items.push_back("修改运动能力: " + std::to_string(slotData.stats.fitness));
                items.push_back("修改知识学力: " + std::to_string(slotData.stats.knowledge));
                items.push_back("修改感性艺术: " + std::to_string(slotData.stats.sensibility));
                items.push_back("修改魅力交际: " + std::to_string(slotData.stats.charm));
                items.push_back("修改所持金钱: " + std::to_string(slotData.stats.rich) + " G");
                items.push_back("修改TODO点数: " + std::to_string(slotData.stats.todo) + " 点");
                items.push_back("彻底卸除相机滤镜");
                items.push_back("一键彼氏力全要素全满");
                items.push_back("★ 保存写入存档");
                break;
            }

            // Page 4: 女友
            case 4:
            {
                items.push_back("女友好感度升满: 255");
                items.push_back("调整性格横轴 X: " + std::to_string(slotData.girlfriend.sliderX));
                items.push_back("调整性格纵轴 Y: " + std::to_string(slotData.girlfriend.sliderY));
                items.push_back("切换性格形态 Z: " + std::to_string(slotData.girlfriend.sliderZ));
                const auto &title = nlpp::Dict::getCoupleTitleByExp(slotData.girlfriend.coupleExp);
                items.push_back("修改情侣称号: " + std::string(title.name_jp));
                items.push_back("一键直达满级称号");
                items.push_back("★ 保存写入存档");
                break;
            }

            // Page 5: 日程
            case 5:
            {
                items.push_back("修改实机陪伴天数: " + std::to_string(slotData.timeline.datingDays) + " 天");
                items.push_back("修改日历总天数: " + std::to_string(slotData.timeline.timelineDays) + " 日");
                items.push_back("清除离线天数 (消除冷战惩罚)");
                items.push_back("★ 保存写入存档");
                break;
            }

            // Page 6: 收集
            case 6:
            {
                items.push_back("一键全量解锁 513 件全套衣柜");
                items.push_back("一键全要素点亮 1,317 项全局主画廊");
                items.push_back("一键批量清除全图鉴 NEW 红点");
                items.push_back("★ 保存写入存档");
                break;
            }
        }

        // Draw items using TYSS menu list style at x = 0, y = 4, width = 320
        for (size_t i = 0; i < items.size(); ++i)
        {
            drawMenuItem(0, 4 + (int)i * 18, 320, items[i], (int)i == s_selectedItem);
        }

        ui::drawUIBar(getTxt("\ue000 选择 \ue001 退出 \ue01A\ue077\ue019 切换视图"), ui::SCREEN_BOT, true);
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

        // Draw Dialog Overlays if active (TYSS Dialogs)
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
            if (s_picker.selected >= s_picker.scroll + 8) s_picker.scroll = s_picker.selected - 7;
        }
        if (down & KEY_DDOWN)
        {
            s_picker.selected = (s_picker.selected + 1) % count;
            if (s_picker.selected >= s_picker.scroll + 8) s_picker.scroll = s_picker.selected - 7;
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
        // View 0: 存档
        if (s_mainView == 0)
        {
            if (!s_slotSelectOpen)
            {
                // Top screen menu: 0: 数字版, 1: 实体版
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
                            "未检测到数字版存档！" :
                            "未插入游戏卡带或未检测到卡带存档！";
                        showAlert(failMsg);
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
                // Slot select on bottom screen: 3 slots
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
                    const auto &srcSlotData = nlpp::g_saveMgr.getSlotData(srcSlot);
                    if (srcSlotData.empty)
                    {
                        showAlert("该槽位为空存档，无法进行克隆！");
                        return;
                    }
                    std::vector<PickerItem> opts;
                    for (int s = 0; s < 3; ++s)
                    {
                        if (s == srcSlot) continue;
                        opts.push_back({s, "覆盖至 槽位 " + std::to_string(s + 1)});
                    }
                    openPicker("选择克隆目标槽位", opts, (srcSlot + 1) % 3, [srcSlot](const PickerItem &item) {
                        char msg[160];
                        snprintf(msg, sizeof(msg), "你确定要将槽位 %d 的数据克隆到槽位 %d 吗？\n目标槽位原有数据将被覆盖！", srcSlot + 1, item.id + 1);
                        requestConfirm(msg, [srcSlot, item]() {
                            nlpp::g_saveMgr.cloneSlot(srcSlot, item.id);
                            showAlert("槽位数据克隆成功！");
                        });
                    });
                    return;
                }

                if (down & KEY_Y) // 立即备份
                {
                    requestConfirm("确定要备份此存档吗？", []() {
                        std::string outPath;
                        nlpp::g_saveMgr.createAutoBackup(&outPath);
                        showAlert("备份成功！已保存至 sdmc:/NLPP_BACKUP/");
                    });
                    return;
                }

                if (down & KEY_A)
                {
                    const auto &slotData = nlpp::g_saveMgr.getSlotData(s_selectedItem);
                    if (slotData.empty)
                    {
                        showAlert("该槽位为空存档，无法进入编辑！");
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
        // View 1: 备份
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
                    std::string q = "你确定要删除备份文件 " + dirName + " 吗？\n该操作无法撤销！";
                    requestConfirm(q, [dirName]() {
                        std::string path = "sdmc:/NLPP_BACKUP/" + dirName;
                        fs::delDirRec(fs::getSDMCArch(), util::toUtf16(path));
                        showAlert("备份已删除！");
                    });
                }
                else if (down & KEY_Y) // 恢复
                {
                    std::string dirName = bList[s_selectedItem].dirName;
                    std::string q = "你确定要恢复备份文件 " + dirName + " 吗？\n当前存档数据将被覆盖！";
                    requestConfirm(q, [dirName]() {
                        nlpp::g_saveMgr.restoreBackup(dirName);
                        showAlert("存档数据恢复成功！");
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
                    case 0:
                        cfg::config["autobackup"] = 1 - cfg::config["autobackup"];
                        cfg::saveCommon();
                        break;
                    case 1:
                        cfg::config["zip"] = 1 - cfg::config["zip"];
                        cfg::saveCommon();
                        break;
                    case 2:
                    {
                        int lvl = cfg::config["deflateLevel"];
                        lvl = (lvl % 9) + 1;
                        cfg::config["deflateLevel"] = lvl;
                        cfg::saveCommon();
                        break;
                    }
                    case 3:
                    {
                        bool curLight = (cfg::config["lightback"] != 0);
                        cfg::config["lightback"] = curLight ? 0 : 1;
                        gfx::setColor(cfg::config["lightback"]);
                        cfg::saveCommon();
                        break;
                    }
                    case 4:
                    {
                        int l = (cfg::config["uilang"] + 1) % 3;
                        cfg::config["uilang"] = l;
                        cfg::setUILanguage(l);
                        cfg::saveCommon();
                        break;
                    }
                    case 5:
                        cfg::config["autoload"] = 1 - cfg::config["autoload"];
                        cfg::saveCommon();
                        break;
                    case 6:
                        cfg::config["default_type"] = 1 - cfg::config["default_type"];
                        cfg::saveCommon();
                        break;
                    case 7:
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

        int maxItems = 8;
        if (s_editPage == 0) maxItems = 8;
        else if (s_editPage == 1) maxItems = 6;
        else if (s_editPage == 2) maxItems = 7;
        else if (s_editPage == 3) maxItems = 9;
        else if (s_editPage == 4) maxItems = 7;
        else if (s_editPage == 5) maxItems = 4;
        else if (s_editPage == 6) maxItems = 4;

        if (down & KEY_DUP) s_selectedItem = (s_selectedItem + maxItems - 1) % maxItems;
        if (down & KEY_DDOWN) s_selectedItem = (s_selectedItem + 1) % maxItems;

        // B key exits Edit Mode back to Slot Select
        if (down & KEY_B)
        {
            requestConfirm("确定退出存档编辑模式吗？\n未保存写回的修改将会丢失。", []() {
                s_appMode = MODE_MAIN_MENU;
                s_mainView = 0;
                s_slotSelectOpen = true;
                s_selectedItem = nlpp::g_saveMgr.getCurrentSlot();
            });
            return;
        }

        // Check if user pressed A on the last button: ★ 保存写入存档
        if (down & KEY_A && s_selectedItem == maxItems - 1)
        {
            requestConfirm("确定将当前所有修改提交写回至 3DS 介质吗？\n（写回前将在 SD 卡自动生成安全备份）", []() {
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
                            requestConfirm("确定将玩家姓氏修改为「" + input + "」吗？", [curSlot, input]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.lastName = input;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        }
                    }
                    else if (s_selectedItem == 1) // 名字
                    {
                        std::string input = inputStringSwkbd("请输入玩家名字 (支持汉字/假名)", slotData.profile.firstName, 8);
                        if (!input.empty() && input != slotData.profile.firstName)
                        {
                            requestConfirm("确定将玩家名字修改为「" + input + "」吗？", [curSlot, input]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.firstName = input;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        }
                    }
                    else if (s_selectedItem == 2) // HN
                    {
                        std::string input = inputStringSwkbd("请输入通信昵称 (HN)", slotData.profile.hn, 8);
                        if (!input.empty() && input != slotData.profile.hn)
                        {
                            requestConfirm("确定将通信昵称(HN)修改为「" + input + "」吗？", [curSlot, input]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.hn = input;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        }
                    }
                    else if (s_selectedItem == 3) // 月份
                    {
                        std::vector<PickerItem> opts;
                        for (int m = 1; m <= 12; ++m) opts.push_back({m, std::to_string(m) + "月"});
                        openPicker("选择出生月份", opts, slotData.profile.birthMonth, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将出生月份修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.birthMonth = (uint8_t)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        });
                    }
                    else if (s_selectedItem == 4) // 日期
                    {
                        std::vector<PickerItem> opts;
                        for (int d = 1; d <= 31; ++d) opts.push_back({d, std::to_string(d) + "日"});
                        openPicker("选择出生日期", opts, slotData.profile.birthDay, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将出生日期修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.birthDay = (uint8_t)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        });
                    }
                    else if (s_selectedItem == 5) // 血型
                    {
                        std::vector<PickerItem> opts = {
                            {0, "A型"}, {1, "B型"}, {2, "AB型"}, {3, "O型"}
                        };
                        openPicker("选择玩家血型", opts, (int)slotData.profile.blood, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将玩家血型修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.blood = (nlpp::BloodType)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        });
                    }
                    else if (s_selectedItem == 6) // 出身地
                    {
                        std::vector<PickerItem> opts;
                        int count = nlpp::Dict::getPrefectureCount();
                        for (int p = 0; p < count; ++p)
                            opts.push_back({p, nlpp::Dict::getPrefectureName(p)});
                        openPicker("选择出身地域", opts, slotData.profile.prefecture, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将出身地修改为「" + item.label + "」吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.profile.prefecture = (uint16_t)item.id;
                                nlpp::g_saveMgr.setProfile(curSlot, sd.profile);
                            });
                        });
                    }
                    break;
                }

                // Page 1: 发型
                case 1:
                {
                    if (s_selectedItem == 0) // 发型款式
                    {
                        auto styles = nlpp::Dict::getAvailableStyles(slotData.girlfriend.heroine, slotData.girlfriend.hairLength);
                        std::vector<PickerItem> opts;
                        if (!styles.empty())
                        {
                            for (const auto &st : styles)
                                opts.push_back({st.style_id, st.name});
                        }
                        else
                        {
                            for (int s = 0; s < 24; ++s)
                                opts.push_back({s, nlpp::Dict::getStyleName(s)});
                        }
                        openPicker("选择发型款式", opts, slotData.girlfriend.hairStyleId, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将发型款式修改为「%s」吗？", item.label.c_str());
                            requestConfirm(msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.hairStyleId = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            });
                        });
                    }
                    else if (s_selectedItem == 1) // 头发长度
                    {
                        auto lengths = nlpp::Dict::getValidLengthStages(slotData.girlfriend.heroine);
                        std::vector<PickerItem> opts;
                        for (uint8_t l : lengths)
                            opts.push_back({l, nlpp::Dict::getHairLengthName(slotData.girlfriend.heroine, l)});
                        openPicker("选择头发长度", opts, slotData.girlfriend.hairLength, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将头发长度调整为「%s」吗？", item.label.c_str());
                            requestConfirm(msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.hairLength = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
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
                            pi.colorRgb = rgb;
                            pi.hasColor = true;
                            opts.push_back(pi);
                        }
                        openPicker("选择发色方案", opts, slotData.girlfriend.hairColorId, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将发色修改为「%s」吗？", item.label.c_str());
                            requestConfirm(msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.hairColorId = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            });
                        });
                    }
                    else if (s_selectedItem == 3) // 重置形象
                    {
                        requestConfirm("确定将发型、发长和发色重置为初始默认形象吗？", [curSlot]() {
                            auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                            sd.girlfriend.hairStyleId = 0;
                            sd.girlfriend.hairColorId = 0;
                            if (sd.girlfriend.heroine == nlpp::HEROINE_MANAKA) sd.girlfriend.hairLength = 2;
                            else if (sd.girlfriend.heroine == nlpp::HEROINE_RINKO) sd.girlfriend.hairLength = 1;
                            else if (sd.girlfriend.heroine == nlpp::HEROINE_NENE) sd.girlfriend.hairLength = 2;
                            nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                        });
                    }
                    else if (s_selectedItem == 4) // 解除限制
                    {
                        requestConfirm("确定解除发型限制，允许自由尝试全部发型吗？", [curSlot]() {
                            auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                            sd.girlfriend.reservedStyleId = 0xFF;
                            sd.girlfriend.hairTasteTarget = 0;
                            nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                        });
                    }
                    break;
                }

                // Page 2: 呼称
                case 2:
                {
                    if (s_selectedItem == 0) // 生效呼称槽
                    {
                        std::vector<PickerItem> opts = {
                            {0, "槽0 (姓氏)"},
                            {1, "槽1 (名字)"},
                            {2, "槽2 (后缀)"},
                            {3, "槽3 (预设)"},
                            {4, "槽4 (爱称)"}
                        };
                        openPicker("选择当前生效呼称槽", opts, slotData.stats.currentSelector, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将生效呼称槽切换为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.stats.currentSelector = (uint32_t)item.id;
                                nlpp::g_saveMgr.setBoyfriendStats(curSlot, sd.stats);
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
                            opts.push_back({(int)e->id, e->text});

                        openPicker("选择呼称读音", opts, slotData.stats.readings[rslot], [curSlot, rslot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将槽%d呼称修改为「%s」吗？", rslot, item.label.c_str());
                            requestConfirm(msg, [curSlot, rslot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.stats.readings[rslot] = (uint32_t)item.id;
                                nlpp::g_saveMgr.setBoyfriendStats(curSlot, sd.stats);
                            });
                        });
                    }
                    break;
                }

                // Page 3: 彼氏力
                case 3:
                {
                    if (s_selectedItem >= 0 && s_selectedItem <= 3)
                    {
                        static const char *statNames[] = {"运动能力", "知识学力", "感性艺术", "魅力交际"};
                        std::vector<PickerItem> opts = {
                            {255, "255 (满值)"},
                            {200, "200"},
                            {150, "150"},
                            {100, "100"},
                            {50,  "50"}
                        };
                        int itemIdx = s_selectedItem;
                        openPicker(std::string("调节") + statNames[itemIdx], opts, 255, [curSlot, itemIdx](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将%s修改为 %d 吗？", statNames[itemIdx], item.id);
                            requestConfirm(msg, [curSlot, itemIdx, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                if (itemIdx == 0) sd.stats.fitness = (uint8_t)item.id;
                                else if (itemIdx == 1) sd.stats.knowledge = (uint8_t)item.id;
                                else if (itemIdx == 2) sd.stats.sensibility = (uint8_t)item.id;
                                else if (itemIdx == 3) sd.stats.charm = (uint8_t)item.id;
                                nlpp::g_saveMgr.setBoyfriendStats(curSlot, sd.stats);
                            });
                        });
                    }
                    else if (s_selectedItem == 4) // 金钱
                    {
                        requestConfirm("确定将所持金钱修改为 999,999 G 吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxRichMoney(curSlot);
                        });
                    }
                    else if (s_selectedItem == 5) // TODO点数
                    {
                        requestConfirm("确定将 TODO 点数修改为 9,999 点吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxTodoPoints(curSlot);
                        });
                    }
                    else if (s_selectedItem == 6) // 卸除滤镜
                    {
                        requestConfirm("确定彻底卸除相机滤镜吗？", [curSlot]() {
                            nlpp::g_saveMgr.removeCameraFilter(curSlot);
                        });
                    }
                    else if (s_selectedItem == 7) // 一键全满
                    {
                        requestConfirm("确定将彼氏力四维、金钱、TODO全部升满并卸除滤镜吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxBoyfriendStats(curSlot);
                            nlpp::g_saveMgr.maxRichMoney(curSlot);
                            nlpp::g_saveMgr.maxTodoPoints(curSlot);
                            nlpp::g_saveMgr.removeCameraFilter(curSlot);
                        });
                    }
                    break;
                }

                // Page 4: 女友
                case 4:
                {
                    if (s_selectedItem == 0) // 好感度
                    {
                        requestConfirm("确定将女友好感度升满至 255 吗？", [curSlot]() {
                            nlpp::g_saveMgr.maxGirlfriendAffection(curSlot);
                        });
                    }
                    else if (s_selectedItem == 1) // 性格 X
                    {
                        std::vector<PickerItem> opts = {
                            {100, "100 (极文静)"},
                            {300, "300"},
                            {500, "500 (平衡)"},
                            {700, "700"},
                            {900, "900 (极活泼)"}
                        };
                        openPicker("选择性格横轴 X", opts, slotData.girlfriend.sliderX, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将性格横轴 X 修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.sliderX = item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            });
                        });
                    }
                    else if (s_selectedItem == 2) // 性格 Y
                    {
                        std::vector<PickerItem> opts = {
                            {100, "100 (极理性)"},
                            {300, "300"},
                            {500, "500 (平衡)"},
                            {700, "700"},
                            {900, "900 (极感性)"}
                        };
                        openPicker("选择性格纵轴 Y", opts, slotData.girlfriend.sliderY, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将性格纵轴 Y 修改为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.sliderY = item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            });
                        });
                    }
                    else if (s_selectedItem == 3) // 性格 Z
                    {
                        std::vector<PickerItem> opts = {
                            {0, "形态 0 (默认)"},
                            {1, "形态 1"},
                            {2, "形态 2"},
                            {3, "形态 3"}
                        };
                        openPicker("选择性格形态 Z", opts, slotData.girlfriend.sliderZ, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将性格形态 Z 切换为 " + item.label + " 吗？", [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.girlfriend.sliderZ = (uint8_t)item.id;
                                nlpp::g_saveMgr.setGirlfriendStatus(curSlot, sd.girlfriend);
                            });
                        });
                    }
                    else if (s_selectedItem == 4) // 情侣称号
                    {
                        std::vector<PickerItem> opts;
                        for (int r = 1; r <= 23; ++r)
                        {
                            const auto &t = nlpp::Dict::getCoupleTitle(r);
                            opts.push_back({r, "Lv." + std::to_string(t.rank) + " " + t.name_jp});
                        }
                        const auto &curT = nlpp::Dict::getCoupleTitleByExp(slotData.girlfriend.coupleExp);
                        openPicker("选择情侣称号等级", opts, curT.rank, [curSlot](const PickerItem &item) {
                            requestConfirm("确定将情侣称号修改为「" + item.label + "」吗？", [curSlot, item]() {
                                nlpp::g_saveMgr.setCoupleTitleRank(curSlot, (uint8_t)item.id);
                            });
                        });
                    }
                    else if (s_selectedItem == 5) // 一键满级
                    {
                        requestConfirm("确定直达满级情侣称号 Lv.23 并获得 23 枚黄金勋章吗？", [curSlot]() {
                            nlpp::g_saveMgr.setCoupleTitleRank(curSlot, 23);
                        });
                    }
                    break;
                }

                // Page 5: 日程
                case 5:
                {
                    if (s_selectedItem == 0) // 实机陪伴天数
                    {
                        std::vector<PickerItem> opts = {
                            {30,   "30 天 (满月纪念)"},
                            {100,  "100 天 (百日纪念)"},
                            {365,  "365 天 (一周年)"},
                            {730,  "730 天 (两周年)"},
                            {1000, "1000 天 (千日结缘)"}
                        };
                        openPicker("选择实机交往陪伴天数", opts, (int)slotData.timeline.datingDays, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将实机陪伴天数修改为 %d 天吗？", item.id);
                            requestConfirm(msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.timeline.datingDays = (uint16_t)item.id;
                                nlpp::g_saveMgr.setTimelineData(curSlot, sd.timeline);
                            });
                        });
                    }
                    else if (s_selectedItem == 1) // 日历总天数
                    {
                        std::vector<PickerItem> opts = {
                            {100,  "100 日"},
                            {365,  "365 日 (1年)"},
                            {730,  "730 日 (2年)"},
                            {1095, "1095 日 (3年)"}
                        };
                        openPicker("选择日历累计总天数", opts, (int)slotData.timeline.timelineDays, [curSlot](const PickerItem &item) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "确定将日历总天数修改为 %d 日吗？", item.id);
                            requestConfirm(msg, [curSlot, item]() {
                                auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                                sd.timeline.timelineDays = (uint16_t)item.id;
                                nlpp::g_saveMgr.setTimelineData(curSlot, sd.timeline);
                            });
                        });
                    }
                    else if (s_selectedItem == 2) // 清除冷战
                    {
                        requestConfirm("确定清除离线天数，彻底消除冷战惩罚吗？", [curSlot]() {
                            auto &sd = nlpp::g_saveMgr.getSlotData(curSlot);
                            sd.timeline.maxAbsence = 0;
                            nlpp::g_saveMgr.setTimelineData(curSlot, sd.timeline);
                        });
                    }
                    break;
                }

                // Page 6: 收集
                case 6:
                {
                    if (s_selectedItem == 0) // 513件全衣柜
                    {
                        requestConfirm("确定全量解锁 513 件全套衣柜吗？", [curSlot]() {
                            nlpp::g_saveMgr.unlockAllWardrobe513(curSlot);
                        });
                    }
                    else if (s_selectedItem == 1) // 1317项主画廊
                    {
                        requestConfirm("确定全要素点亮 1,317 项全局主画廊吗？", []() {
                            nlpp::g_saveMgr.unlockMasterGallery();
                        });
                    }
                    else if (s_selectedItem == 2) // 清除NEW红点
                    {
                        requestConfirm("确定批量清除全部 NEW 提示红点吗？", [curSlot]() {
                            nlpp::g_saveMgr.unlockAllWardrobe513(curSlot);
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

        // Update pulsating highlight color for TYSS glowing effect
        updatePulseColor();

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
