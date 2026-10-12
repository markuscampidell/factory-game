@echo off
REM Installs MSYS2 + g++ (UCRT64) + raylib + cmake/make for the C++ factory game.
echo === Installing MSYS2 via winget ===
winget install --id MSYS2.MSYS2 -e --accept-source-agreements --accept-package-agreements
if not exist C:\msys64\usr\bin\bash.exe (
  echo MSYS2 was not found at C:\msys64. Install it from https://www.msys2.org and run this again.
  pause
  exit /b 1
)
echo === Updating MSYS2 and installing g++, raylib, cmake, make ===
C:\msys64\usr\bin\bash.exe -lc "pacman -Syu --noconfirm"
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-raylib mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make"
echo === Adding C:\msys64\ucrt64\bin to your user PATH ===
powershell -NoProfile -Command "$p=[Environment]::GetEnvironmentVariable('Path','User'); if($p -notlike '*msys64\ucrt64\bin*'){[Environment]::SetEnvironmentVariable('Path',$p+';C:\msys64\ucrt64\bin','User')}"
C:\msys64\ucrt64\bin\g++.exe --version
echo === Done. Open a NEW terminal and run: g++ --version ===
pause
