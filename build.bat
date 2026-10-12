@echo off
REM Builds factory_game.exe with MSYS2's g++ (UCRT64) and raylib.
REM Usage:  build.bat          -> optimized game, no console window
REM         build.bat debug    -> with console window + debug info
REM         build.bat test     -> builds and runs the simulation tests
setlocal
cd /d "%~dp0"
set PATH=C:\msys64\ucrt64\bin;%PATH%

where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Run install_cpp.bat first.
  if not defined NOPAUSE pause
  exit /b 1
)

set CORE=src\core\Items.cpp src\core\Inventory.cpp
set SIM=src\sim\Terrain.cpp src\sim\World.cpp src\sim\Transport.cpp src\sim\Player.cpp src\sim\Build.cpp src\sim\Save.cpp src\sim\Blueprint.cpp
set GAME=src\game\main.cpp src\game\Assets.cpp src\game\Ui.cpp src\game\MainMenu.cpp src\game\GameScreen.cpp src\game\GameDraw.cpp src\game\GamePanels.cpp src\game\GameMenu.cpp src\game\GameBlueprints.cpp

if /i "%1"=="test" (
  echo Building tests...
  g++ -std=c++17 -O2 -Ithird_party %CORE% %SIM% tests\sim_tests.cpp -static -o sim_tests.exe || goto :fail
  sim_tests.exe tests\terrain_expected_seed424242.txt
  if not defined NOPAUSE pause
  exit /b
)

set FLAGS=-std=c++17 -O2 -DNDEBUG -mwindows
if /i "%1"=="debug" set FLAGS=-std=c++17 -O0 -g

set LIBS=-lraylib
if exist C:\msys64\ucrt64\lib\libglfw3.a set LIBS=-lraylib -lglfw3

echo Building factory_game.exe ...
REM 1st try: fully static, gives one self-contained .exe
g++ %FLAGS% -Ithird_party %CORE% %SIM% %GAME% -static %LIBS% -lopengl32 -lgdi32 -lwinmm -o factory_game.exe 2>build_static.log
if not errorlevel 1 (
  del build_static.log >nul 2>nul
  echo.
  echo Done! Start factory_game.exe
  exit /b 0
)

REM 2nd try: use raylib's DLL and copy the needed DLLs next to the .exe
echo (raylib has no static library here, using its DLL instead)
g++ %FLAGS% -Ithird_party %CORE% %SIM% %GAME% -lraylib -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++ -o factory_game.exe || goto :fail
for %%D in (libraylib.dll glfw3.dll libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll) do if exist C:\msys64\ucrt64\bin\%%D copy /y C:\msys64\ucrt64\bin\%%D . >nul
echo.
echo Done! Start factory_game.exe  (keep the .dll files next to it)
exit /b 0

:fail
echo.
echo BUILD FAILED
if not defined NOPAUSE pause
exit /b 1
