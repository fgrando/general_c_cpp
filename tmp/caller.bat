@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM  caller.bat - example of consuming mountfolder.bat
REM
REM  Contract relied upon:
REM     stdout = chosen drive letter only (e.g. X:)
REM     stderr = human messages (passed through to console)
REM     exit   = 0 ok / 1 no folder / 2 already mounted / 3 no drive
REM ============================================================

set "TARGET=C:\Users\fgrando\Downloads\general_c_cpp"
set "OUT=%TEMP%\mf.out"

REM Run child. Redirect only stdout to the temp file; stderr stays
REM on the console. Capture the exit code on the VERY NEXT line,
REM before any other command can overwrite errorlevel.
call mountfolder.bat "%TARGET%" X Y Z > "%OUT%"
set "RC=%errorlevel%"

REM Read the single line of stdout (the drive) back out.
set "CHOSEN="
if exist "%OUT%" (
    set /p CHOSEN=<"%OUT%"
    del "%OUT%" 2>nul
)

if "%RC%"=="0" (
    echo SUCCESS: folder is on drive !CHOSEN!
    REM Use !CHOSEN! from here, e.g.:  dir !CHOSEN!\
) else if "%RC%"=="1" (
    echo FAIL: folder does not exist.
) else if "%RC%"=="2" (
    echo FAIL: already mounted.
) else if "%RC%"=="3" (
    echo FAIL: no drive letter available.
) else (
    echo FAIL: unexpected code %RC%.
)

endlocal & exit /b %RC%