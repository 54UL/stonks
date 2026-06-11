#pragma once

// Legacy header — all SQL queries are now generated from the strategy schema.
// See StrategySchema.hpp for the single source of truth.
//
// This header exists for backward compatibility. New code should use
// sql::Insert(), sql::Update(), etc. from StrategySchema.hpp directly.

#include <Db/StrategySchema.hpp>
