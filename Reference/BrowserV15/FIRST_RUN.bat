@echo off
setlocal
cd /d "%~dp0"
title Rift Crown Arena - First Run

echo ========================================
echo       RIFT CROWN ARENA - FIRST RUN
echo ========================================
echo.
echo [1/2] Looking for Blender...

set "BLENDER_EXE="
where blender >nul 2>nul
if not errorlevel 1 set "BLENDER_EXE=blender"

for %%V in (5.2 5.1 5.0 4.5 4.4 4.3 4.2 4.1 4.0) do (
  if not defined BLENDER_EXE if exist "C:\Program Files\Blender Foundation\Blender %%V\blender.exe" set "BLENDER_EXE=C:\Program Files\Blender Foundation\Blender %%V\blender.exe"
)

if defined BLENDER_EXE (
  echo Blender found: %BLENDER_EXE%
  echo Generating Rift Crown Model Forge V12 units, buildings, and towers...
  "%BLENDER_EXE%" --background --python-exit-code 1 --python "%~dp0blender\generate_models.py"
  if errorlevel 1 (
    echo.
    echo Blender generation failed. The game will still launch with its built-in fallback models.
  ) else (
    echo Blender models generated successfully.
  )
) else (
  echo Blender was not found automatically.
  echo The game will still run with its built-in V12 procedural fallback models.
  echo You can generate Blender models later with build_models.bat.
)

echo.
echo [2/2] Launching Rift Crown Arena...
call "%~dp0start_game.bat"
