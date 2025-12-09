@echo off
set NDK_ROOT=D:\android-ndk-r27c
set CLANG=%NDK_ROOT%\toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe

echo Compiling rate_daemon...

"%CLANG%" ^
    --target=aarch64-linux-android30 ^
    -O3 ^
    -static ^
    src\rate_daemon.c ^
    -o bin\rate_daemon

echo Compiling dts_tool...

"%CLANG%" ^
    --target=aarch64-linux-android30 ^
    -O3 ^
    -static ^
    src\dts_tool.c ^
    -o bin\dts_tool

if %ERRORLEVEL% EQU 0 (
    echo Build successful! Output: bin\rate_daemon, bin\dts_tool
) else (
    echo Build failed!
)
