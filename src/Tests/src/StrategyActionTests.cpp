#include <gtest/gtest.h>
#include <Server/IStrategyAction.hpp>
#include <Server/BrokerAction.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <Http/HttpClient.hpp>
#include <vector>

using namespace stnks;

// ── Mock action that records all events ─────────────────────────────────────

class MockAction : public IStrategyAction
{
public:
    std::string Name() const override { return "MockAction"; }

    bool Execute(const StrategyTriggerEvent& event) override
    {
        events.push_back({
            event.strategy.id,
            event.symbol,
            event.triggerType,
            event.currentPrice
        });
        return shouldSucceed;
    }

    bool Validate() override { return validateResult; }

    struct RecordedEvent
    {
        int64_t        strategyId;
        std::string    symbol;
        StrategyStatus triggerType;
        float          currentPrice;
    };

    std::vector<RecordedEvent> events;
    bool shouldSucceed   = true;
    bool validateResult  = true;
};

// ── Failing action ──────────────────────────────────────────────────────────

class FailingAction : public IStrategyAction
{
public:
    std::string Name() const override { return "FailingAction"; }
    bool Execute(const StrategyTriggerEvent&) override { return false; }
};

class ThrowingAction : public IStrategyAction
{
public:
    std::string Name() const override { return "ThrowingAction"; }
    bool Execute(const StrategyTriggerEvent&) override
    {
        throw std::runtime_error("action exploded");
    }
};

// ── Tests ───────────────────────────────────────────────────────────────────

TEST(StrategyAction, MockActionRecordsEvents)
{
    MockAction action;

    Strategy s;
    s.id         = 42;
    s.symbol     = "AAPL";
    s.entryPrice = 150.f;
    s.takeProfit = 165.f;
    s.stopLoss   = 140.f;

    StrategyTriggerEvent event{s, StrategyStatus::TPHit, 166.f, "AAPL", 1000};

    bool result = action.Execute(event);
    EXPECT_TRUE(result);
    ASSERT_EQ(action.events.size(), 1u);
    EXPECT_EQ(action.events[0].strategyId, 42);
    EXPECT_EQ(action.events[0].symbol, "AAPL");
    EXPECT_EQ(action.events[0].triggerType, StrategyStatus::TPHit);
    EXPECT_FLOAT_EQ(action.events[0].currentPrice, 166.f);
}

TEST(StrategyAction, MockActionCanFail)
{
    MockAction action;
    action.shouldSucceed = false;

    Strategy s;
    s.id = 1;
    StrategyTriggerEvent event{s, StrategyStatus::SLHit, 100.f, "MSFT", 1000};

    EXPECT_FALSE(action.Execute(event));
}

TEST(StrategyAction, FailingActionReturnsFalse)
{
    FailingAction action;
    Strategy s;
    s.id = 1;
    StrategyTriggerEvent event{s, StrategyStatus::TPHit, 100.f, "X", 0};
    EXPECT_FALSE(action.Execute(event));
}

TEST(StrategyAction, ThrowingActionThrows)
{
    ThrowingAction action;
    Strategy s;
    s.id = 1;
    StrategyTriggerEvent event{s, StrategyStatus::TPHit, 100.f, "X", 0};
    EXPECT_THROW(action.Execute(event), std::runtime_error);
}

TEST(StrategyAction, ValidateDefault)
{
    MockAction action;
    EXPECT_TRUE(action.Validate());

    action.validateResult = false;
    EXPECT_FALSE(action.Validate());
}

// ── BrokerAction dry-run tests ──────────────────────────────────────────────

class BrokerActionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        threads_ = std::make_unique<ThreadRegistry>(1);
        http_    = std::make_unique<HttpClient>(*threads_);
    }

    void TearDown() override
    {
        http_.reset();
        threads_.reset();
    }

    std::unique_ptr<ThreadRegistry> threads_;
    std::unique_ptr<HttpClient>     http_;
};

TEST_F(BrokerActionTest, DryRunValidates)
{
    BrokerAction::Config config;
    // No API key = dry-run
    BrokerAction action(*http_, config);

    EXPECT_TRUE(action.Validate());
    EXPECT_EQ(action.Name(), "BrokerAction (GBM)");
}

TEST_F(BrokerActionTest, DryRunExecuteSucceeds)
{
    BrokerAction::Config config;
    BrokerAction action(*http_, config);

    Strategy s;
    s.id         = 10;
    s.symbol     = "AAPL";
    s.direction  = StrategyDirection::Long;
    s.entryPrice = 150.f;
    s.takeProfit = 165.f;
    s.stopLoss   = 140.f;

    StrategyTriggerEvent event{s, StrategyStatus::TPHit, 166.5f, "AAPL", 1000};
    EXPECT_TRUE(action.Execute(event));
}

TEST_F(BrokerActionTest, DryRunSLExecuteSucceeds)
{
    BrokerAction::Config config;
    BrokerAction action(*http_, config);

    Strategy s;
    s.id         = 11;
    s.symbol     = "TSLA";
    s.direction  = StrategyDirection::Short;
    s.entryPrice = 200.f;
    s.takeProfit = 180.f;
    s.stopLoss   = 210.f;

    StrategyTriggerEvent event{s, StrategyStatus::SLHit, 211.f, "TSLA", 1000};
    EXPECT_TRUE(action.Execute(event));
}

// ── Multiple actions in chain ───────────────────────────────────────────────

TEST(StrategyAction, MultipleActionsAllFire)
{
    std::vector<std::unique_ptr<IStrategyAction>> actions;
    auto* mock1 = new MockAction();
    auto* mock2 = new MockAction();
    actions.emplace_back(mock1);
    actions.emplace_back(mock2);

    Strategy s;
    s.id     = 5;
    s.symbol = "NVDA";
    StrategyTriggerEvent event{s, StrategyStatus::TPHit, 500.f, "NVDA", 0};

    for (auto& action : actions)
        action->Execute(event);

    EXPECT_EQ(mock1->events.size(), 1u);
    EXPECT_EQ(mock2->events.size(), 1u);
}
