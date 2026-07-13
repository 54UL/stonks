@echo off
REM STNKS Environment Variables Template (Windows)
REM Copy to .env.bat, fill in your keys, then run: .env.bat

REM Root directory for assets/config/database. Unset = cwd.
set "ASSETS_STNKS="

REM Set = remote mode (http://host:port), unset = monolith mode
set "STNKS_SERVER_URL="

REM AI & News
set "CLAUDE_API_KEY="
set "GNEWS_API_KEY="

REM Binance
set "BINANCE_API_KEY="
set "BINANCE_API_SECRET="
set "BINANCE_SANDBOX=1"

REM MetaTrader 5 (via MetaApi)
set "MT5_API_KEY="
set "MT5_ACCOUNT_ID="
set "MT5_SERVER=MetaQuotes-Demo"
set "MT5_LOGIN="
set "MT5_PASSWORD="
