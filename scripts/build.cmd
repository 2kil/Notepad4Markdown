@ECHO OFF
@rem ===========================================================================
@rem  Notepad4 Markdown Preview - one-shot build
@rem
@rem   1. build Notepad4.exe (preview + CSS/JS + zh-Hans UI all embedded)
@rem   2. assemble build\out\x64 (distributable folder)
@rem
@rem  Usage: scripts\build.cmd
@rem ===========================================================================
SETLOCAL ENABLEEXTENSIONS
SET "ROOT=%~dp0.."

CALL "%~dp0build_notepad4.cmd"
IF ERRORLEVEL 1 (
	ECHO [ERROR] step 1 failed: Notepad4.exe.
	EXIT /B 1
)

SET "DIST=%ROOT%\build\out\x64"

ECHO.
ECHO ============================================================
ECHO  Build complete: %DIST%
ECHO    Notepad4.exe   - editor + inline preview
ECHO                     (CSS/JS assets and zh-Hans UI are embedded)
ECHO ============================================================
EXIT /B 0
