@echo off
rem ---------------------------------------------------------------------------
rem TortoiseSVN check-commit hook wrapper.
rem
rem Point TortoiseSVN at THIS file, not at the .py directly - a .bat wrapper
rem survives a missing .py file association and lets you pin the interpreter.
rem
rem CRITICAL: the  exit /b %ERRORLEVEL%  line is what propagates a rejection
rem back to TortoiseSVN. Without it the batch file always returns 0 and every
rem commit is silently accepted.
rem ---------------------------------------------------------------------------

setlocal

rem Adjust if python.exe is not on PATH, e.g.:
rem set PYTHON=C:\Python311\python.exe
set PYTHON=python

"%PYTHON%" "%~dp0check_commit_hook.py" %*

exit /b %ERRORLEVEL%