@ECHO OFF
@rem ===========================================================================
@rem  Notepad4 Markdown Preview - one-shot build
@rem
@rem   0. fetch the pinned upstream tree and apply the patch (first run only)
@rem   1. build Notepad4.exe (preview + CSS/JS + zh-Hans UI all embedded)
@rem   2. assemble build\out\x64 (distributable folder)
@rem
@rem  Usage: scripts\build.cmd
@rem ===========================================================================
SETLOCAL ENABLEEXTENSIONS
SET "ROOT=%~dp0.."
PUSHD "%ROOT%"

IF NOT EXIST "%ROOT%\notepad4\src\Notepad4.cpp" (
	ECHO === Fetching upstream source (see upstream.lock) ===
	WHERE python >nul 2>&1
	IF ERRORLEVEL 1 (
		ECHO [ERROR] python not found. Install Python 3, or clone the upstream tree
		ECHO         into notepad4\ and apply patch\0001-notepad4-markdown-preview.patch
		POPD
		EXIT /B 3
	)
	python "%ROOT%\scripts\fetch_upstream.py"
	IF ERRORLEVEL 1 (
		ECHO [ERROR] failed to fetch upstream source.
		POPD
		EXIT /B 3
	)
)

CALL "%~dp0build_notepad4.cmd"
IF ERRORLEVEL 1 (
	ECHO [ERROR] step 1 failed: Notepad4.exe.
	POPD
	EXIT /B 1
)

SET "DIST=%ROOT%\build\out\x64"

ECHO.
ECHO ============================================================
ECHO  Build complete: %DIST%
ECHO    Notepad4.exe   - editor + inline preview
ECHO                     (CSS/JS assets and zh-Hans UI are embedded)
ECHO ============================================================
POPD
EXIT /B 0
