#pragma once

#include <3ds.h>
#include <string>

namespace ui
{
    // Mode 1: Main Menu - 存档 (Save Slots)
    void saveSelectInit(void *a = nullptr);
    void saveSelectExit();
    void saveSelectUpdate();
    void saveSelectDrawTop();
    void saveSelectDrawBot();

    // Mode 1: Main Menu - 备份管理 (Backup Management)
    void backupMgrInit(void *a = nullptr);
    void backupMgrExit();
    void backupMgrUpdate();
    void backupMgrDrawTop();
    void backupMgrDrawBot();
    void backupMgrRefresh();

    // Mode 2: Slot Editor - 4 Pages
    void editorInit(int slot);
    void editorExit();
    void editorUpdate();
    void editorDrawTop();
    void editorDrawBot();
}
