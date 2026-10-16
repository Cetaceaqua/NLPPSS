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
    bool changed = false;
    for (const auto& [key, newValue] : newSettings) {
        auto it = sconfig.find(key);
        if (it == sconfig.end() || it->second != newValue) {
            changed = true;
            break;
        }
    }
    return changed;
}

static void toggleBool(void *b)
{
    bool *in = (bool *)b;
    *in = !(*in);
}

static void toggleUInt(void *b, int step, int max)
{
    int *in = (int *)b;
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
    int* in = (int*)t->argPtr;
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
    int* in = (int*)t->argPtr;
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

void ui::setInit(void *a)
{
    threadInfo *t = (threadInfo *)a;
    sconfig = cfg::config;

    setMenu.addOpt(getTxt("界面主题色"), 320);
    setMenu.addOptEvent(0, KEY_A, setMenuToggleColor, &cfg::config["lightback"]);

    setMenu.addOpt(getTxt("界面语言"), 320);
    setMenu.addOptEvent(1, KEY_B, setMenuDecreaseUILang, &cfg::config["uilang"]);
    setMenu.addOptEvent(1, KEY_A, setMenuIncreaseUILang, &cfg::config["uilang"]);

    setMenu.addOpt(getTxt("切换LR按键功能"), 320);
    setMenu.addOptEvent(2, KEY_A, setMenuToggleBOOL, &cfg::config["swaplrfunc"]);

    t->finished = true;
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
        ui::state = USR;
    }
    else if (down & KEY_PAGE_RIGHT)
    {
        if (anySettingChanged(cfg::config)) ui::newThread(setMenuSaveCommon, NULL, NULL);
        ui::state = USR;
    }

    setMenu.editOpt(0, getTxt("界面主题色") + std::string(": ") + getLightDarkText(cfg::config["lightback"]));
    setMenu.editOpt(1, getUILangText(cfg::config["uilang"]));
    setMenu.editOpt(2, getTxt("切换LR按键功能") + std::string(": ") + getBoolText(cfg::config["swaplrfunc"]));

    setMenu.update();
}

void ui::setDrawTop()
{
    ui::drawUIBar(std::string(TITLE_TEXT) + " - " + getTxt("设置"), ui::SCREEN_TOP, true);
    setMenu.draw(0, 22, gfx::txtCont, 400);
}

void ui::setDrawBottom()
{
    std::string setOptsDesc;
    switch(setMenu.getSelected())
    {
        case 0:
            setOptsDesc = getTxt("选择 UI 界面主题色。\n(浅色系/深色系)");
            break;
        case 1:
            setOptsDesc = getTxt("选择 UI 界面显示语言。");
            break;
        case 2:
            setOptsDesc = getTxt("切换 L/R 按键和 Left/Right 按键在界面切换中的功能映射。");
            break;
        default:
            break;
    }

    if(!setOptsDesc.empty())
        gfx::drawTextWrap(setOptsDesc, 20, 20, 0.45f, 0.45f, 280, gfx::txtCont);
}
