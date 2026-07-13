#pragma once

#include <Strategy/Strategy.hpp>
#include <Market/MarketHours.hpp>
#include <Signals/MarketSignal.hpp>
#include <Events/GraphEvent.hpp>
#include <AI/IMarketAnalyzer.hpp>
#include <imgui.h>

namespace stnks::ui
{

    // Price movement / P&L
    inline constexpr ImVec4 kColorBullish     = {0.15f, 0.65f, 0.36f, 1.f};
    inline constexpr ImVec4 kColorBearish     = {0.84f, 0.19f, 0.19f, 1.f};
    inline constexpr ImVec4 kColorNeutral     = {0.70f, 0.70f, 0.80f, 1.f};
    inline constexpr ImVec4 kColorWarning     = {0.90f, 0.70f, 0.20f, 1.f};
    inline constexpr ImVec4 kColorFee         = {0.90f, 0.70f, 0.30f, 1.f};
    inline constexpr ImVec4 kColorDisabled    = {0.50f, 0.50f, 0.50f, 1.f};
    inline constexpr ImVec4 kColorAccent      = {0.40f, 0.70f, 1.00f, 1.f};
    inline constexpr ImVec4 kColorAI          = {0.90f, 0.50f, 1.00f, 1.f};
    inline constexpr ImVec4 kColorPosition    = {0.40f, 0.70f, 1.00f, 1.f};

    // Connection / status
    inline constexpr ImVec4 kColorConnected   = {0.15f, 0.85f, 0.40f, 1.f};
    inline constexpr ImVec4 kColorDisconnected= {0.85f, 0.20f, 0.20f, 1.f};
    inline constexpr ImVec4 kColorMonitoring  = {0.30f, 0.70f, 1.00f, 1.f};

    // Severity palette (shared by EventSeverity, SignalSeverity, InsightSeverity)
    inline constexpr ImVec4 kSevInfo          = {0.50f, 0.70f, 1.00f, 1.f};
    inline constexpr ImVec4 kSevWarning       = {1.00f, 0.80f, 0.20f, 1.f};
    inline constexpr ImVec4 kSevAlert         = {1.00f, 0.30f, 0.30f, 1.f};

    // Data freshness
    inline constexpr ImVec4 kFreshGreen       = {0.30f, 0.90f, 0.30f, 1.f};
    inline constexpr ImVec4 kFreshYellow      = {0.90f, 0.90f, 0.30f, 1.f};
    inline constexpr ImVec4 kFreshOrange      = {0.90f, 0.60f, 0.20f, 1.f};
    inline constexpr ImVec4 kFreshRed         = {0.90f, 0.30f, 0.30f, 1.f};

    // Alert title bar (for pulsing window titles)
    inline constexpr ImVec4 kAlertTitleActive = {0.50f, 0.10f, 0.10f, 1.f};
    inline constexpr ImVec4 kAlertTitleBg     = {0.30f, 0.08f, 0.08f, 1.f};

    // Toast colors
    inline constexpr ImVec4 kToastSuccess     = {0.15f, 0.85f, 0.40f, 1.f};
    inline constexpr ImVec4 kToastError       = {0.95f, 0.25f, 0.25f, 1.f};
    inline constexpr ImVec4 kToastInfo        = {0.50f, 0.70f, 1.00f, 1.f};
    inline constexpr ImVec4 kToastWarning     = {0.90f, 0.40f, 0.20f, 1.f};
    inline constexpr ImVec4 kToastDryRun      = {0.50f, 0.70f, 1.00f, 1.f};

    // Table row backgrounds
    inline constexpr ImVec4 kTableRowBg       = {0.09f, 0.09f, 0.12f, 1.f};
    inline constexpr ImVec4 kTableRowBgAlt    = {0.11f, 0.11f, 0.14f, 1.f};
    inline constexpr ImVec4 kTableHdrHover    = {0.22f, 0.28f, 0.42f, 0.8f};
    inline constexpr ImVec4 kTableHdrActive   = {0.26f, 0.34f, 0.52f, 0.9f};
    inline constexpr ImU32  kRowEditHighlight = IM_COL32(60, 55, 20, 200);
    inline constexpr ImU32  kRowSelectTint    = IM_COL32(35, 55, 100, 180);
    inline constexpr ImU32  kRowHoverTint     = IM_COL32(28, 38, 58, 140);

    // Toolbar / filter
    inline constexpr ImVec4 kFilterActiveText = {1.f, 0.7f, 0.2f, 1.f};
    inline constexpr ImVec4 kDeleteText       = {0.9f, 0.3f, 0.3f, 1.f};
    inline constexpr ImVec4 kSelectedTabBtn   = {0.2f, 0.4f, 0.7f, 1.f};

    inline constexpr ImVec4 kTimeframeBtnRT   = {0.15f, 0.45f, 0.20f, 1.f};
    inline constexpr ImVec4 kTimeframeBtnNorm = {0.20f, 0.28f, 0.45f, 1.f};

    inline constexpr ImU32 kABCharts     = IM_COL32( 60, 140, 220, 255);  // blue
    inline constexpr ImU32 kABStrategies = IM_COL32(220, 160,  40, 255);  // gold
    inline constexpr ImU32 kABPortfolio  = IM_COL32( 50, 180, 100, 255);  // green
    inline constexpr ImU32 kABWizard     = IM_COL32(180,  80, 200, 255);  // purple
    inline constexpr ImU32 kABDashboard  = IM_COL32( 80, 180, 200, 255);  // cyan
    inline constexpr ImU32 kABRecs       = IM_COL32( 50, 160,  80, 255);  // emerald
    inline constexpr ImU32 kABWarnings   = IM_COL32(220,  80,  60, 255);  // red
    inline constexpr ImU32 kABAIOps      = IM_COL32(140, 100, 220, 255);  // lavender
    inline constexpr ImU32 kABEvents     = IM_COL32(200, 130,  50, 255);  // orange
    inline constexpr ImU32 kABSignals    = IM_COL32( 60, 200, 180, 255);  // teal
    inline constexpr ImU32 kABServer     = IM_COL32(120, 120, 140, 255);  // gray
    inline constexpr ImU32 kABThreads    = IM_COL32(100, 100, 120, 255);  // dark gray
    inline constexpr ImU32 kABPositions  = IM_COL32(230, 120,  90, 255);  // coral
    inline constexpr ImU32 kABBarBg      = IM_COL32( 15,  18,  23, 255);  // bar background
    inline constexpr ImU32 kABTextActive = IM_COL32(255, 255, 255, 255);
    inline constexpr ImU32 kABTextDim    = IM_COL32(200, 200, 200, 180);
    inline constexpr ImU32 kABRingActive = IM_COL32(255, 255, 255, 100);

    // Dim a color to ~1/3 brightness for inactive state
    inline constexpr ImU32 ABDimColor(ImU32 col)
    {
        return IM_COL32(
            ((col >>  0) & 0xFF) / 3,
            ((col >>  8) & 0xFF) / 3,
            ((col >> 16) & 0xFF) / 3, 180);
    }


    inline constexpr float kDisabledAlpha       = 0.5f;
    inline constexpr float kDismissedAlpha      = 0.4f;
    inline constexpr float kPortfolioSampleSec  = 15.f;
    inline constexpr int   kPortfolioMaxSamples = 240;

    // Centralized enum→display transforms: color, label, icon.

    // -- MarketType --

    inline ImVec4 MarketTypeColor(MarketType t)
    {
        switch (t)
        {
        case MarketType::US:      return {0.90f, 0.92f, 0.96f, 1.f};
        case MarketType::Mexico:  return {0.95f, 0.75f, 0.25f, 1.f};
        case MarketType::Crypto:  return {0.30f, 0.85f, 0.90f, 1.f};
        default:                  return {0.70f, 0.70f, 0.70f, 1.f};
        }
    }

    // -- BrokerSource --

    inline ImVec4 BrokerColor(BrokerSource b)
    {
        switch (b)
        {
        case BrokerSource::Binance:    return {0.96f, 0.76f, 0.07f, 1.f};
        case BrokerSource::MetaTrader: return {0.30f, 0.75f, 0.40f, 1.f};
        case BrokerSource::Auto:       return {0.45f, 0.80f, 0.85f, 1.f};
        }
        return {0.55f, 0.55f, 0.60f, 1.f};
    }

    inline const char* BrokerIcon(BrokerSource b)
    {
        switch (b)
        {
        case BrokerSource::Binance:    return "[B]";
        case BrokerSource::MetaTrader: return "[M]";
        case BrokerSource::Auto:       return "[A]";
        }
        return "[?]";
    }

    inline const char* BrokerDescription(BrokerSource b)
    {
        switch (b)
        {
        case BrokerSource::Binance:    return "Binance Spot - Crypto";
        case BrokerSource::MetaTrader: return "MetaTrader 5 - Forex/CFD";
        default:                       return "Auto - resolved from data source";
        }
    }

    // Map a data source name string to a display color
    inline ImVec4 SourceNameColor(const std::string& src)
    {
        if (src == "Binance")        return {0.96f, 0.76f, 0.07f, 1.f};
        if (src == "MT5")            return {0.30f, 0.75f, 0.40f, 1.f};
        if (src == "Yahoo Finance")  return {0.55f, 0.30f, 0.75f, 1.f};
        return {0.50f, 0.50f, 0.55f, 1.f};
    }

    // -- StrategyType --

    inline ImVec4 StrategyTypeColor(StrategyType t)
    {
        switch (t)
        {
        case StrategyType::AI:       return kColorAI;
        case StrategyType::Position: return kColorPosition;
        default:                     return kColorDisabled;
        }
    }

    inline const char* StrategyTypeLabel(StrategyType t)
    {
        switch (t)
        {
        case StrategyType::AI:       return "AI";
        case StrategyType::Position: return "POS";
        default:                     return "TP/SL";
        }
    }

    // -- StrategyDirection --

    inline ImVec4 DirectionColor(StrategyDirection d)
    {
        return d == StrategyDirection::Long ? kColorBullish : kColorBearish;
    }

    inline const char* DirectionLabel(StrategyDirection d)
    {
        return d == StrategyDirection::Long ? "LONG" : "SHORT";
    }

    // -- StrategyStatus --

    inline ImVec4 StatusColor(StrategyStatus s)
    {
        switch (s)
        {
        case StrategyStatus::Active:    return kColorMonitoring;
        case StrategyStatus::TPHit:     return kColorBullish;
        case StrategyStatus::SLHit:     return kColorBearish;
        case StrategyStatus::Cancelled: return kColorDisabled;
        }
        return kColorDisabled;
    }

    inline const char* StatusLabel(StrategyStatus s)
    {
        switch (s)
        {
        case StrategyStatus::Active:    return "Active";
        case StrategyStatus::TPHit:     return "TP Hit";
        case StrategyStatus::SLHit:     return "SL Hit";
        case StrategyStatus::Cancelled: return "Cancelled";
        }
        return "Unknown";
    }

    // -- Severity (shared icon/color for EventSeverity, SignalSeverity, InsightSeverity) --

    inline ImVec4 SeverityColor(EventSeverity s)
    {
        switch (s)
        {
        case EventSeverity::Alert:   return kSevAlert;
        case EventSeverity::Warning: return kSevWarning;
        default:                     return kSevInfo;
        }
    }

    inline const char* SeverityIcon(EventSeverity s)
    {
        switch (s)
        {
        case EventSeverity::Alert:   return "!!";
        case EventSeverity::Warning: return "!";
        default:                     return "i";
        }
    }

    inline ImVec4 SeverityColor(SignalSeverity s)
    {
        switch (s)
        {
        case SignalSeverity::Alert:   return kSevAlert;
        case SignalSeverity::Warning: return kSevWarning;
        default:                      return kSevInfo;
        }
    }

    inline const char* SeverityIcon(SignalSeverity s)
    {
        switch (s)
        {
        case SignalSeverity::Alert:   return "[!]";
        case SignalSeverity::Warning: return "[*]";
        default:                      return "[i]";
        }
    }

    inline ImVec4 SeverityColor(InsightSeverity s)
    {
        switch (s)
        {
        case InsightSeverity::Alert:   return kSevAlert;
        case InsightSeverity::Warning: return kSevWarning;
        default:                       return kSevInfo;
        }
    }

    // -- SignalOrigin --

    inline ImVec4 SignalOriginColor(SignalOrigin o)
    {
        switch (o)
        {
        case SignalOrigin::GraphEvent: return {0.30f, 0.80f, 0.50f, 1.f};
        case SignalOrigin::Pattern:    return {0.30f, 0.80f, 0.50f, 1.f};
        case SignalOrigin::AI:         return {0.60f, 0.40f, 1.00f, 1.f};
        case SignalOrigin::Broker:     return {0.90f, 0.70f, 0.20f, 1.f};
        case SignalOrigin::Strategy:   return {0.90f, 0.60f, 0.20f, 1.f};
        default:                       return {0.60f, 0.60f, 0.60f, 1.f};
        }
    }

    inline const char* SignalOriginLabel(SignalOrigin o)
    {
        switch (o)
        {
        case SignalOrigin::GraphEvent: return "TECH";
        case SignalOrigin::Pattern:    return "TECH";
        case SignalOrigin::AI:         return "AI";
        case SignalOrigin::Broker:     return "BRKR";
        case SignalOrigin::Strategy:   return "STRAT";
        default:                       return "?";
        }
    }

    // -- OperationType --

    inline ImVec4 OperationTypeColor(OperationType t)
    {
        switch (t)
        {
        case OperationType::Sell:     return kColorBearish;
        case OperationType::Buy:      return kColorBullish;
        default:                      return kColorNeutral;
        }
    }

    inline const char* OperationTypeLabel(OperationType t)
    {
        switch (t)
        {
        case OperationType::Hold:     return "HOLD";
        case OperationType::Buy:      return "BUY";
        case OperationType::Sell:     return "SELL";
        case OperationType::AdjustTP: return "ADJ TP";
        case OperationType::AdjustSL: return "ADJ SL";
        }
        return "?";
    }

    // -- MarketState --

    inline ImVec4 MarketStateColor(MarketState s)
    {
        switch (s)
        {
        case MarketState::Open:       return {0.20f, 0.90f, 0.30f, 1.f};
        case MarketState::PreMarket:  return {0.90f, 0.80f, 0.20f, 1.f};
        case MarketState::AfterHours: return {0.80f, 0.60f, 0.20f, 1.f};
        case MarketState::Closed:     return {0.60f, 0.30f, 0.30f, 1.f};
        }
        return kColorDisabled;
    }

    // -- Risk/Reward color --

    inline ImVec4 RiskRewardColor(float rr)
    {
        if (rr >= 2.f) return kColorBullish;
        if (rr >= 1.f) return kColorWarning;
        return kColorBearish;
    }

    // -- P/L color --

    inline ImVec4 PnLColor(float pnlPct)
    {
        return pnlPct >= 0.f ? kColorBullish : kColorBearish;
    }

    // -- Data freshness (age in seconds) --

    inline ImVec4 FreshnessColor(float ageSec)
    {
        if (ageSec < 60.f)   return kFreshGreen;
        if (ageSec < 300.f)  return kFreshYellow;
        if (ageSec < 900.f)  return kFreshOrange;
        return kFreshRed;
    }

    // -- Latency color --

    inline ImVec4 LatencyColor(float ms)
    {
        if (ms < 50.f)  return kColorConnected;
        if (ms < 150.f) return kColorWarning;
        return kColorDisconnected;
    }


    // Full verbose format: "3d 2h 15m 4s"
    inline void FormatAge(char* buf, int bufSize, int64_t elapsed)
    {
        int64_t days  = elapsed / 86400;
        int64_t hours = (elapsed % 86400) / 3600;
        int64_t mins  = (elapsed % 3600) / 60;
        int64_t secs  = elapsed % 60;

        if (days > 0)
            snprintf(buf, bufSize, "%lldd %lldh %lldm %llds",
                     (long long)days, (long long)hours, (long long)mins, (long long)secs);
        else if (hours > 0)
            snprintf(buf, bufSize, "%lldh %lldm %llds",
                     (long long)hours, (long long)mins, (long long)secs);
        else if (mins > 0)
            snprintf(buf, bufSize, "%lldm %llds",
                     (long long)mins, (long long)secs);
        else
            snprintf(buf, bufSize, "%llds", (long long)secs);
    }

    // Compact format for freshness badges: "5s", "3m", "2h", "4d"
    inline void FormatDuration(char* buf, int bufSize, int64_t secs)
    {
        if (secs < 60)        snprintf(buf, bufSize, "%llds",  (long long)secs);
        else if (secs < 3600) snprintf(buf, bufSize, "%lldm",  (long long)(secs / 60));
        else if (secs < 86400) snprintf(buf, bufSize, "%lldh", (long long)(secs / 3600));
        else                   snprintf(buf, bufSize, "%lldd", (long long)(secs / 86400));
    }


    enum class EventTimeRange : int
    {
        Last1H = 0, Last4H, Last1D, Last3D, Last1W, All
    };

    inline constexpr const char* kEventTimeRangeLabels[] = {"1H", "4H", "1D", "3D", "1W", "All"};
    inline constexpr int kEventTimeRangeCount = 6;

    inline int64_t EventTimeRangeSeconds(EventTimeRange r)
    {
        switch (r)
        {
        case EventTimeRange::Last1H: return 3600;
        case EventTimeRange::Last4H: return 4 * 3600;
        case EventTimeRange::Last1D: return 86400;
        case EventTimeRange::Last3D: return 3 * 86400;
        case EventTimeRange::Last1W: return 7 * 86400;
        case EventTimeRange::All:    return 0;
        }
        return 86400;
    }


    inline void ApplyDefaultTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding      = 4.0f;
        style.FrameRounding       = 2.0f;
        style.GrabRounding        = 2.0f;
        style.ScrollbarRounding   = 4.0f;
        style.TabRounding         = 3.0f;
        style.DockingSeparatorSize = 2.0f;

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        ImVec4* c = style.Colors;
        c[ImGuiCol_WindowBg]         = {0.08f, 0.08f, 0.10f, 1.00f};
        c[ImGuiCol_TitleBg]          = {0.06f, 0.06f, 0.08f, 1.00f};
        c[ImGuiCol_TitleBgActive]    = {0.10f, 0.12f, 0.18f, 1.00f};
        c[ImGuiCol_FrameBg]          = {0.12f, 0.12f, 0.15f, 1.00f};
        c[ImGuiCol_Header]           = {0.15f, 0.18f, 0.25f, 1.00f};
        c[ImGuiCol_HeaderHovered]    = {0.20f, 0.25f, 0.35f, 1.00f};
        c[ImGuiCol_Button]           = {0.15f, 0.18f, 0.25f, 1.00f};
        c[ImGuiCol_ButtonHovered]    = {0.20f, 0.28f, 0.40f, 1.00f};
        c[ImGuiCol_Tab]              = {0.10f, 0.12f, 0.18f, 1.00f};
        c[ImGuiCol_TabHovered]       = {0.22f, 0.28f, 0.40f, 1.00f};
        c[ImGuiCol_TabActive]        = {0.16f, 0.20f, 0.30f, 1.00f};
        c[ImGuiCol_DockingPreview]   = {0.22f, 0.35f, 0.55f, 0.70f};
        c[ImGuiCol_DockingEmptyBg]   = {0.06f, 0.06f, 0.08f, 1.00f};
        c[ImGuiCol_Separator]        = {0.18f, 0.20f, 0.28f, 1.00f};
        c[ImGuiCol_SeparatorHovered] = {0.25f, 0.35f, 0.55f, 1.00f};
        c[ImGuiCol_SeparatorActive]  = {0.30f, 0.45f, 0.65f, 1.00f};
    }

} // namespace stnks::ui
