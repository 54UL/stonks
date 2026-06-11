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

        // Display settings (persisted)
        inline constexpr const char* WINDOW_MODE       = "window_mode";        // 0=Windowed, 1=Fullscreen, 2=Borderless
        inline constexpr const char* VSYNC             = "vsync";              // "0" or "1"
        inline constexpr const char* RESOLUTION_W      = "resolution_w";       // e.g. "1920"
        inline constexpr const char* RESOLUTION_H      = "resolution_h";       // e.g. "1080"
        inline constexpr const char* FPS_TARGET        = "fps_target";         // 0=unlimited, 30, 60, 120, 144, 240

        // AI settings (persisted)
        inline constexpr const char* AI_AUTO_TRADE     = "ai_auto_trade";      // "1" = enabled
        inline constexpr const char* AI_INTERVAL_SEC   = "ai_interval_sec";    // analysis interval

        // Trading settings (persisted)
        inline constexpr const char* LIVE_TRADING      = "live_trading";       // "1" = real orders, "0" = dry-run
    }

} // namespace stnks::gk
