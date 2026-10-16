#pragma once

#include <3ds.h>
#include <string>
#include <vector>

namespace ui
{
    void nlppInit();
    void nlppExit();
    void nlppDrawTop();
    void nlppDrawBot();
    void nlppUpdate();

    // Helper to trigger save commit with feedback
    void nlppSaveAndCommit();
}
