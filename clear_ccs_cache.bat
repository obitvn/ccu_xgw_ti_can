@echo off
REM Clear CCS Cache Script
REM Use this if Core3 doesn't halt on subsequent debug sessions

echo ============================================
echo Clear CCS Cache
echo ============================================
echo.
echo This script will clear CCS debug cache
echo Close CCS before running this script!
echo.

pause

echo.
echo Clearing CCS debug cache...
echo.

REM Clear CDT DSF cache (debug session state)
set CCS_CACHE=%USERPROFILE%\.ccs\workspace\.metadata\.plugins\org.eclipse.cdt.dsf
if exist "%CCS_CACHE%" (
    echo Found: %CCS_CACHE%
    rd /s /q "%CCS_CACHE%"
    echo [OK] DSF cache cleared
) else (
    echo [SKIP] DSF cache not found
)

echo.

REM Clear CDT Core cache
set CCS_CORE=%USERPROFILE%\.ccs\workspace\.metadata\.plugins\org.eclipse.cdt.core
if exist "%CCS_CORE%" (
    echo Found: %CCS_CORE%
    rd /s /q "%CCS_CORE%"
    echo [OK] Core cache cleared
) else (
    echo [SKIP] Core cache not found
)

echo.
echo ============================================
echo Done! Please restart CCS Theia.
echo ============================================
echo.
pause
