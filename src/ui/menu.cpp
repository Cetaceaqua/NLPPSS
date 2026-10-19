/*
 *  This file is part of TYSS.
 *  Copyright (C) 2024-2026 R-YaTian
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

#include <string>
#include <algorithm>

#include "ui.h"
#include "gfx.h"
#include "util.h"

int ui::menu::addOpt(const std::string &add, int maxWidth, const std::string& desc)
{
    menuOpt newOpt;
    std::string _desc = "";
    std::u16string replaceStr = u"⋯";
    int descWidth = 0;
    int ofs = 2;
    if (util::endsWith(add, std::string(".zip")) || util::endsWith(add, std::string(".sav")) || util::endsWith(add, std::string(".bin")))
    {
        replaceStr += util::toUtf16(add.substr(add.size() - 4));
        ofs += 4;
    }

    if (!desc.empty() && desc != "")
    {
        _desc = " (" + desc + ")";
        descWidth = gfx::getTextWidth(_desc);
    }

    int strWidth = gfx::getTextWidth(add) + descWidth;
    if (strWidth < maxWidth - 16 || maxWidth == 0)
        newOpt.txt = add + _desc;
    else
    {
        std::u16string tmp, _add = util::toUtf16(add);
        for (unsigned i = 0; i < _add.length(); i++)
        {
            tmp += _add[i];
            if ((int)gfx::getTextWidth(util::toUtf8(tmp)) >= maxWidth - 16 - descWidth)
            {
                tmp.replace(i - ofs, ofs + 1, replaceStr);
                newOpt.txt = util::toUtf8(tmp) + _desc;
                break;
            }
        }
    }

    opt.push_back(newOpt);
    return opt.size() - 1;
}

void ui::menu::addOptEvent(unsigned ind, uint32_t _key, funcPtr _func, void *_args)
{
    menuOptEvent newEvent = {_func, _args, _key};
    opt[ind].events.push_back(newEvent);
}

void ui::menu::reset()
{
    opt.clear();
    clrSh = 0x88;
    clrAdd = true;
    selected = 0;
    fc = 0;
    start = 0;
}

void ui::menu::adjust()
{
    if (opt.empty())
    {
        selected = 0;
        start = 0;
        return;
    }
    int size = (int)opt.size() - 1;
    selected = std::clamp(selected, 0, size);

    if ((int)opt.size() <= maxVisible)
        start = 0;
    else
    {
        int maxStart = (int)opt.size() - maxVisible;
        int half = maxVisible / 2;
        start = std::clamp(selected - half, 0, maxStart);
    }
}

void ui::menu::setSelected(const int &newSel)
{
    if (opt.empty())
    {
        selected = 0;
        start = 0;
        return;
    }
    int size = (int)opt.size() - 1;
    selected = std::clamp(newSel, 0, size);

    if ((int)opt.size() <= maxVisible)
    {
        start = 0;
    }
    else
    {
        int maxStart = (int)opt.size() - maxVisible;
        int half = maxVisible / 2;
        start = std::clamp(selected - half, 0, maxStart);
    }
}

void ui::menu::update()
{
    if (opt.empty())
        return;

    uint32_t down = ui::padKeysDown();
    uint32_t held = ui::padKeysHeld();

    if ((held & KEY_DUP) || (held & KEY_DDOWN))
        fc++;
    else
        fc = 0;
    if (fc > 10)
        fc = 0;

    int size = (int)opt.size() - 1;
    int maxStart = std::max(0, (int)opt.size() - maxVisible);
    int half = maxVisible / 2;

    if ((down & KEY_DUP) || ((held & KEY_DUP) && fc == 10))
    {
        selected--;
        if (selected < 0)
        {
            selected = size;
            start = maxStart;
        }
        else if (size < maxVisible)
        {
            start = 0;
        }
        else
        {
            if (selected - start < half && start > 0)
                start--;
            if (start > selected)
                start = selected;
        }
    }
    else if ((down & KEY_DDOWN) || ((held & KEY_DDOWN) && fc == 10))
    {
        selected++;
        if (selected > size)
        {
            selected = 0;
            start = 0;
        }
        else if (size < maxVisible)
        {
            start = 0;
        }
        else
        {
            if (selected - start > half && start < maxStart)
                start++;
            if (selected > start + (maxVisible - 1))
                start = selected - (maxVisible - 1);
        }
    }
    else if (paging && (down & KEY_DRIGHT))
    {
        selected += maxVisible;
        if (selected > size)
            selected = size;
        if ((int)opt.size() <= maxVisible)
            start = 0;
        else
            start = std::clamp(selected - half, 0, maxStart);
    }
    else if (paging && (down & KEY_DLEFT))
    {
        selected -= maxVisible;
        if (selected < 0)
            selected = 0;
        if ((int)opt.size() <= maxVisible)
            start = 0;
        else
            start = std::clamp(selected - half, 0, maxStart);
    }

    if (down && !opt[selected].events.empty())
    {
        for (ui::menuOptEvent &m : opt[selected].events)
        {
            if (m.func && down & m.button)
                (*(m.func))(m.args);
        }
    }

    if (cb)
        (*(cb))(args);
}

void ui::menu::draw(const int &x, const int &y, const uint32_t &baseClr, const uint32_t &rectWidth, bool isFileMenu)
{
    if (opt.empty())
        return;

    if (clrAdd)
    {
        clrSh += 6;
        if (clrSh >= 0xFA)
            clrAdd = false;
    }
    else
    {
        clrSh -= 3;
        if (clrSh <= 0x88)
            clrAdd = true;
    }

    int length = 0;
    if ((int)opt.size() < maxVisible)
        length = opt.size();
    else
        length = std::min((int)opt.size(), start + maxVisible);

    uint32_t rectClr = 0xFF << 24 | 0xC5 << 16 | clrSh << 8 | 0x00;

    for (int i = start; i < length; i++)
    {
        if (i == selected)
        {
            gfx::drawBoundingBox(x, (y - 2) + ((i - start) * 18), rectWidth, 18, GFX_DEPTH_DEFAULT, rectClr);
            C2D_DrawRectSolid(x + 4, y + 1 + ((i - start) * 18), GFX_DEPTH_DEFAULT, 2, 12, 0xFFC5FF00);
        }

        gfx::drawText(isFileMenu ? opt[i].txt : getTextFromMap(opt[i].txt.c_str()), x + 8, (y - 1) + ((i - start) * 18), GFX_DEPTH_DEFAULT, 0.5f, baseClr);
    }
}
