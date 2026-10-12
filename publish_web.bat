@echo off
REM Uploads your latest changes. GitHub then builds the browser version by
REM itself (about 3 minutes) - just reload the page on the Chromebook.
setlocal
cd /d "%~dp0"
where git >nul 2>nul
if errorlevel 1 ( echo Git is not installed - see setup_github.bat. & pause & exit /b 1 )
if not exist .git ( echo Run setup_github.bat first. & pause & exit /b 1 )

git add -A
git diff --cached --quiet
if errorlevel 1 (
  git commit -q -m "Update %date% %time%"
) else (
  echo Nothing changed since the last upload.
)
git push
if errorlevel 1 ( echo Upload failed. & pause & exit /b 1 )
echo.
echo Uploaded. In about 3 minutes the new version is online at
echo   https://markuscampidell.github.io/factory-game/
echo (progress: https://github.com/markuscampidell/factory-game/actions)
pause
