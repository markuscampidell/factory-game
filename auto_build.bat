@echo off
REM Lets Claude build the game for you.
REM Start this once (double-click) and leave the window open. Whenever Claude
REM changes the code it creates the file "build.request" in this folder; this
REM window notices it, runs build.bat and writes the result to build_log.txt
REM so Claude can read it. Close the window to stop.
cd /d "%~dp0"
title Factory game auto-builder - leave this window open
set NOPAUSE=1
echo Auto-builder is running. Leave this window open.
echo (Close the game before a build, otherwise the .exe can't be replaced.)
echo.
:loop
if exist build.request (
  del build.request
  echo [%date% %time%] Building...
  echo BUILDING> build_status.txt
  call build.bat > build_log.txt 2>&1
  if errorlevel 1 (
    echo FAILED> build_status.txt
    echo [%date% %time%] BUILD FAILED - see build_log.txt
  ) else (
    echo OK> build_status.txt
    echo [%date% %time%] Build finished - you can start factory_game.exe
  )
)
timeout /t 2 /nobreak >nul
goto loop
