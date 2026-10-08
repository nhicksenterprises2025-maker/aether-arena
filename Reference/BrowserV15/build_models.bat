@echo off
setlocal
cd /d "%~dp0"
title Rift Crown Arena - Blender Model Forge V12

set "BLENDER_EXE="
where blender >nul 2>nul
if not errorlevel 1 set "BLENDER_EXE=blender"

for %%V in (5.2 5.1 5.0 4.5 4.4 4.3 4.2 4.1 4.0) do (
  if not defined BLENDER_EXE if exist "C:\Program Files\Blender Foundation\Blender %%V\blender.exe" set "BLENDER_EXE=C:\Program Files\Blender Foundation\Blender %%V\blender.exe"
)

if not defined BLENDER_EXE (
  echo Blender was not found in PATH or a standard installation folder.
  echo Open Blender, then run blender\generate_models.py from the Scripting workspace,
  echo or add Blender to PATH and run this file again.
  pause
  exit /b 1
)

echo Using Blender: %BLENDER_EXE%
echo Generating Rift Crown Model Forge V12 assets...
"%BLENDER_EXE%" --background --python-exit-code 1 --python "%~dp0blender\generate_models.py"
if errorlevel 1 (
  echo Model generation failed.
  pause
  exit /b 1
)

echo.
echo V12 models generated in assets\models\
echo Editable source saved to blender\rift_crown_models.blend
pause
