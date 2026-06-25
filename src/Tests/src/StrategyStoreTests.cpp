#include <gtest/gtest.h>
#include <Strategy/StrategyStore.hpp>
#include <filesystem>
#include <ctime>

using namespace stnks;
namespace fs = std::filesystem;

// Each test gets a fresh temporary DB
class StrategyStoreTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Use a unique temp file for each test
        dbPath_ = (fs::temp_directory_path() / ("stnks_test_" + std::to_string(std::time(nullptr)) + ".db")).string();
        store_ = std::make_unique<StrategyStore>(dbPath_);
    }

    void TearDown() override
    {
        store_.reset();  // Close DB first
        fs::remove(dbPath_);
    }

    Strategy MakeStrategy(const std::string& symbol, float entry, float tp, float sl,
                          StrategyDirection dir = StrategyDirection::Long)
    {
        Strategy s;
        s.symbol     = symbol;
        s.direction  = dir;
        s.entryPrice = entry;
        s.takeProfit = tp;
        s.stopLoss   = sl;
        s.createdAt  = std::time(nullptr);
        return s;
    }

    std::string                       dbPath_;
    std::unique_ptr<StrategyStore>    store_;
};

// ── Insert & GetById ────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, InsertAndRetrieve)
{
    auto s = MakeStrategy("AAPL", 150.f, 165.f, 142.f);
    s.notes = "test note";

    int64_t id = store_->Insert(s);
    ASSERT_GT(id, 0);

    auto retrieved = store_->GetById(id);
    EXPECT_EQ(retrieved.id, id);
    EXPECT_EQ(retrieved.symbol, "AAPL");
    EXPECT_FLOAT_EQ(retrieved.entryPrice, 150.f);
    EXPECT_FLOAT_EQ(retrieved.takeProfit, 165.f);
    EXPECT_FLOAT_EQ(retrieved.stopLoss, 142.f);
    EXPECT_EQ(retrieved.direction, StrategyDirection::Long);
    EXPECT_EQ(retrieved.status, StrategyStatus::Active);
    EXPECT_EQ(retrieved.notes, "test note");
}

TEST_F(StrategyStoreTest, InsertMultipleAutoIncrementIds)
{
    int64_t id1 = store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    int64_t id2 = store_->Insert(MakeStrategy("MSFT", 300.f, 330.f, 280.f));
    int64_t id3 = store_->Insert(MakeStrategy("TSLA", 200.f, 220.f, 180.f));

    ASSERT_GT(id1, 0);
    ASSERT_GT(id2, id1);
    ASSERT_GT(id3, id2);
}

// ── GetAll ──────────────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, GetAllEmpty)
{
    auto all = store_->GetAll();
    EXPECT_TRUE(all.empty());
}

TEST_F(StrategyStoreTest, GetAllReturnsInserted)
{
    store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    store_->Insert(MakeStrategy("MSFT", 300.f, 330.f, 280.f));

    auto all = store_->GetAll();
    EXPECT_GT(all.size(), 2u);
}

// ── GetBySymbol ─────────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, GetBySymbolFilters)
{
    store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    store_->Insert(MakeStrategy("AAPL", 155.f, 170.f, 145.f));
    store_->Insert(MakeStrategy("MSFT", 300.f, 330.f, 280.f));

    auto appl = store_->GetBySymbol("AAPL");
    EXPECT_GT(appl.size(), 2u);
    for (auto& s : appl)
        EXPECT_EQ(s.symbol, "AAPL");

    auto msft = store_->GetBySymbol("MSFT");
    EXPECT_EQ(msft.size(), 1u);

    auto none = store_->GetBySymbol("NVDA");
    EXPECT_TRUE(none.empty());
}

// ── GetActive ───────────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, GetActiveFiltersTriggered)
{
    int64_t id1 = store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    int64_t id2 = store_->Insert(MakeStrategy("MSFT", 300.f, 330.f, 280.f));
    store_->Insert(MakeStrategy("TSLA", 200.f, 220.f, 180.f));

    // Trigger one
    store_->MarkTriggered(id1, StrategyStatus::TPHit, std::time(nullptr));
    // Cancel another
    store_->MarkTriggered(id2, StrategyStatus::Cancelled, std::time(nullptr));

    auto active = store_->GetActive();
    EXPECT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].symbol, "TSLA");
}

// ── Update ──────────────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, UpdateModifiesPrices)
{
    int64_t id = store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));

    auto s = store_->GetById(id);
    s.entryPrice = 155.f;
    s.takeProfit = 170.f;
    s.stopLoss   = 148.f;
    s.notes      = "updated";

    bool ok = store_->Update(s);
    EXPECT_TRUE(ok);

    auto updated = store_->GetById(id);
    EXPECT_FLOAT_EQ(updated.entryPrice, 155.f);
    EXPECT_FLOAT_EQ(updated.takeProfit, 170.f);
    EXPECT_FLOAT_EQ(updated.stopLoss, 148.f);
    EXPECT_EQ(updated.notes, "updated");
}

// ── Delete ──────────────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, DeleteRemovesStrategy)
{
    int64_t id = store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    ASSERT_GT(id, 0);

    bool ok = store_->Delete(id);
    EXPECT_TRUE(ok);

    auto deleted = store_->GetById(id);
    EXPECT_EQ(deleted.id, 0);  // Default-constructed = not found
}

TEST_F(StrategyStoreTest, DeleteNonExistent)
{
    bool ok = store_->Delete(999);
    EXPECT_TRUE(ok);  // SQLite DELETE on non-existent is not an error
}

// ── MarkTriggered ───────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, MarkTriggeredTPHit)
{
    int64_t id = store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    int64_t now = std::time(nullptr);

    bool ok = store_->MarkTriggered(id, StrategyStatus::TPHit, now);
    EXPECT_TRUE(ok);

    auto s = store_->GetById(id);
    EXPECT_EQ(s.status, StrategyStatus::TPHit);
    EXPECT_EQ(s.triggeredAt, now);
}

TEST_F(StrategyStoreTest, MarkTriggeredSLHit)
{
    int64_t id = store_->Insert(MakeStrategy("AAPL", 150.f, 160.f, 140.f));
    int64_t now = std::time(nullptr);

    bool ok = store_->MarkTriggered(id, StrategyStatus::SLHit, now);
    EXPECT_TRUE(ok);

    auto s = store_->GetById(id);
    EXPECT_EQ(s.status, StrategyStatus::SLHit);
}

// ── Direction ───────────────────────────────────────────────────────────────

TEST_F(StrategyStoreTest, ShortDirectionPersists)
{
    auto s = MakeStrategy("TSLA", 200.f, 180.f, 210.f, StrategyDirection::Short);
    int64_t id = store_->Insert(s);

    auto retrieved = store_->GetById(id);
    EXPECT_EQ(retrieved.direction, StrategyDirection::Short);
}

// ── Persistence across reopen ───────────────────────────────────────────────

TEST_F(StrategyStoreTest, DataPersistsAcrossReopen)
{
    int64_t id = store_->Insert(MakeStrategy("AAPL", 150.f, 165.f, 142.f));
    ASSERT_GT(id, 0);

    // Close and reopen
    store_.reset();
    store_ = std::make_unique<StrategyStore>(dbPath_);

    auto all = store_->GetAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].symbol, "AAPL");
    EXPECT_FLOAT_EQ(all[0].entryPrice, 150.f);
}
