@echo off
REM Usage:
REM   dev_env\build.bat              Build GUI (default)
REM   dev_env\build.bat --server     Build headless server
REM   dev_env\build.bat --tests      Build tests
REM   dev_env\build.bat --all        Build everything
REM   dev_env\build.bat --clean      Clean + reconfigure
REM   dev_env\build.bat --release    Release mode (default: Debug)
REM
REM Flags can be combined: dev_env\build.bat --server --release --clean
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "SRC_DIR=%PROJECT_ROOT%\src"
set "BUILD_DIR=%SRC_DIR%\build"
set "BUILD_TYPE=Debug"
set "TARGET=Entry"
set "HEADLESS=OFF"
set "CLEAN=0"
set "NPROC=%NUMBER_OF_PROCESSORS%"

:parse_args
if "%~1"=="" goto done_args
if "%~1"=="--server"  (set "TARGET=STNKS_SERVER" & set "HEADLESS=ON" & shift & goto parse_args)
if "%~1"=="--tests"   (set "TARGET=STNKS_TESTS" & shift & goto parse_args)
if "%~1"=="--all"     (set "TARGET=all" & shift & goto parse_args)
if "%~1"=="--clean"   (set "CLEAN=1" & shift & goto parse_args)
if "%~1"=="--release" (set "BUILD_TYPE=Release" & shift & goto parse_args)
echo Unknown option: %~1
exit /b 1
:done_args

if "%VCPKG_ROOT%"=="" (
    echo ERROR: VCPKG_ROOT is not set. Run setup-dev.bat or set it manually.
    exit /b 1
)

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo [build] Cleaning %BUILD_DIR% ...
        rmdir /s /q "%BUILD_DIR%"
    )
)

if not exist "%BUILD_DIR%" (
    echo [build] Configuring (%BUILD_TYPE%, HEADLESS=%HEADLESS%) ...
    mkdir "%BUILD_DIR%"

    set "OVERLAY_ARG="
    if exist "%PROJECT_ROOT%\overlay-ports" (
        set "OVERLAY_ARG=-DVCPKG_OVERLAY_PORTS=%PROJECT_ROOT%\overlay-ports"
    )

    cmake -S "%SRC_DIR%" -B "%BUILD_DIR%" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
        !OVERLAY_ARG! ^
        -DSTNKS_HEADLESS=%HEADLESS%

    if errorlevel 1 (
        echo [build] Configure FAILED.
        exit /b 1
    )
)

echo [build] Building target=%TARGET% jobs=%NPROC% ...
cmake --build "%BUILD_DIR%" --target %TARGET% -j %NPROC%

if errorlevel 1 (
    echo [build] Build FAILED.
    exit /b 1
)

echo [build] Done.
endlocal
