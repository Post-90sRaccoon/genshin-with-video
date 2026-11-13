@echo off
setlocal
set "GenshinVideo_LaunchDir=%~dp0"
powershell.exe -NoProfile -WindowStyle Hidden -Command "if (@(Get-Process -Name GenshinVideo,GenshinVideo-diagnostics* -ErrorAction SilentlyContinue).Count) { Add-Type -AssemblyName System.Windows.Forms; [void][System.Windows.Forms.MessageBox]::Show('Exit the running overlay before starting the diagnostics build.','Genshin Video'); exit }; Start-Process -FilePath (Join-Path $env:GenshinVideo_LaunchDir 'GenshinVideo-diagnostics.exe') -Verb RunAs -WindowStyle Normal"
endlocal
