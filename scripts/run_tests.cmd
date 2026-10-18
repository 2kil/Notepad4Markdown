@ECHO OFF
@rem ===========================================================================
@rem  Notepad4 Markdown Preview - build and run automated tests
@rem
@rem  Stage 1: unit tests    mdtest.exe           (render / security / assets)
@rem  Stage 2: editor test   integration_test.exe (real Notepad4, inline pane)
@rem ===========================================================================
SETLOCAL ENABLEEXTENSIONS
SET "ROOT=%~dp0.."
PUSHD "%ROOT%"

SET "VSPATH="
FOR %%D IN (
	"%ProgramFiles%\Microsoft Visual Studio\18\Professional"
	"%ProgramFiles%\Microsoft Visual Studio\18\Community"
	"%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
	"%ProgramFiles%\Microsoft Visual Studio\18\BuildTools"
	"%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
	"%ProgramFiles%\Microsoft Visual Studio\2022\Community"
	"%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
	"%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
	"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
	"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
	"%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
) DO (
	IF NOT DEFINED VSPATH IF EXIST "%%~D\VC\Auxiliary\Build\vcvars64.bat" SET "VSPATH=%%~D"
)
IF NOT DEFINED VSPATH (
	ECHO [ERROR] Visual Studio not found.
	POPD
	EXIT /B 1
)
CALL "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
SET "HTTPS_PROXY="
SET "https_proxy="
SET "HTTP_PROXY="
SET "http_proxy="

SET "SDKROOT=C:\Program Files (x86)\Windows Kits\10"
SET "SDKVER=10.0.26100.0"
IF NOT EXIST "%SDKROOT%\Include\%SDKVER%\um\windows.h" (
	FOR /F "delims=" %%V IN ('DIR /B /AD "%SDKROOT%\Include" 2^>nul') DO SET "SDKVER=%%V"
)
SET "INCLUDE=%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\winrt;%INCLUDE%"
SET "LIB=%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64;%LIB%"
SET "PATH=%SDKROOT%\bin\%SDKVER%\x64;%PATH%"

SET "OUTDIR=%ROOT%\build\out\x64"
IF NOT EXIST "%OUTDIR%" MKDIR "%OUTDIR%"
IF NOT EXIST "%ROOT%\build\obj\tests" MKDIR "%ROOT%\build\obj\tests"
IF NOT EXIST "%ROOT%\build\logs" MKDIR "%ROOT%\build\logs"

SET "CFLAGS=/nologo /utf-8 /W4 /EHsc /std:c++20 /O2 /MT /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS"
SET "MD4C=%ROOT%\third_party\md4c\md4c.c %ROOT%\third_party\md4c\md4c-html.c %ROOT%\third_party\md4c\entity.c"

ECHO === Building mdtest.exe ===
cl %CFLAGS% /I"%ROOT%\third_party\md4c" /I"%ROOT%\shared" /I"%ROOT%\preview\src" ^
 /Fo"%ROOT%\build\obj\tests\\" /Fd"%ROOT%\build\obj\tests\tests.pdb" ^
 "%ROOT%\tests\mdtest.cpp" "%ROOT%\preview\src\MarkdownRenderer.cpp" "%ROOT%\preview\src\LocalContent.cpp" ^
 "%ROOT%\shared\text_encoding.cpp" %MD4C% ^
 /Fe"%OUTDIR%\mdtest.exe" /link /SUBSYSTEM:CONSOLE advapi32.lib shlwapi.lib user32.lib shell32.lib
IF ERRORLEVEL 1 (
	ECHO [ERROR] mdtest build failed.
	POPD
	EXIT /B 2
)

ECHO === Building integration_test.exe ===
cl %CFLAGS% /I"%ROOT%\third_party\md4c" ^
 /Fo"%ROOT%\build\obj\tests\\" /Fd"%ROOT%\build\obj\tests\integration.pdb" ^
 "%ROOT%\tests\integration_test.cpp" ^
 /Fe"%OUTDIR%\integration_test.exe" /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib gdiplus.lib ole32.lib shell32.lib
IF ERRORLEVEL 1 (
	ECHO [ERROR] integration_test build failed.
	POPD
	EXIT /B 2
)

IF NOT EXIST "%OUTDIR%\Notepad4.exe" (
	ECHO [ERROR] Notepad4.exe missing, run scripts\build.cmd first.
	POPD
	EXIT /B 3
)

ECHO.
ECHO === Stage 1: unit tests ===
"%OUTDIR%\mdtest.exe"
SET "UNIT_EXIT=%ERRORLEVEL%"
ECHO stage1 exit code: %UNIT_EXIT%

ECHO.
ECHO === Stage 2: Notepad4 inline preview ===
"%OUTDIR%\integration_test.exe"
SET "INTEG_EXIT=%ERRORLEVEL%"
ECHO stage2 exit code: %INTEG_EXIT%

ECHO.
IF "%UNIT_EXIT%%INTEG_EXIT%"=="00" (
	ECHO [PASS] all test stages passed.
	POPD
	EXIT /B 0
)
ECHO [FAIL] see output above.
POPD
EXIT /B 1
