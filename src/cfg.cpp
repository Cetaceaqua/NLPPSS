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
#include <unordered_map>

#include "cfg.h"
#include "fs.h"
#include "ctr/3dsgettext.h"

#define NLPPSS_CFG_VER 4

std::unordered_map<std::string, u8> cfg::config;

void cfg::initToDefault()
{
    u8 syslang = CFG_LANGUAGE_EN;
    CFGU_GetSystemLanguage(&syslang);

    cfg::config["autobackup"] = 1; // 自动备份存档：开
    cfg::config["zip"] = 1;        // 导出到 ZIP：开 (默认启用)
    cfg::config["deflateLevel"] = 6; // 默认 6 (标准压缩)
    cfg::config["lightback"] = 0;    // 深色系 (默认)
    cfg::config["titlelang"] = syslang;
    cfg::config["uilang"] = (syslang == CFG_LANGUAGE_ZH ? 0 : (syslang == CFG_LANGUAGE_TW ? 2 : 1));
    cfg::config["autoload"] = 0; // 启动时加载存档：关
    cfg::config["default_type"] = 0; // 默认存档类型：0 数字版, 1 实体版
    cfg::config["default_slot"] = 0; // 默认存档槽位：0 (槽位 1)
    cfg::config["use_builtinkbd"] = 1; // 内置软键盘：开 (默认启用)
}

void cfg::setUILanguage(u8 langIndex)
{
    switch(langIndex)
    {
        case 1:
            setLanguage("en_US");
            break;
        case 2:
            setLanguage("zh_Hant");
            break;
        default:
            setLanguage("zh_Hans");
            break;
    }
}

void cfg::load()
{
    FILE *cfgIn = fopen("/NLPPSS/config.bin", "rb");
    if(cfgIn)
    {
        u8 version = 0;
        fread(&version, sizeof(u8), 1, cfgIn);
        if (version >= 4)
        {
            u8 getVal = 0;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["zip"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["deflateLevel"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["lightback"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["titlelang"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["uilang"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["autobackup"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["autoload"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["default_type"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["default_slot"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["use_builtinkbd"] = getVal;
        }
        else if (version >= 1)
        {
            u8 getVal = 0;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["zip"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["deflateLevel"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["lightback"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); /* skip legacy swaplrfunc */
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["titlelang"] = getVal;
            fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["uilang"] = getVal;

            if (version >= 2)
            {
                fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["autobackup"] = getVal;
                fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["autoload"] = getVal;
                fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["default_type"] = getVal;
                fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["default_slot"] = getVal;

                if (version >= 3)
                {
                    fread(&getVal, sizeof(u8), 1, cfgIn); cfg::config["use_builtinkbd"] = getVal;
                }
                else
                {
                    cfg::config["use_builtinkbd"] = 1;
                }
            }
        }

        fclose(cfgIn);
    }
}

void cfg::saveCommon()
{
    FILE *cfgOut = fopen("/NLPPSS/config.bin", "wb");
    if(cfgOut)
    {
        u8 cfgver = NLPPSS_CFG_VER;
        fwrite(&cfgver, sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["zip"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["deflateLevel"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["lightback"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["titlelang"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["uilang"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["autobackup"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["autoload"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["default_type"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["default_slot"], sizeof(u8), 1, cfgOut);
        fwrite(&cfg::config["use_builtinkbd"], sizeof(u8), 1, cfgOut);
        fclose(cfgOut);
    }
}

