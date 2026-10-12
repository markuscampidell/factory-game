@echo off
REM One-time setup: connects this folder to your GitHub repository and
REM uploads the game. After that, use publish_web.bat for updates.
setlocal
cd /d "%~dp0"
set REPO=https://github.com/markuscampidell/factory-game.git

where git >nul 2>nul
if errorlevel 1 (
  echo Git is not installed.
  echo Install it from https://git-scm.com/download/win ^(just keep all the defaults^),
  echo then run this file again.
  pause
  exit /b 1
)
if exist .git (
  echo This folder is already set up. Use publish_web.bat to upload updates.
  pause
  exit /b 0
)

echo Connecting this folder to %REPO%
git init -b main
git config user.name "markuscampidell"
git config user.email "markuscampidell@users.noreply.github.com"
git add -A
git commit -q -m "Factory game"
git remote add origin %REPO%
echo.
echo Uploading... (a browser window may open: sign in to GitHub there)
git push -u origin main
if errorlevel 1 (
  echo.
  echo Upload failed. Did you create the empty repository "factory-game" on GitHub first?
  pause
  exit /b 1
)
echo.
echo Done! One last step on github.com:
echo   open your repository -^> Settings -^> Pages -^> Source: "GitHub Actions"
echo Then, after about 3 minutes, the game is at:
echo   https://markuscampidell.github.io/factory-game/
pause
