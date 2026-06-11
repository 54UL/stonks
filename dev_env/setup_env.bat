@echo off
REM ═══════════════════════════════════════════════════════════════════════════════
REM STNKS Environment Variables Template (Windows)
REM ═══════════════════════════════════════════════════════════════════════════════
REM Copy this to .env.bat, fill in your keys, and run it before launching.
REM NEVER commit .env.bat to version control! (no seas estupido)
REM
REM Usage:
REM   copy dev_env\setup_env.bat .env.bat
REM   REM fill in your keys
REM   .env.bat
REM ═══════════════════════════════════════════════════════════════════════════════

REM ─── Core ───────────────────────────────────────────────────────────────────
REM Root directory for assets, config, and database files (app\strategies.db lives here).
REM Unset = current working directory.
set "ASSETS_STNKS="

REM ─── Client-Server Mode ────────────────────────────────────────────────────
REM Format: "http://host:port" (e.g. "http://localhost:8099")
REM Set   = remote mode (UI connects to standalone stnks-server via HTTP + ENet)
REM Unset = monolith mode (embedded server in background thread)
set "STNKS_SERVER_URL="

REM ─── AI & News ─────────────────────────────────────────────────────────────
set "CLAUDE_API_KEY="
set "GNEWS_API_KEY="

REM ─── Binance (Crypto) ─────────────────────────────────────────────────────
REM https://www.binance.com/en/my/settings/api-management
REM Testnet: https://testnet.binance.vision/
set "BINANCE_API_KEY="
set "BINANCE_API_SECRET="
set "BINANCE_SANDBOX=1"

REM ─── GBM+ (Mexican Stock Market) ──────────────────────────────────────────
REM https://developers.gbm.com
set "GBM_CLIENT_ID="
set "GBM_CLIENT_SECRET="
set "GBM_REFRESH_TOKEN="
set "GBM_ACCOUNT_ID="
set "GBM_SANDBOX=1"
set "GBM_WEBHOOK_SECRET="
set "GBM_WEBHOOK_PORT=0"

REM ─── MetaTrader 5 (via MetaApi) ───────────────────────────────────────────
REM https://metaapi.cloud
set "MT5_API_KEY="
set "MT5_ACCOUNT_ID="
set "MT5_SERVER=MetaQuotes-Demo"
set "MT5_LOGIN="
set "MT5_PASSWORD="
