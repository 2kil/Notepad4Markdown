@ECHO OFF
@rem ===========================================================================
@rem  Notepad4 Markdown Preview - build patched Notepad4.exe (x64 Release)
@rem
@rem  This script drives the official Visual Studio solution of Notepad4 found
@rem  in notepad4\build\VisualStudio\Notepad4.sln. No project files of the
@rem  upstream tree are replaced: only MarkdownPreview.cpp and the shared
@rem  protocol source are added to the project.
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

@rem Duplicate HTTPS_PROXY/https_proxy entries break the MSBuild CL task wrapper.
SET "HTTPS_PROXY="
SET "https_proxy="
SET "HTTP_PROXY="
SET "http_proxy="

SET "SDKROOT=C:\Program Files (x86)\Windows Kits\10"
SET "SDKVER=10.0.26100.0"
IF NOT EXIST "%SDKROOT%\Include\%SDKVER%\um\windows.h" (
	FOR /F "delims=" %%V IN ('DIR /B /AD "%SDKROOT%\Include" 2^>nul') DO SET "SDKVER=%%V"
)

@rem vcvars64.bat cannot query the SDK version on hosts where reg.exe is blocked,
@rem so the SDK paths are supplied explicitly to both the compiler and MSBuild.
SET "MDINC=%INCLUDE%"
SET "MDLIB=%LIB%"
SET "MDSDKINC=%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\winrt"
SET "MDSDKLIB=%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64"
SET "PATH=%SDKROOT%\bin\%SDKVER%\x64;%PATH%"

IF NOT EXIST "%ROOT%\notepad4\build\generated\Notepad4.zh-Hans.rc" (
	ECHO [ERROR] missing generated zh-Hans resource block.
	ECHO         run: python scripts\gen_locale_rc.py
	POPD
	EXIT /B 5
)

ECHO === Building Notepad4.exe ===
"%VSPATH%\MSBuild\Current\Bin\MSBuild.exe" /nologo "%ROOT%\notepad4\build\VisualStudio\Notepad4.sln" /t:Notepad4 ^
 /p:Configuration=Release /p:Platform=x64 ^
 /p:WindowsSDKInstalled=true /p:WindowsSDK_Desktop_Support=true /p:WindowsSDK_UAP_Support=true ^
 /p:IncludePath="%MDINC%;%MDSDKINC%" /p:LibraryPath="%MDLIB%;%MDSDKLIB%" /p:ResourceIncludePath="%MDSDKINC%" ^
 /m /v:m /clp:Verbosity=minimal

IF ERRORLEVEL 1 (
	ECHO [ERROR] Notepad4 build failed.
	POPD
	EXIT /B 2
)

SET "NB4DIR=%ROOT%\notepad4\build\bin\Release\x64"
IF NOT EXIST "%NB4DIR%\Notepad4.exe" (
	ECHO [ERROR] Notepad4.exe not produced.
	POPD
	EXIT /B 3
)

@rem Preview assets live next to the executable: <exe>\res\shell.html etc.
SET "DIST=%ROOT%\build\out\x64"
IF NOT EXIST "%DIST%" MKDIR "%DIST%"
COPY /Y "%NB4DIR%\Notepad4.exe" "%DIST%\" >nul

IF NOT EXIST "%ROOT%\preview\res\shell.html" (
	ECHO [ERROR] preview\res assets are missing.
	POPD
	EXIT /B 4
)
@rem res is embedded in the executable; a local copy is kept only in build\bin
@rem so CSS/JS can be tweaked without rebuilding.
IF NOT EXIST "%NB4DIR%\res" MKDIR "%NB4DIR%\res"
XCOPY /Q /Y "%ROOT%\preview\res\*.*" "%NB4DIR%\res\" >nul

ECHO [OK] %NB4DIR%\Notepad4.exe
ECHO [OK] %DIST%\Notepad4.exe
POPD
EXIT /B 0
