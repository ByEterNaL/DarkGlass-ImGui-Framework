@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

:: Try to find vswhere
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
    echo Visual Studio Installer not found. Make sure Visual Studio with C++ Desktop Development is installed.
    exit /b 1
)

:: Get installation path
for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "vs_path=%%i"
)

if "%vs_path%"=="" (
    echo C++ tools not found. Please install "Desktop development with C++" workload in Visual Studio.
    exit /b 1
)

:: Call vcvarsall.bat to setup MSVC environment. We use VSCMD_START_DIR to preserve the directory.
set "VSCMD_START_DIR=%~dp0"
call "%vs_path%\VC\Auxiliary\Build\vcvarsall.bat" x64

echo Compiling DarkGlass_UI...

:: Compiler flags with C++17
set CFLAGS=/nologo /EHsc /MD /O2 /std:c++17 /I. /I.\imgui
set LIBS=d3d11.lib d3dcompiler.lib dxgi.lib user32.lib dwmapi.lib comdlg32.lib shell32.lib

:: Source files (Note: backend files were downloaded directly into imgui folder)
set SOURCES=main.cpp DarkGlass.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp imgui\imgui_demo.cpp imgui\imgui_impl_win32.cpp imgui\imgui_impl_dx11.cpp

:: Compile
cl %CFLAGS% %SOURCES% /Fe:DarkGlass_UI.exe /link %LIBS%

if %errorlevel% neq 0 (
    echo Compilation failed!
    exit /b 1
)

echo Compilation successful!
