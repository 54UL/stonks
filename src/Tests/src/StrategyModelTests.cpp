#include <gtest/gtest.h>
#include <Strategy/Strategy.hpp>

using namespace stnks;

// ── RiskReward ──────────────────────────────────────────────────────────────

TEST(StrategyModel, RiskRewardLong)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    s.stopLoss   = 95.f;
    // reward = 10, risk = 5, R:R = 2.0
    EXPECT_FLOAT_EQ(s.RiskReward(), 2.0f);
}

TEST(StrategyModel, RiskRewardShort)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.takeProfit = 90.f;
    s.stopLoss   = 105.f;
    // reward = 10, risk = 5, R:R = 2.0
    EXPECT_FLOAT_EQ(s.RiskReward(), 2.0f);
}

TEST(StrategyModel, RiskRewardZeroRisk)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    s.stopLoss   = 100.f;  // risk = 0
    EXPECT_FLOAT_EQ(s.RiskReward(), 0.0f);
}

// ── TPPercent / SLPercent ───────────────────────────────────────────────────

TEST(StrategyModel, TPPercentLong)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    EXPECT_FLOAT_EQ(s.TPPercent(), 10.0f);
}

TEST(StrategyModel, TPPercentShort)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.takeProfit = 90.f;
    EXPECT_FLOAT_EQ(s.TPPercent(), -10.0f);
}

TEST(StrategyModel, SLPercentLong)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.stopLoss   = 95.f;
    EXPECT_FLOAT_EQ(s.SLPercent(), -5.0f);
}

TEST(StrategyModel, SLPercentShort)
{
    Strategy s;
    s.entryPrice = 100.f;
    s.stopLoss   = 103.f;
    EXPECT_FLOAT_EQ(s.SLPercent(), 3.0f);
}

TEST(StrategyModel, PercentZeroEntry)
{
    Strategy s;
    s.entryPrice = 0.f;
    s.takeProfit = 10.f;
    s.stopLoss   = 5.f;
    EXPECT_FLOAT_EQ(s.TPPercent(), 0.0f);
    EXPECT_FLOAT_EQ(s.SLPercent(), 0.0f);
}

// ── Status ──────────────────────────────────────────────────────────────────

TEST(StrategyModel, IsActive)
{
    Strategy s;
    s.status = StrategyStatus::Active;
    EXPECT_TRUE(s.IsActive());

    s.status = StrategyStatus::TPHit;
    EXPECT_FALSE(s.IsActive());

    s.status = StrategyStatus::SLHit;
    EXPECT_FALSE(s.IsActive());

    s.status = StrategyStatus::Cancelled;
    EXPECT_FALSE(s.IsActive());
}

// ── String conversions ──────────────────────────────────────────────────────

TEST(StrategyModel, StatusToString)
{
    EXPECT_STREQ(StatusToString(StrategyStatus::Active), "Active");
    EXPECT_STREQ(StatusToString(StrategyStatus::TPHit), "TP Hit");
    EXPECT_STREQ(StatusToString(StrategyStatus::SLHit), "SL Hit");
    EXPECT_STREQ(StatusToString(StrategyStatus::Cancelled), "Cancelled");
}

TEST(StrategyModel, DirectionToString)
{
    EXPECT_STREQ(DirectionToString(StrategyDirection::Long), "Long");
    EXPECT_STREQ(DirectionToString(StrategyDirection::Short), "Short");
}
