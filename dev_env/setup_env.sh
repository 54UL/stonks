#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# STNKS Environment Variables Template
# ═══════════════════════════════════════════════════════════════════════════════
# Copy this to .env.sh, fill in your keys, and source it before running.
# NEVER commit .env.sh to version control! (no seas estúpido)
#
# Usage:
#   cp dev_env/setup_env.sh .env.sh
#   # fill in your keys
#   source .env.sh
# ═══════════════════════════════════════════════════════════════════════════════

# ─── Core ───────────────────────────────────────────────────────────────────
# Root directory for assets, config, and database files (app/strategies.db lives here).
# Unset = current working directory.
export ASSETS_STNKS=""

# ─── Client-Server Mode ────────────────────────────────────────────────────
# Format: "http://host:port" (e.g. "http://localhost:8099")
# Set   = remote mode (UI connects to standalone stnks-server via HTTP + ENet)
# Unset = monolith mode (embedded server in background thread)
export STNKS_SERVER_URL=""

# ─── AI & News ─────────────────────────────────────────────────────────────
export CLAUDE_API_KEY=""
export GNEWS_API_KEY=""

# ─── Binance (Crypto) ─────────────────────────────────────────────────────
# https://www.binance.com/en/my/settings/api-management
# Testnet: https://testnet.binance.vision/
export BINANCE_API_KEY=""
export BINANCE_API_SECRET=""
export BINANCE_SANDBOX="1"   # 1=testnet, 0=production

# ─── GBM+ (Mexican Stock Market) ──────────────────────────────────────────
# https://developers.gbm.com
export GBM_CLIENT_ID=""
export GBM_CLIENT_SECRET=""
export GBM_REFRESH_TOKEN=""
export GBM_ACCOUNT_ID=""
export GBM_SANDBOX="1"       # 1=sandbox, 0=production
export GBM_WEBHOOK_SECRET=""
export GBM_WEBHOOK_PORT="0"  # 0=disabled

# ─── MetaTrader 5 (via MetaApi) ───────────────────────────────────────────
# https://metaapi.cloud
export MT5_API_KEY=""
export MT5_ACCOUNT_ID=""
export MT5_SERVER="MetaQuotes-Demo"
export MT5_LOGIN=""
export MT5_PASSWORD=""
