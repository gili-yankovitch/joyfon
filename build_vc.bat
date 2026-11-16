@echo off
rem Build joyfon firmware (Ninja + arm-none-eabi-gcc, with MSVC env for picotool).
rem Usage: build_vc.bat [extra ninja args, e.g. --clean-first]
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
set PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%
cmake --build "%~dp0build-ninja" %*
