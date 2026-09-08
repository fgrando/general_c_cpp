@echo off
rem ---------------------------------------------------------------------------
rem Set the TortoiseSVN issue-tracker properties on the repository ROOT.
rem
rem REGEX MODE: you type [SCRUM-5] straight into the commit message text, as
rem many as you like, anywhere in the message. TortoiseSVN highlights them as
rem links while you type, so you can follow one BEFORE committing.
rem
rem Run this from a working copy OF THE ROOT. A depth-empty checkout is enough:
rem
rem     svn checkout --depth empty https://svn.internal/svn/myproject C:\tmp\rootwc
rem     cd /d C:\tmp\rootwc
rem     C:\svn-tools\set-bugtraq-props.bat
rem     svn commit -m "[NOJIRA] add TortoiseSVN issue tracker integration" .
rem
rem Since TortoiseSVN 1.8 these are INHERITED properties: setting them on the
rem root applies them implicitly to every subfolder, so each engineer gets the
rem behaviour on checkout with nothing to configure locally.
rem
rem NOTE ON %%: inside a .bat file, %%BUGID%% produces the literal text
rem %BUGID% that TortoiseSVN expects. Writing %BUGID% here would expand to an
rem empty string and silently break the property.
rem ---------------------------------------------------------------------------

setlocal

set JIRA_BASE=https://demo123467890.atlassian.net
set LOGREGEX=%~dp0bugtraq-logregex.txt

if not exist "%LOGREGEX%" (
    echo ERROR: bugtraq-logregex.txt not found next to this batch file.
    exit /b 1
)

echo Setting bugtraq properties on "%CD%" ...

rem Turns each detected key into a link to the Jira issue.
svn propset bugtraq:url "%JIRA_BASE%/browse/%%BUGID%%" .

rem Two regexes, one per line, read from a file - a multi-line property value
rem cannot be passed reliably on a cmd.exe command line.
rem   line 1: find a block of one or more [KEY-n] references
rem   line 2: pull each bare KEY-n out of that block
svn propset bugtraq:logregex -F "%LOGREGEX%" .

rem --- input-field mode properties: removed, logregex replaces them ---------
rem logregex takes precedence over bugtraq:message, but leaving these set only
rem causes confusion later, so delete them if a previous run added them.
svn propdel bugtraq:message . 2>nul
svn propdel bugtraq:append . 2>nul
svn propdel bugtraq:label . 2>nul
svn propdel bugtraq:number . 2>nul
svn propdel bugtraq:warnifnoissue . 2>nul

echo.
echo Done. Current properties:
svn proplist -v .
echo.
echo Now commit them:
echo     svn commit -m "[NOJIRA] add TortoiseSVN issue tracker integration" .

endlocal