@echo off
REM One-time dev environment setup: checks prerequisites and bootstraps vcpkg.
setlocal

set "MISSING="
where cmake >nul 2>&1 || set "MISSING=!MISSING! cmake"
where git >nul 2>&1   || set "MISSING=!MISSING! git"

setlocal enabledelayedexpansion
if not "!MISSING!"=="" (
    echo Missing prerequisites:!MISSING!
    echo Install them and ensure they are on PATH.
    echo   cmake: https://cmake.org/download/
    echo   git:   https://git-scm.com/download/win
    exit /b 1
)
endlocal

if "%VCPKG_ROOT%"=="" (
    set "DEFAULT_VCPKG=%USERPROFILE%\.vcpkg"
    echo VCPKG_ROOT is not set. Cloning vcpkg to %USERPROFILE%\.vcpkg ...
    if not exist "%USERPROFILE%\.vcpkg" (
        git clone https://github.com/microsoft/vcpkg.git "%USERPROFILE%\.vcpkg"
    )
    call "%USERPROFILE%\.vcpkg\bootstrap-vcpkg.bat" -disableMetrics
    set "VCPKG_ROOT=%USERPROFILE%\.vcpkg"
    echo.
    echo Set VCPKG_ROOT permanently:
    echo   setx VCPKG_ROOT "%USERPROFILE%\.vcpkg"
) else (
    echo VCPKG_ROOT is set to: %VCPKG_ROOT%
    if not exist "%VCPKG_ROOT%\vcpkg.exe" (
        echo Bootstrapping vcpkg ...
        call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    )
)

echo.
echo Setup complete. Next steps:
echo   1. copy dev_env\setup_env.bat .env.bat   (edit with your API keys)
echo   2. .env.bat
echo   3. dev_env\build.bat                      (build GUI)
echo   4. dev_env\build.bat --server             (build headless server)
endlocal
