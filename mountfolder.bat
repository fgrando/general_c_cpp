@echo off
setlocal EnableDelayedExpansion

REM ============================================================
REM  mountfolder.bat
REM  Mounts a folder to the first available drive using SUBST.
REM
REM  Usage:
REM     mountfolder.bat <folder> <drive1> [drive2] [drive3] ...
REM
REM  Example:
REM     mountfolder.bat "C:\Users\Fernando\projects" X Y Z
REM
REM  Exit codes:
REM     0  mounted OK (prints current substitutions)
REM     1  folder does not exist
REM     2  folder is already mounted to a drive
REM     3  none of the informed drive letters is available
REM ============================================================

if "%~1"=="" (
    echo Usage: %~nx0 ^<folder^> ^<drive1^> [drive2] ... 1>&2
    endlocal & exit /b 1
)

REM Canonicalize the requested folder to a full path (no trailing slash)
set "FOLDER=%~f1"

REM --- (1) Folder must exist as a directory --------------------
if not exist "%FOLDER%\" (
    echo ERROR: folder "%FOLDER%" does not exist. 1>&2
    endlocal & exit /b 1
)

REM --- (2) Is this folder already mounted via SUBST? -----------
REM SUBST output line format:  Z:\: => C:\path\to\folder
for /f "tokens=1* delims==" %%a in ('subst') do (
    set "rest=%%b"
    REM strip the leading "> " (2 chars) that follows the '=' of '=>'
    set "tgt=!rest:~2!"
    if /i "!tgt!"=="%FOLDER%" (
        echo ERROR: folder is already mounted ^(%%a=^> !tgt!^). 1>&2
        endlocal & exit /b 2
    )
)

REM --- (3) Find the first available drive from the list --------
shift
set "MOUNTED="

:findloop
if "%~1"=="" goto nodrive
set "arg=%~1"
REM normalize "X" or "X:" -> "X:"
set "drv=!arg:~0,1!:"

REM Letter already in use (real, network or subst) -> skip it
if exist "!drv!\" (
    shift
    goto findloop
)

REM Letter looks free -> try to mount
subst !drv! "%FOLDER%" 2>nul
if not errorlevel 1 (
    set "MOUNTED=!drv!"
    goto done
)

REM subst failed even though the letter looked free -> try next
shift
goto findloop

:nodrive
echo ERROR: none of the informed drive letters is available. 1>&2
endlocal & exit /b 3

:done
echo Mounted "%FOLDER%" on !MOUNTED!
echo.
echo Current substitutions:
subst
endlocal & exit /b 0
