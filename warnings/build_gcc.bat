@echo off
setlocal enabledelayedexpansion
if not defined CC set CC=gcc
set CFLAGS=-I include -Wall -Wextra -O1 -fdiagnostics-color=never -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion
if exist build_%CC%.log del build_%CC%.log
for %%f in (src\*.c) do (
    >> build_%CC%.log echo %CC% %CFLAGS% -c %%f -o %%~nf.o
    %CC% %CFLAGS% -c %%f -o %%~nf.o >> build_%CC%.log 2>&1
)
echo wrote build_%CC%.log
endlocal
