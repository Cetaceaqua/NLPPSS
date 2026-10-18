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

#include <3ds.h>
#include <malloc.h>

#include "gfx.h"
#include "fs.h"
#include "ui.h"
#include "util.h"
#include "sys.h"
#include "cfg.h"

#include "nlpp_save.h"
#include "nlpp_dict.h"

int main(int argc, const char *argv[])
{
    sys::init();
    gfx::init();
    fs::init();
    cfg::initToDefault();
    cfg::load();
    cfg::setUILanguage(cfg::config["uilang"]);

    nlpp::Dict::init();
    nlpp::g_saveMgr.init();
    ui::init();

    while(aptMainLoop() && ui::runApp()){ }

    cfg::saveCommon();
    ui::exit();
    nlpp::g_saveMgr.exit();
    nlpp::Dict::exit();
    fs::exit();
    gfx::exit();
    sys::exit();
    return 0;
}
