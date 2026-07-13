#!/bin/bash
# STNKS Environment Variables Template
# Copy to .env.sh, fill in your keys, then: source .env.sh

# Root directory for assets/config/database. Unset = cwd.
export ASSETS_STNKS=""

# Set = remote mode (http://host:port), unset = monolith mode
export STNKS_SERVER_URL=""

# AI & News
export CLAUDE_API_KEY=""
export GNEWS_API_KEY=""

# Binance
export BINANCE_API_KEY=""
export BINANCE_API_SECRET=""
export BINANCE_SANDBOX="1"

# MetaTrader 5 (via MetaApi)
export MT5_API_KEY=""
export MT5_ACCOUNT_ID=""
export MT5_SERVER="MetaQuotes-Demo"
export MT5_LOGIN=""
export MT5_PASSWORD=""
