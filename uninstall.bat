@echo off
setlocal
title ModelThumbs - Kaldirma

net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Yonetici izni isteniyor...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

set "DEST=%ProgramW6432%\ModelThumbs"

if exist "%DEST%\ModelThumbs.dll" (
  regsvr32 /u /s "%DEST%\ModelThumbs.dll"
  echo Kayit silindi.
) else (
  echo ModelThumbs kurulu gorunmuyor, yine de kayit temizleniyor...
  if exist "%~dp0ModelThumbs.dll" regsvr32 /u /s "%~dp0ModelThumbs.dll"
)

del /f /q "%DEST%\ModelThumbs.dll" >nul 2>&1
del /f /q "%DEST%\thumbtest.exe" >nul 2>&1
rmdir "%DEST%" >nul 2>&1
if exist "%DEST%\ModelThumbs.dll" (
  echo Not: DLL su an kullanimda oldugu icin silinemedi.
  echo Bilgisayar yeniden baslatildiktan sonra "%DEST%" klasorunu elle silebilirsiniz.
)

echo.
echo Kaldirma tamam. Kucuk resimler Explorer yeniden baslatilinca eski haline doner.
pause
