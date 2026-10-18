#pragma once
#include <string>

#ifndef APP_TITLE
#define APP_TITLE "NLPP Save Studio"
#endif

#ifndef APP_VERSION
#define APP_VERSION "v1.0.0"
#endif

namespace nlpp
{
    namespace version
    {
        constexpr const char *TITLE   = APP_TITLE;
        constexpr const char *STRING  = APP_VERSION;

        inline const std::string& getTitleText()
        {
            static const std::string s_title = std::string(TITLE) + " - " + STRING;
            return s_title;
        }
    }
}
