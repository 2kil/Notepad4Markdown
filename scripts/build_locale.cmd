@ECHO OFF
@rem ===========================================================================
@rem  Notepad4 Markdown Preview - build the Simplified Chinese resource DLL
@rem
@rem  The editor loads its localized menu/dialog strings from
@rem  <exe>\locale\zh-Hans\Notepad4.dll. That DLL is produced by the upstream
@rem  locale solution; this script only drives it with the same environment
@rem  workarounds used by build_notepad4.cmd.
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

SET "MDINC=%INCLUDE%"
SET "MDLIB=%LIB%"
SET "MDSDKINC=%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\winrt"
SET "MDSDKLIB=%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64"
SET "PATH=%SDKROOT%\bin\%SDKVER%\x64;%PATH%"

ECHO === Building zh-Hans resource DLL ===
"%VSPATH%\MSBuild\Current\Bin\MSBuild.exe" /nologo "%ROOT%\notepad4\locale\Locale.sln" /t:Notepad4_zh-Hans_ ^
 /p:Configuration=Release /p:Platform=x64 ^
 /p:WindowsSDKInstalled=true /p:WindowsSDK_Desktop_Support=true /p:WindowsSDK_UAP_Support=true ^
 /p:IncludePath="%MDINC%;%MDSDKINC%" /p:LibraryPath="%MDLIB%;%MDSDKLIB%" /p:ResourceIncludePath="%MDSDKINC%" ^
 /m /v:m /clp:Verbosity=minimal

IF ERRORLEVEL 1 (
	ECHO [ERROR] zh-Hans resource build failed.
	POPD
	EXIT /B 2
)

SET "LOCDIR=%ROOT%\notepad4\build\bin\Release\x64\locale\zh-Hans"
IF NOT EXIST "%LOCDIR%\Notepad4.dll" (
	ECHO [ERROR] Notepad4.dll not produced in %LOCDIR%
	POPD
	EXIT /B 3
)

ECHO [OK] %LOCDIR%\Notepad4.dll
POPD
EXIT /B 0
