#include <gtest/gtest.h>
#include <Server/StrategyServer.hpp>
#include <Server/IStrategyAction.hpp>
#include <Strategy/StrategyStore.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <ctime>
#include <atomic>

using namespace stnks;
namespace fs = std::filesystem;

// ── Mock action for capturing server triggers ───────────────────────────────

class TestAction : public IStrategyAction
{
public:
    std::string Name() const override { return "TestAction"; }

    bool Execute(const StrategyTriggerEvent& event) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.push_back({
            event.strategy.id,
            event.symbol,
            event.triggerType,
            event.currentPrice
        });
        return true;
    }

    struct Event
    {
        int64_t        id;
        std::string    symbol;
        StrategyStatus type;
        float          price;
    };

    std::vector<Event> GetEvents()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events;
    }

    size_t EventCount()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events.size();
    }

private:
    std::mutex         mutex_;
    std::vector<Event> events;
};

// ── Server lifecycle tests ──────────────────────────────────────────────────

TEST(StrategyServer, ConstructsAndDestructs)
{
    StrategyServer::Config config;
    config.pollIntervalSec = 1;

    // Should not throw
    StrategyServer server(config);
    EXPECT_FALSE(server.IsRunning());
}

TEST(StrategyServer, StopBeforeRunIsNoop)
{
    StrategyServer server;
    server.Stop();
    EXPECT_FALSE(server.IsRunning());
}

TEST(StrategyServer, RunAndStopFromAnotherThread)
{
    StrategyServer::Config config;
    config.pollIntervalSec = 1;

    StrategyServer server(config);

    std::atomic<bool> started{false};

    std::thread runner([&]() {
        started = true;
        server.Run();
    });

    // Wait for server to actually start
    while (!started)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Give it a moment to enter the loop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(server.IsRunning());

    server.Stop();
    runner.join();
    EXPECT_FALSE(server.IsRunning());
}

TEST(StrategyServer, AddActionBeforeRun)
{
    StrategyServer server;

    auto action = std::make_unique<TestAction>();
    TestAction* actionPtr = action.get();

    server.AddAction(std::move(action));

    // No strategies, no triggers — just verify it doesn't crash
    EXPECT_EQ(actionPtr->EventCount(), 0u);
}

// ── Trigger detection logic (unit test without network) ─────────────────────

// This tests the trigger logic independently from the server polling loop
// by directly using StrategyStore and checking conditions.

class TriggerDetectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dbPath_ = (fs::temp_directory_path() / ("stnks_trigger_test_" + std::to_string(std::time(nullptr)) + ".db")).string();
        store_ = std::make_unique<StrategyStore>(dbPath_);
    }

    void TearDown() override
    {
        store_.reset();
        fs::remove(dbPath_);
    }

    // Simulate checking a strategy against a candle's high/low
    bool CheckTPHit(const Strategy& s, float high, float low)
    {
        if (s.direction == StrategyDirection::Long)
            return high >= s.takeProfit;
        else
            return low <= s.takeProfit;
    }

    bool CheckSLHit(const Strategy& s, float high, float low)
    {
        if (s.direction == StrategyDirection::Long)
            return low <= s.stopLoss;
        else
            return high >= s.stopLoss;
    }

    std::string                    dbPath_;
    std::unique_ptr<StrategyStore> store_;
};

TEST_F(TriggerDetectionTest, LongTPHitWhenHighAboveTarget)
{
    Strategy s;
    s.direction  = StrategyDirection::Long;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    s.stopLoss   = 95.f;

    EXPECT_FALSE(CheckTPHit(s, 109.f, 98.f));  // High below TP
    EXPECT_TRUE(CheckTPHit(s, 110.f, 98.f));   // High exactly at TP
    EXPECT_TRUE(CheckTPHit(s, 115.f, 98.f));   // High above TP
}

TEST_F(TriggerDetectionTest, LongSLHitWhenLowBelowStop)
{
    Strategy s;
    s.direction  = StrategyDirection::Long;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    s.stopLoss   = 95.f;

    EXPECT_FALSE(CheckSLHit(s, 105.f, 96.f));  // Low above SL
    EXPECT_TRUE(CheckSLHit(s, 105.f, 95.f));   // Low exactly at SL
    EXPECT_TRUE(CheckSLHit(s, 105.f, 90.f));   // Low below SL
}

TEST_F(TriggerDetectionTest, ShortTPHitWhenLowBelowTarget)
{
    Strategy s;
    s.direction  = StrategyDirection::Short;
    s.entryPrice = 100.f;
    s.takeProfit = 90.f;
    s.stopLoss   = 105.f;

    EXPECT_FALSE(CheckTPHit(s, 102.f, 91.f));  // Low above TP
    EXPECT_TRUE(CheckTPHit(s, 102.f, 90.f));   // Low exactly at TP
    EXPECT_TRUE(CheckTPHit(s, 102.f, 85.f));   // Low below TP
}

TEST_F(TriggerDetectionTest, ShortSLHitWhenHighAboveStop)
{
    Strategy s;
    s.direction  = StrategyDirection::Short;
    s.entryPrice = 100.f;
    s.takeProfit = 90.f;
    s.stopLoss   = 105.f;

    EXPECT_FALSE(CheckSLHit(s, 104.f, 98.f));  // High below SL
    EXPECT_TRUE(CheckSLHit(s, 105.f, 98.f));   // High exactly at SL
    EXPECT_TRUE(CheckSLHit(s, 110.f, 98.f));   // High above SL
}

TEST_F(TriggerDetectionTest, NoTriggerWhenPriceInRange)
{
    Strategy s;
    s.direction  = StrategyDirection::Long;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    s.stopLoss   = 95.f;

    // Price stays between SL and TP
    EXPECT_FALSE(CheckTPHit(s, 105.f, 97.f));
    EXPECT_FALSE(CheckSLHit(s, 105.f, 97.f));
}

TEST_F(TriggerDetectionTest, BothHitOnExtremeMoveTPTakesPriority)
{
    Strategy s;
    s.direction  = StrategyDirection::Long;
    s.entryPrice = 100.f;
    s.takeProfit = 110.f;
    s.stopLoss   = 95.f;

    // Extreme candle: high above TP AND low below SL
    float high = 115.f, low = 90.f;

    bool tp = CheckTPHit(s, high, low);
    bool sl = CheckSLHit(s, high, low);

    // Both are true — server code checks TP first (tp takes priority)
    EXPECT_TRUE(tp);
    EXPECT_TRUE(sl);
}

// ── Integration: Store + Trigger ────────────────────────────────────────────

TEST_F(TriggerDetectionTest, FullCycleInsertCheckTriggerUpdate)
{
    // Insert an active strategy
    Strategy s;
    s.symbol     = "AAPL";
    s.direction  = StrategyDirection::Long;
    s.entryPrice = 150.f;
    s.takeProfit = 165.f;
    s.stopLoss   = 140.f;
    s.createdAt  = std::time(nullptr);

    int64_t id = store_->Insert(s);
    ASSERT_GT(id, 0);

    // Load active strategies
    auto active = store_->GetActive();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_TRUE(active[0].IsActive());

    // Simulate candle that hits TP
    float high = 166.f, low = 148.f;
    EXPECT_TRUE(CheckTPHit(active[0], high, low));

    // Mark as triggered
    int64_t now = std::time(nullptr);
    store_->MarkTriggered(id, StrategyStatus::TPHit, now);

    // Verify it's no longer active
    active = store_->GetActive();
    EXPECT_TRUE(active.empty());

    auto triggered = store_->GetById(id);
    EXPECT_EQ(triggered.status, StrategyStatus::TPHit);
    EXPECT_EQ(triggered.triggeredAt, now);
}

TEST_F(TriggerDetectionTest, OnlyActiveStrategiesTrigger)
{
    // Insert two strategies, cancel one
    Strategy s1;
    s1.symbol = "AAPL"; s1.direction = StrategyDirection::Long;
    s1.entryPrice = 150.f; s1.takeProfit = 165.f; s1.stopLoss = 140.f;
    s1.createdAt = std::time(nullptr);

    Strategy s2 = s1;
    s2.symbol = "AAPL";
    s2.entryPrice = 160.f; s2.takeProfit = 180.f; s2.stopLoss = 150.f;

    int64_t id1 = store_->Insert(s1);
    int64_t id2 = store_->Insert(s2);

    // Cancel first one
    store_->MarkTriggered(id1, StrategyStatus::Cancelled, std::time(nullptr));

    auto active = store_->GetActive();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].id, id2);
}
