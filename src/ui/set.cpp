/*
 *  This file is part of TYSS / NLPPSS.
 *  Copyright (C) 2024-2026 R-YaTian, Cetaceaqua
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <3ds.h>
#include <string>
#include <unordered_map>

#include "ui.h"
#include "gfx.h"
#include "cfg.h"
#include "fs.h"
#include "util.h"
#include "sys.h"

static ui::menu setMenu;
static std::unordered_map<std::string, u8> sconfig;

static bool anySettingChanged(const std::unordered_map<std::string, u8>& newSettings) {
    for (const auto& [key, newValue] : newSettings) {
        auto it = sconfig.find(key);
        if (it == sconfig.end() || it->second != newValue) {
            return true;
        }
    }
    return false;
}

static void toggleBool(void *b)
{
    u8 *in = (u8 *)b;
    *in = !(*in);
}

static void toggleUInt(void *b, int step, int max)
{
    u8 *in = (u8 *)b;
    *in = (*in + step + (max + 1)) % (max + 1);
}

static std::string getLightDarkText(const bool& g)
{
    return g ? getTxt("浅色系") : getTxt("深色系");
}

static std::string getBoolText(const bool& g)
{
    return g ? getTxt("开") : getTxt("关");
}

static std::string getUILangText(const int& g)
{
    std::string s = std::string(getTxt("界面语言")) + ": ";
    if (g == 0) s += "简体中文";
    else if (g == 1) s += "English";
    else if (g == 2) s += "繁體中文";
    return s;
}

static std::string getUILangMessage(const int& g)
{
    switch(g)
    {
        case 1:
            return "UI language set to English!";
        case 2:
            return "界面語言已設置為繁體中文!";
        default:
            return "界面语言已设置为简体中文!";
    }
}

static void setMenuSaveCommon(void *a)
{
    threadInfo *t = (threadInfo *)a;
    t->status->setStatus(getTxt("正在保存设置..."));
    cfg::saveCommon();
    sconfig = cfg::config;
    svcSleepThread(1e+9 / 4);
    t->finished = true;
}

static void setMenuToggleColor(void *a)
{
    toggleBool(a);
    gfx::setColor(cfg::config["lightback"]);
}

static void setMenuIncreaseUILang_t(void *a)
{
    threadInfo *t = (threadInfo *)a;
    u8* in = (u8*)t->argPtr;
    toggleUInt(in, 1, 2);
    t->status->setStatus(getUILangMessage(*in));
    cfg::setUILanguage(*in);
    svcSleepThread(1e+9 / 4);
    t->lock();
    t->argPtr = NULL;
    t->unlock();
    t->finished = true;
}

static void setMenuIncreaseUILang(void *a)
{
    ui::newThread(setMenuIncreaseUILang_t, a, NULL);
}

static void setMenuDecreaseUILang_t(void *a)
{
    threadInfo *t = (threadInfo *)a;
    u8* in = (u8*)t->argPtr;
    toggleUInt(in, -1, 2);
    t->status->setStatus(getUILangMessage(*in));
    cfg::setUILanguage(*in);
    svcSleepThread(1e+9 / 4);
    t->lock();
    t->argPtr = NULL;
    t->unlock();
    t->finished = true;
}

static void setMenuDecreaseUILang(void *a)
{
    ui::newThread(setMenuDecreaseUILang_t, a, NULL);
}

static void setMenuToggleBOOL(void *a)
{
    toggleBool(a);
}

static void setMenuToggleType(void *a)
{
    toggleUInt(a, 1, 1);
}

static void setMenuToggleSlot(void *a)
{
    toggleUInt(a, 1, 2);
}

void ui::setInit(void *a)
{
    sconfig = cfg::config;

    setMenu.reset();
    setMenu.addOpt(getTxt("自动备份存档"), 320);
    setMenu.addOptEvent(0, KEY_A, setMenuToggleBOOL, &cfg::config["autobackup"]);

    setMenu.addOpt(getTxt("界面主题色"), 320);
    setMenu.addOptEvent(1, KEY_A, setMenuToggleColor, &cfg::config["lightback"]);

    setMenu.addOpt(getTxt("界面语言"), 320);
    setMenu.addOptEvent(2, KEY_B, setMenuDecreaseUILang, &cfg::config["uilang"]);
    setMenu.addOptEvent(2, KEY_A, setMenuIncreaseUILang, &cfg::config["uilang"]);

    setMenu.addOpt(getTxt("默认存档类型"), 320);
    setMenu.addOptEvent(3, KEY_A, setMenuToggleType, &cfg::config["default_type"]);

    setMenu.addOpt(getTxt("默认存档槽位"), 320);
    setMenu.addOptEvent(4, KEY_A, setMenuToggleSlot, &cfg::config["default_slot"]);

    setMenu.addOpt(getTxt("切换LR按键功能"), 320);
    setMenu.addOptEvent(5, KEY_A, setMenuToggleBOOL, &cfg::config["swaplrfunc"]);

    if (a)
    {
        threadInfo *t = (threadInfo *)a;
        t->finished = true;
    }
}

void ui::setExit()
{
    cfg::saveCommon();
}

void ui::setUpdate()
{
    uint32_t down = ui::padKeysDown();
    if (down & KEY_PAGE_LEFT)
    {
        if (anySettingChanged(cfg::config)) ui::newThread(setMenuSaveCommon, NULL, NULL);
        ui::state = BACKUP_MGR;
        return;
    }
    else if (down & KEY_PAGE_RIGHT)
    {
        if (anySettingChanged(cfg::config)) ui::newThread(setMenuSaveCommon, NULL, NULL);
        ui::state = SAVE_SELECT;
        return;
    }

    setMenu.editOpt(0, getTxt("自动备份存档") + std::string(": ") + getBoolText(cfg::config["autobackup"]));
    setMenu.editOpt(1, getTxt("界面主题色") + std::string(": ") + getLightDarkText(cfg::config["lightback"]));
    setMenu.editOpt(2, getUILangText(cfg::config["uilang"]));

    std::string typeTxt = (cfg::config["default_type"] == 1) ? getTxt("实体版") : getTxt("数字版");
    setMenu.editOpt(3, getTxt("默认存档类型") + std::string(": ") + typeTxt);

    char slotBuf[32];
    snprintf(slotBuf, sizeof(slotBuf), "%s %d", getTxt("槽位"), (int)cfg::config["default_slot"] + 1);
    setMenu.editOpt(4, getTxt("默认存档槽位") + std::string(": ") + slotBuf);

    setMenu.editOpt(5, getTxt("切换LR按键功能") + std::string(": ") + getBoolText(cfg::config["swaplrfunc"]));

    setMenu.update();
}

void ui::setDrawTop()
{
    ui::drawUIBar(std::string(TITLE_TEXT) + " - " + getTxt("设置"), ui::SCREEN_TOP, true);
    setMenu.draw(0, 26, gfx::txtCont, 400);
}

void ui::setDrawBottom()
{
    std::string setOptsDesc;
    switch(setMenu.getSelected())
    {
        case 0:
            setOptsDesc = getTxt("修改并保存存档数据时，是否自动在 sdmc:/NLPPSS/Saves 生成安全备份。");
            break;
        case 1:
            setOptsDesc = getTxt("选择 UI 界面主题色。\n(浅色系 / 深色系)");
            break;
        case 2:
            setOptsDesc = getTxt("选择 UI 界面显示语言。\n(简体中文 / English / 繁體中文)");
            break;
        case 3:
            setOptsDesc = getTxt("启动软件时默认选中的存档介质类型。\n(数字版 / 实体版)");
            break;
        case 4:
            setOptsDesc = getTxt("启动软件时默认选中的存档槽位编号。\n(槽位 1 / 槽位 2 / 槽位 3)");
            break;
        case 5:
            setOptsDesc = getTxt("切换 L/R 按键和 Left/Right 按键在界面切换中的功能映射。");
            break;
        default:
            break;
    }

    if(!setOptsDesc.empty())
        gfx::drawTextWrap(setOptsDesc, 20, 20, GFX_DEPTH_DEFAULT, 0.5f, 280, gfx::txtCont);

    ui::drawUIBar(getTxt("\ue000 切换/选择  \ue01A\ue019 切换页面"), ui::SCREEN_BOT, true);
}
