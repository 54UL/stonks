#pragma once

namespace stnks::gk
{
    inline constexpr const char* JSON_ROOT     = "stnks_data";
    inline constexpr const char* JSON_FILENAME = "stnks.json";

    inline constexpr const char* ENV_ASSETS_ROOT = "ASSETS_STNKS";

    namespace prefix
    {
        inline constexpr const char* APP     = "app";
        inline constexpr const char* PATHS   = "paths";
        inline constexpr const char* STATE   = "state";
        inline constexpr const char* ENGINE  = "engine";
    }

    namespace key
    {
        inline constexpr const char* APP_TITLE      = "title";
        inline constexpr const char* APP_FLAGS       = "flags";
        inline constexpr const char* APP_RESOLUTION  = "resolution";

        inline constexpr const char* PATH_CONFIG     = "config";
        inline constexpr const char* PATH_SHADERS    = "shaders";

        inline constexpr const char* ENGINE_EXE_DIR     = "exe_dir";
        inline constexpr const char* ENGINE_WORKING_DIR = "working_dir";

        // AI settings (persisted)
        inline constexpr const char* AI_AUTO_TRADE     = "ai_auto_trade";      // "1" = enabled
        inline constexpr const char* AI_INTERVAL_SEC   = "ai_interval_sec";    // analysis interval
    }

} // namespace stnks::gk
