#pragma once

#include <Strategy/Strategy.hpp>
#include <Charts/ChartLayer.hpp>
#include <Market/MarketHours.hpp>
#include <imgui.h>
#include <string>
#include <vector>
#include <ctime>
#include <cstdio>

namespace stnks
{
    class StrategyWizard
    {
    public:
        enum class Mode { Closed, Create, Edit };
        enum class TimeMode { Current, Historical };

        void OpenCreate(const std::string& symbol, StrategyType type,
                        StrategyDirection direction, float currentPrice,
                        float visiblePriceRange = 0.f)
        {
            mode_ = Mode::Create;
            timeMode_ = TimeMode::Current;
            selectedCandle_ = -1;
            confirmed_ = false;
            cancelled_ = false;
            visibleRange_ = visiblePriceRange;

            strategy_ = Strategy{};
            strategy_.symbol    = symbol;
            strategy_.type      = type;
            strategy_.direction = direction;
            strategy_.entryPrice = currentPrice;
            strategy_.quantity   = 1.f;

            if (type == StrategyType::TPSL)
            {
                float offset = ComputeDefaultOffset(currentPrice);
                if (direction == StrategyDirection::Long)
                {
                    strategy_.takeProfit = currentPrice + offset;
                    strategy_.stopLoss   = currentPrice - offset;
                }
                else
                {
                    strategy_.takeProfit = currentPrice - offset;
                    strategy_.stopLoss   = currentPrice + offset;
                }
            }

            snprintf(symbolBuf_, sizeof(symbolBuf_), "%s", symbol.c_str());
            snprintf(notesBuf_, sizeof(notesBuf_), "");
        }

        void OpenEdit(const Strategy& s)
        {
            mode_ = Mode::Edit;
            timeMode_ = TimeMode::Current;
            selectedCandle_ = -1;
            confirmed_ = false;
            cancelled_ = false;

            strategy_ = s;
            snprintf(symbolBuf_, sizeof(symbolBuf_), "%s", s.symbol.c_str());
            snprintf(notesBuf_, sizeof(notesBuf_), "%s", s.notes.c_str());
        }

        void Close()
        {
            mode_ = Mode::Closed;
            confirmed_ = false;
            cancelled_ = false;
        }

        bool IsOpen() const { return mode_ != Mode::Closed; }
        bool IsCreating() const { return mode_ == Mode::Create; }
        bool IsEditing() const { return mode_ == Mode::Edit; }

        Strategy& GetStrategy() { return strategy_; }
        const Strategy& GetStrategy() const { return strategy_; }

        void SetFocusedChart(const std::string& symbol, float currentPrice)
        {
            focusedSymbol_ = symbol;
            focusedPrice_  = currentPrice;
        }
        const std::string& GetFocusedSymbol() const { return focusedSymbol_; }
        float GetFocusedPrice() const { return focusedPrice_; }

        bool WasConfirmed() const { return confirmed_; }
        bool WasCancelled() const { return cancelled_; }
        void ConsumeResult() { confirmed_ = false; cancelled_ = false; }

        void BeginFrame()
        {
            chartClickCandle_ = -1;
        }

        // Must be called BEFORE DrawDockable()/DrawOverlay() each frame.
        // Can be called multiple times per frame (once per chart panel) — first valid click wins.
        void PreUpdate(const ImVec2& chartMin, const ImVec2& chartMax,
                       int focusedCandle, int totalCandles,
                       const std::vector<Candle>* candles)
        {
            if (mode_ == Mode::Closed) return;
            if (timeMode_ != TimeMode::Historical) return;
            if (chartClickCandle_ >= 0) return; // Already detected a click this frame

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
            {
                ImVec2 mouse = ImGui::GetMousePos();
                if (mouse.x >= chartMin.x && mouse.x <= chartMax.x &&
                    mouse.y >= chartMin.y && mouse.y <= chartMax.y &&
                    focusedCandle >= 0)
                {
                    chartClickCandle_ = focusedCandle;
                    chartClickTotalCandles_ = totalCandles;
                    chartClickCandles_ = candles;
                }
            }
        }

        bool DrawDockable(bool* pOpen, const std::vector<Candle>* candles,
                          int focusedCandle, int totalCandles)
        {
            if (mode_ != Mode::Closed && timeMode_ == TimeMode::Historical && chartClickCandle_ >= 0)
            {
                int lastIdx = chartClickTotalCandles_ - 1;
                if (chartClickCandle_ < lastIdx)
                {
                    selectedCandle_ = chartClickCandle_;
                    if (chartClickCandles_ && chartClickCandle_ < (int)chartClickCandles_->size())
                        strategy_.entryPrice = (*chartClickCandles_)[chartClickCandle_].close;
                }
                else
                {
                    timeMode_ = TimeMode::Current;
                    selectedCandle_ = -1;
                }
            }

            ImGui::SetNextWindowSize(ImVec2(280.f, 420.f), ImGuiCond_FirstUseEver);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.18f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.28f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.25f, 0.42f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.55f, 1.f));

            const char* wizTitle = (mode_ == Mode::Edit)
                ? "Strategy Wizard [EDIT]###StrategyWizard"
                : (mode_ == Mode::Create)
                    ? "Strategy Wizard [NEW]###StrategyWizard"
                    : "Strategy Wizard###StrategyWizard";
            bool visible = ImGui::Begin(wizTitle, pOpen);

            if (visible)
            {
                if (mode_ == Mode::Closed)
                {
                    DrawIdleState();
                }
                else
                {
                    confirmed_ = false;
                    cancelled_ = false;
                    DrawActiveWizard(candles, focusedCandle, totalCandles);
                }
            }

            ImGui::End();
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(3);

            return visible;
        }

        void DrawOverlay(const ImVec2& chartMin, const ImVec2& chartMax,
                         int focusedCandle, int totalCandles,
                         const std::vector<Candle>* candles)
        {
            if (mode_ == Mode::Closed) return;

            if (timeMode_ == TimeMode::Historical && chartClickCandle_ >= 0)
            {
                int lastIdx = chartClickTotalCandles_ - 1;
                if (chartClickCandle_ < lastIdx)
                {
                    selectedCandle_ = chartClickCandle_;
                    if (chartClickCandles_ && chartClickCandle_ < (int)chartClickCandles_->size())
                        strategy_.entryPrice = (*chartClickCandles_)[chartClickCandle_].close;
                }
                else
                {
                    timeMode_ = TimeMode::Current;
                    selectedCandle_ = -1;
                }
            }

            confirmed_ = false;
            cancelled_ = false;

            if (firstOpen_)
            {
                ImVec2 initPos(chartMin.x + 10.f, chartMin.y + 10.f);
                ImGui::SetNextWindowPos(initPos, ImGuiCond_Always);
                firstOpen_ = false;
            }

            ImGui::SetNextWindowSize(ImVec2(260.f, 0.f), ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.30f, 0.55f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.12f, 0.20f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.18f, 0.30f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.18f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.28f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.25f, 0.42f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.55f, 1.f));

            std::string winId = "Strategy##WizOverlay_" + strategy_.symbol;
            bool open = true;
            ImGui::Begin(winId.c_str(), &open,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoCollapse);

            if (!open)
            {
                cancelled_ = true;
                mode_ = Mode::Closed;
                ImGui::End();
                ImGui::PopStyleColor(8);
                ImGui::PopStyleVar(4);
                return;
            }

            DrawActiveWizard(candles, focusedCandle, totalCandles);

            ImGui::End();
            ImGui::PopStyleColor(8);
            ImGui::PopStyleVar(4);
        }

        void Draw(const ImVec2& chartMin, const ImVec2& chartMax,
                  int focusedCandle, int totalCandles,
                  const std::vector<Candle>* candles)
        {
            DrawOverlay(chartMin, chartMax, focusedCandle, totalCandles, candles);
        }

    private:
        float ComputeDefaultOffset(float price) const
        {
            if (visibleRange_ > 0.f)
                return visibleRange_ * 0.25f;
            return price * 0.02f;
        }

        Mode      mode_      = Mode::Closed;
        TimeMode  timeMode_  = TimeMode::Current;
        Strategy  strategy_;
        int       selectedCandle_ = -1;
        bool      confirmed_ = false;
        bool      cancelled_ = false;
        bool      firstOpen_ = true;
        float     visibleRange_ = 0.f;

        int                        chartClickCandle_       = -1;
        int                        chartClickTotalCandles_ = 0;
        const std::vector<Candle>* chartClickCandles_      = nullptr;

        std::string focusedSymbol_;
        float       focusedPrice_ = 0.f;

        char symbolBuf_[64] = "";
        char notesBuf_[256] = "";

        void DrawTPSLFields()
        {
            ImGui::TextDisabled("Take Profit");
            ImGui::SetNextItemWidth(-50.f);
            ImGui::InputFloat("##wizTP", &strategy_.takeProfit, 0.f, 0.f, "%.4f");
            if (strategy_.takeProfit > 0.f && strategy_.entryPrice > 0.f)
            {
                float pct = strategy_.TPPercent();
                ImGui::SameLine();
                ImVec4 col = pct >= 0.f ? ImVec4(0.3f, 0.9f, 0.5f, 1.f) : ImVec4(0.9f, 0.3f, 0.3f, 1.f);
                ImGui::TextColored(col, "%+.2f%%", pct);
            }

            ImGui::TextDisabled("Stop Loss");
            ImGui::SetNextItemWidth(-50.f);
            ImGui::InputFloat("##wizSL", &strategy_.stopLoss, 0.f, 0.f, "%.4f");
            if (strategy_.stopLoss > 0.f && strategy_.entryPrice > 0.f)
            {
                float pct = strategy_.SLPercent();
                ImGui::SameLine();
                ImVec4 col = pct >= 0.f ? ImVec4(0.3f, 0.9f, 0.5f, 1.f) : ImVec4(0.9f, 0.3f, 0.3f, 1.f);
                ImGui::TextColored(col, "%+.2f%%", pct);
            }

            float rr = strategy_.RiskReward();
            if (rr > 0.f)
            {
                ImVec4 rrCol = rr >= 2.f ? ImVec4(0.3f, 0.9f, 0.5f, 1.f)
                             : rr >= 1.f ? ImVec4(0.9f, 0.8f, 0.3f, 1.f)
                             : ImVec4(0.9f, 0.3f, 0.3f, 1.f);
                ImGui::TextColored(rrCol, "R:R  %.2f", rr);
            }
        }

        void DrawIdleState()
        {
            const std::string& activeSymbol = !focusedSymbol_.empty() ? focusedSymbol_
                : (symbolBuf_[0] ? std::string(symbolBuf_) : std::string());
            float activePrice = focusedPrice_;

            if (!activeSymbol.empty())
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                float availW = ImGui::GetContentRegionAvail().x;

                dl->AddRectFilled(
                    ImVec2(cursor.x - 4, cursor.y),
                    ImVec2(cursor.x + availW + 4, cursor.y + 36.f),
                    IM_COL32(20, 25, 40, 255), 6.f);
                dl->AddRect(
                    ImVec2(cursor.x - 4, cursor.y),
                    ImVec2(cursor.x + availW + 4, cursor.y + 36.f),
                    IM_COL32(60, 100, 180, 120), 6.f);

                ImGui::SetCursorScreenPos(ImVec2(cursor.x + 8, cursor.y + 4));
                ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.f, 1.f), "%s", activeSymbol.c_str());
                if (activePrice > 0.f)
                {
                    ImGui::SameLine();
                    char pb[32]; FmtPrice(pb, sizeof(pb), activePrice, activeSymbol);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.f), "@ %s", pb);
                }
                ImGui::SameLine(availW - 30.f);
                ImGui::TextDisabled("LIVE");

                ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + 42.f));
            }
            else
            {
                ImGui::TextDisabled("No chart focused.");
                ImGui::TextDisabled("Open a chart to create strategies.");
                return;
            }

            ImGui::Spacing();

            float w = ImGui::GetContentRegionAvail().x;
            float btnW = (w - 6.f) * 0.5f;
            float btnH = 34.f;

            auto QuickBtn = [&](const char* label, const char* icon, StrategyType type,
                               StrategyDirection dir, ImVec4 bgCol, ImVec4 hoverCol) {
                ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverCol);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(hoverCol.x + 0.05f, hoverCol.y + 0.05f, hoverCol.z + 0.05f, 1.f));

                char fullLabel[64];
                snprintf(fullLabel, sizeof(fullLabel), "%s %s", icon, label);
                if (ImGui::Button(fullLabel, ImVec2(btnW, btnH)))
                    OpenCreate(activeSymbol, type, dir, activePrice);

                ImGui::PopStyleColor(3);
            };

            ImGui::TextDisabled("TP / SL Strategy");
            QuickBtn("Long", "\xe2\x96\xb2", StrategyType::TPSL, StrategyDirection::Long,
                     {0.08f, 0.32f, 0.18f, 1.f}, {0.12f, 0.42f, 0.22f, 1.f});
            ImGui::SameLine(0.f, 6.f);
            QuickBtn("Short", "\xe2\x96\xbc", StrategyType::TPSL, StrategyDirection::Short,
                     {0.35f, 0.08f, 0.08f, 1.f}, {0.45f, 0.12f, 0.12f, 1.f});

            ImGui::Spacing();

            ImGui::TextDisabled("Position Tracking");
            QuickBtn("Long", "\xe2\x96\xb2", StrategyType::Position, StrategyDirection::Long,
                     {0.08f, 0.20f, 0.32f, 1.f}, {0.12f, 0.28f, 0.42f, 1.f});
            ImGui::SameLine(0.f, 6.f);
            QuickBtn("Short", "\xe2\x96\xbc", StrategyType::Position, StrategyDirection::Short,
                     {0.30f, 0.08f, 0.22f, 1.f}, {0.40f, 0.12f, 0.28f, 1.f});

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("Or right-click on a chart candle for precise entry.");
        }

        void DrawActiveWizard(const std::vector<Candle>* candles,
                              int focusedCandle, int totalCandles)
        {
            if (mode_ == Mode::Create)
            {
                int typeInt = (int)strategy_.type;
                float w = ImGui::GetContentRegionAvail().x;
                float btnW = (w - 8.f) / 3.f;

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 6));
                for (int i = 0; i < 3; i++)
                {
                    if (i > 0) ImGui::SameLine(0.f, 4.f);
                    bool sel = (typeInt == i);
                    if (sel)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.35f, 0.6f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.4f, 0.7f, 1.f));
                    }
                    const char* labels[] = {"TP/SL", "Position", "AI"};
                    if (ImGui::Button(labels[i], ImVec2(btnW, 0)))
                    {
                        if ((StrategyType)i != strategy_.type)
                        {
                            strategy_.type = (StrategyType)i;
                            if (strategy_.type != StrategyType::TPSL)
                            {
                                strategy_.takeProfit = 0.f;
                                strategy_.stopLoss = 0.f;
                            }
                            else if (strategy_.entryPrice > 0.f)
                            {
                                float offset = ComputeDefaultOffset(strategy_.entryPrice);
                                if (strategy_.direction == StrategyDirection::Long)
                                {
                                    strategy_.takeProfit = strategy_.entryPrice + offset;
                                    strategy_.stopLoss   = strategy_.entryPrice - offset;
                                }
                                else
                                {
                                    strategy_.takeProfit = strategy_.entryPrice - offset;
                                    strategy_.stopLoss   = strategy_.entryPrice + offset;
                                }
                            }
                        }
                    }
                    if (sel) ImGui::PopStyleColor(2);
                }
                ImGui::PopStyleVar();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }
            else
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();

                ImU32 headerCol = IM_COL32(200, 140, 40, 255);
                ImU32 headerBg  = IM_COL32(60, 40, 15, 255);
                float barH = 28.f;
                dl->AddRectFilled(ImVec2(cursor.x - 4, cursor.y),
                                  ImVec2(cursor.x + avail.x + 4, cursor.y + barH),
                                  headerBg, 4.f);
                dl->AddRect(ImVec2(cursor.x - 4, cursor.y),
                            ImVec2(cursor.x + avail.x + 4, cursor.y + barH),
                            headerCol, 4.f, 0, 1.5f);

                ImVec4 typeCol = strategy_.IsTPSL() ? ImVec4(0.4f, 0.7f, 1.f, 1.f)
                               : strategy_.IsPosition() ? ImVec4(0.4f, 1.f, 0.6f, 1.f)
                               : ImVec4(0.9f, 0.6f, 1.f, 1.f);

                ImGui::SetCursorScreenPos(ImVec2(cursor.x + 6, cursor.y + 5));
                ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), "EDITING");
                ImGui::SameLine();
                ImGui::TextColored(typeCol, "%s", StrategyTypeToString(strategy_.type));
                ImGui::SameLine();
                ImGui::TextDisabled("#%lld  %s", (long long)strategy_.id, strategy_.symbol.c_str());

                ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + barH + 6));
                ImGui::Separator();
                ImGui::Spacing();
            }



            if (mode_ == Mode::Create)
            {
                int tm = (int)timeMode_;
                ImGui::TextDisabled("Timing");
                if (ImGui::RadioButton("Current##tm", &tm, 0))
                {
                    timeMode_ = TimeMode::Current;
                    selectedCandle_ = -1;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Historical##tm", &tm, 1))
                {
                    timeMode_ = TimeMode::Historical;
                    selectedCandle_ = -1;
                }

                if (timeMode_ == TimeMode::Historical)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.4f, 1.f));
                    ImGui::TextWrapped("Click on chart candle to set entry");
                    ImGui::PopStyleColor();

                    if (selectedCandle_ >= 0 && candles && selectedCandle_ < (int)candles->size())
                    {
                        auto& c = (*candles)[selectedCandle_];
                        time_t ts = (time_t)c.timestamp;
                        struct tm t;
#ifdef _WIN32
                        localtime_s(&t, &ts);
#else
                        localtime_r(&ts, &t);
#endif
                        char dateBuf[32];
                        strftime(dateBuf, sizeof(dateBuf), "%b %d, %Y", &t);
                        { char pb[32]; FmtPrice(pb, sizeof(pb), c.close, strategy_.symbol);
                        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1.f),
                            "%s @ %s", dateBuf, pb); }
                    }
                }
                ImGui::Spacing();
            }



            if (mode_ == Mode::Create)
            {
                ImGui::TextDisabled("Symbol");
                ImGui::SetNextItemWidth(-1.f);
                ImGui::InputText("##wizSym", symbolBuf_, sizeof(symbolBuf_));
            }



            ImGui::TextDisabled("Direction");
            {
                int dir = (int)strategy_.direction;
                float w = ImGui::GetContentRegionAvail().x;
                float btnW = (w - 4.f) / 2.f;

                bool isLong = (dir == 0);
                if (isLong)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.2f, 1.f));
                if (ImGui::Button("Long##dir", ImVec2(btnW, 0)))
                    strategy_.direction = StrategyDirection::Long;
                if (isLong)
                    ImGui::PopStyleColor();

                ImGui::SameLine(0.f, 4.f);

                bool isShort = (dir == 1);
                if (isShort)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.f));
                if (ImGui::Button("Short##dir", ImVec2(btnW, 0)))
                    strategy_.direction = StrategyDirection::Short;
                if (isShort)
                    ImGui::PopStyleColor();
            }

            ImGui::Spacing();



            ImGui::TextDisabled("Entry");
            ImGui::SetNextItemWidth(-1.f);
            if (timeMode_ == TimeMode::Historical && selectedCandle_ >= 0)
            {
                ImGui::TextColored(ImVec4(1.f, 0.9f, 0.5f, 1.f), "%.4f", strategy_.entryPrice);
            }
            else
            {
                ImGui::InputFloat("##wizEntry", &strategy_.entryPrice, 0.f, 0.f, "%.4f");
            }



            if (strategy_.type == StrategyType::TPSL)
            {
                ImGui::Spacing();
                DrawTPSLFields();
            }
            else if (strategy_.type == StrategyType::Position)
            {
                ImGui::Spacing();
                bool hasTargets = strategy_.takeProfit > 0.f || strategy_.stopLoss > 0.f;
                if (!hasTargets)
                {
                    if (ImGui::SmallButton("+ Add TP/SL targets"))
                    {
                        float offset = strategy_.entryPrice > 0.f ? ComputeDefaultOffset(strategy_.entryPrice) : 0.f;
                        if (strategy_.direction == StrategyDirection::Long)
                        {
                            strategy_.takeProfit = strategy_.entryPrice + offset;
                            strategy_.stopLoss   = strategy_.entryPrice - offset;
                        }
                        else
                        {
                            strategy_.takeProfit = strategy_.entryPrice - offset;
                            strategy_.stopLoss   = strategy_.entryPrice + offset;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(optional)");
                }
                else
                {
                    ImGui::TextDisabled("TP/SL Targets");
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.f);
                    if (ImGui::SmallButton("Clear"))
                    {
                        strategy_.takeProfit = 0.f;
                        strategy_.stopLoss   = 0.f;
                    }
                    DrawTPSLFields();
                }
            }

            // === Quantity ===
            ImGui::Spacing();
            ImGui::TextDisabled("Quantity");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputFloat("##wizQty", &strategy_.quantity, 1.f, 10.f, "%.1f");

            // === Fees ===
            ImGui::Spacing();
            ImGui::TextDisabled("Fees");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputFloat("##wizEFee", &strategy_.entryFee, 0.f, 0.f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Entry fee (total commission)");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputFloat("##wizXFee", &strategy_.exitFee, 0.f, 0.f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exit fee (total commission)");

            // === Notes ===
            ImGui::TextDisabled("Notes");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputText("##wizNotes", notesBuf_, sizeof(notesBuf_));

            // === Actions ===
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool canConfirm = strategy_.entryPrice > 0.f && symbolBuf_[0] != '\0';
            if (timeMode_ == TimeMode::Historical && selectedCandle_ < 0 && mode_ == Mode::Create)
                canConfirm = false;

            // Confirm button (green)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.45f, 0.25f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.55f, 0.30f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.3f, 1.f));

                if (!canConfirm) ImGui::BeginDisabled();

                const char* confirmLabel = (mode_ == Mode::Create)
                    ? (timeMode_ == TimeMode::Historical ? "Record" : "Create")
                    : "Save";

                if (ImGui::Button(confirmLabel, ImVec2(-1.f, 28.f)))
                {
                    strategy_.symbol = symbolBuf_;
                    strategy_.notes  = notesBuf_;

                    if (mode_ == Mode::Create)
                    {
                        strategy_.createdAt = std::time(nullptr);
                        if (timeMode_ == TimeMode::Historical && selectedCandle_ >= 0 &&
                            candles && selectedCandle_ < (int)candles->size())
                        {
                            strategy_.entryDate = (*candles)[selectedCandle_].timestamp;
                        }
                        else
                        {
                            // Live/now mode: entry date is the current moment
                            strategy_.entryDate = std::time(nullptr);
                        }
                    }

                    confirmed_ = true;
                    mode_ = Mode::Closed;
                }

                if (!canConfirm) ImGui::EndDisabled();
                ImGui::PopStyleColor(3);
            }

            // Cancel button (subtle)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.12f, 0.12f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.15f, 0.15f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.f));

                if (ImGui::Button("Cancel", ImVec2(-1.f, 24.f)))
                {
                    cancelled_ = true;
                    mode_ = Mode::Closed;
                }
                ImGui::PopStyleColor(3);
            }
        }
    };

} // namespace stnks
