@echo off
setlocal
cd /d "%~dp0"
title Rift Crown Arena Launcher
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_game.ps1"
if errorlevel 1 pause
