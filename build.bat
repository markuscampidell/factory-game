@echo off
REM Builds factory_game.exe with MSYS2's g++ (UCRT64) and raylib.
REM Usage:  build.bat          -> optimized game, no console window
REM         build.bat debug    -> with console window + debug info
REM         build.bat test     -> builds and runs the simulation tests
REM         build.bat clean    -> forget everything, the next build compiles all files
REM
REM Fast: only the files that changed since the last build are compiled again
REM (plus the files that include a changed header), on all CPU cores - see
REM Makefile. The first build after "clean" compiles everything once.
setlocal
cd /d "%~dp0"
set PATH=C:\msys64\ucrt64\bin;%PATH%

where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Run install_cpp.bat first.
  if not defined NOPAUSE pause
  exit /b 1
)

if /i "%1"=="test" (
  echo Building tests...
  g++ -std=c++17 -O2 -Ithird_party src\core\*.cpp src\sim\*.cpp tests\sim_tests.cpp -static -o sim_tests.exe || goto :fail
  sim_tests.exe tests\terrain_expected_seed424242.txt
  if not defined NOPAUSE pause
  exit /b
)

if /i "%1"=="clean" (
  if exist build rmdir /s /q build
  echo Cleaned - the next build compiles everything.
  exit /b 0
)

set MODE=release
if /i "%1"=="debug" set MODE=debug

REM switching between debug and release: link the .exe again even if no
REM file changed
if not exist build mkdir build
set LAST=
if exist build\mode.txt set /p LAST=<build\mode.txt
if not "%LAST%"=="%MODE%" if exist factory_game.exe del factory_game.exe
>build\mode.txt echo %MODE%

where mingw32-make >nul 2>nul
if errorlevel 1 goto :oneshot

echo Building factory_game.exe (%MODE%) - only what changed ...
mingw32-make --no-print-directory -j%NUMBER_OF_PROCESSORS% MODE=%MODE% || goto :fail
goto :done

:oneshot
REM No make installed (run install_cpp.bat to get it): compile everything at once.
echo (mingw32-make not found - compiling everything; install_cpp.bat installs it for faster builds)
set FLAGS=-std=c++17 -O2 -DNDEBUG -mwindows
if /i "%MODE%"=="debug" set FLAGS=-std=c++17 -O0 -g
g++ %FLAGS% -Ithird_party src\core\*.cpp src\sim\*.cpp src\game\*.cpp -lraylib -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++ -o factory_game.exe || goto :fail

:done
REM the DLLs the .exe needs, next to it
for %%D in (libraylib.dll glfw3.dll libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll) do if exist C:\msys64\ucrt64\bin\%%D copy /y C:\msys64\ucrt64\bin\%%D . >nul
if exist build_static.log del build_static.log
echo.
echo Done! Start factory_game.exe  (keep the .dll files next to it)
exit /b 0

:fail
echo.
echo BUILD FAILED
if not defined NOPAUSE pause
exit /b 1
