#pragma once

#include <UI/UIContext.hpp>
#include <Broker/IBrokerConnector.hpp>
#include <imgui.h>
#include <vector>
#include <string>
#include <cstdint>

namespace stnks
{
    // Panel showing real broker positions fetched from connected broker connectors.
    // Separate from the Strategies panel — positions represent what you currently
    // hold in your brokerage account(s). They can later be promoted to strategies.
    class PositionsPanel
    {
    public:
        explicit PositionsPanel(UIContext& ctx);
        void Draw(bool* open);

    private:
        UIContext& ctx_;

        // Cached broker positions (refreshed periodically)
        struct BrokerPositionEntry
        {
            BrokerPosition pos;
            std::string    brokerName;  // which broker it came from
        };

        std::vector<BrokerPositionEntry> cachedPositions_;
        float refreshTimer_ = 0.f;
        float refreshInterval_ = 30.f;  // seconds between auto-refresh
        bool  needsRefresh_ = true;

        void RefreshPositions();
        void DrawToolbar();
        void DrawPositionsTable();
        void DrawPositionRow(const BrokerPositionEntry& entry);
    };

} // namespace stnks
