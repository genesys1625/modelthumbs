@echo off
setlocal
title ModelThumbs - Kurulum

net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Yonetici izni isteniyor...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

set "DEST=%ProgramW6432%\ModelThumbs"

if not exist "%~dp0ModelThumbs.dll" (
  echo HATA: ModelThumbs.dll bu betikle ayni klasorde olmali.
  pause
  exit /b 1
)

echo Eski surum varsa kaldiriliyor...
if exist "%DEST%\ModelThumbs.dll" regsvr32 /u /s "%DEST%\ModelThumbs.dll"

if not exist "%DEST%" mkdir "%DEST%"
copy /y "%~dp0ModelThumbs.dll" "%DEST%\ModelThumbs.dll" >nul
if errorlevel 1 (
  echo HATA: Dosya kopyalanamadi. Dosya kullanimda olabilir.
  echo Bilgisayari yeniden baslatip install.bat dosyasini tekrar calistirin.
  pause
  exit /b 1
)
if exist "%~dp0thumbtest.exe" copy /y "%~dp0thumbtest.exe" "%DEST%\thumbtest.exe" >nul

regsvr32 /s "%DEST%\ModelThumbs.dll"
if errorlevel 1 (
  echo HATA: Kayit basarisiz oldu.
  pause
  exit /b 1
)

echo.
echo Kurulum tamam: %DEST%
echo.
echo Windows eski kucuk resimleri onbellekte tutar. Yeni kucuk resimlerin hemen
echo gorunmesi icin onbellek temizlenip Explorer yeniden baslatilabilir.
echo Acik klasor pencereleriniz kapanir, kaydedilmemis bir isiniz olmasin.
choice /c EH /m "Simdi yapilsin mi"
if errorlevel 2 goto done

taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

:done
echo.
echo Klasorunuzu "Buyuk simgeler" veya "Orta simgeler" gorunumune alin.
echo Sorun olursa README.txt dosyasindaki "Sorun giderme" bolumune bakin.
pause
