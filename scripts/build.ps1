param([switch]$TestOnly,[switch]$IntegrationOnly,[switch]$ControllerOnly,[switch]$DiagnosticsBuild,[switch]$NoPackage)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio C++ Build Tools and Windows SDK first.' }
New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$flags = '/nologo /std:c++17 /EHsc /utf-8 /W4 /O2 /MP2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /I..\third_party\include /I..\third_party\mpv\include /I..\src'
$appExecutable=if($DiagnosticsBuild){'GenshinVideo-diagnostics.exe'}else{'GenshinVideo.exe'}
if ($ControllerOnly) {
    $commands=@('@echo off',('call "'+$vcvars+'" >nul'),('cd /d "'+(Join-Path $root 'build')+'"'),'rc /nologo /fo app.res ..\src\app.rc','if errorlevel 1 exit /b 1',('cl '+$flags+' ..\tests\app_tests.cpp core.obj platform.obj source.obj extraction.obj player.obj diagnostics.obj dialogs.obj app.res /Fe:app_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib winhttp.lib ole32.lib dwmapi.lib'),'if errorlevel 1 exit /b 1','app_tests.exe','exit /b %errorlevel%')
    $controllerScript=Join-Path $root 'build/controller-tests.cmd'
    [IO.File]::WriteAllLines($controllerScript,$commands,[Text.Encoding]::Default)
    & cmd.exe /d /c $controllerScript
    if ($LASTEXITCODE -ne 0) {throw 'Controller tests failed'}
    return
}
$lines = @('@echo off',('call "' + $vcvars + '" >nul'),('cd /d "' + (Join-Path $root 'build') + '"'),('cl ' + $flags + ' ..\tests\core_tests.cpp ..\src\core.cpp /Fe:core_tests.exe'), 'if errorlevel 1 exit /b 1', 'core_tests.exe', 'if errorlevel 1 exit /b 1')
if (!$TestOnly) {
    $lines += @('rc /nologo /fo app.res ..\src\app.rc','if errorlevel 1 exit /b 1')
    if (!$IntegrationOnly) { $lines += @(('cl ' + $flags + ' ..\src\main.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\source.cpp ..\src\extraction.cpp ..\src\player.cpp ..\src\diagnostics.cpp ..\src\dialogs.cpp app.res /Fe:'+$appExecutable+' /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib winhttp.lib ole32.lib dwmapi.lib'), 'if errorlevel 1 exit /b 1') }
    $lines += @(('cl ' + $flags + ' ..\tests\player_headers_tests.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\extraction.cpp ..\src\diagnostics.cpp /Fe:player_headers_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib winhttp.lib shell32.lib ole32.lib'),'if errorlevel 1 exit /b 1','player_headers_tests.exe','if errorlevel 1 exit /b 1')
    $lines += @(('cl ' + $flags + ' ..\tests\extraction_tests.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\extraction.cpp ..\src\diagnostics.cpp /Fe:extraction_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib winhttp.lib shell32.lib ole32.lib'),'if errorlevel 1 exit /b 1','extraction_tests.exe','if errorlevel 1 exit /b 1')
    $lines += @(('cl ' + $flags + ' ..\tests\playback_diagnostics_tests.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\diagnostics.cpp /Fe:playback_diagnostics_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib winhttp.lib shell32.lib ole32.lib'),'if errorlevel 1 exit /b 1','playback_diagnostics_tests.exe','if errorlevel 1 exit /b 1',('cl ' + $flags + ' ..\tests\integration.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\source.cpp ..\src\extraction.cpp ..\src\player.cpp ..\src\diagnostics.cpp /Fe:integration.exe /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib winhttp.lib shell32.lib ole32.lib'), 'if errorlevel 1 exit /b 1','integration.exe','if errorlevel 1 exit /b 1',('cl ' + $flags + ' ..\tests\app_tests.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\source.cpp ..\src\extraction.cpp ..\src\player.cpp ..\src\diagnostics.cpp ..\src\dialogs.cpp app.res /Fe:app_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib winhttp.lib ole32.lib dwmapi.lib'),'if errorlevel 1 exit /b 1','app_tests.exe','if errorlevel 1 exit /b 1')
}
$lines += 'exit /b 0'
$cmdFile = Join-Path $root 'build/compile.cmd'
[IO.File]::WriteAllLines($cmdFile,$lines,[Text.Encoding]::Default)
& cmd.exe /d /c $cmdFile
if ($LASTEXITCODE -ne 0) { throw ('Build failed with code ' + $LASTEXITCODE) }
if (!$TestOnly -and !$IntegrationOnly -and !$NoPackage) {
    $portable = Join-Path $root 'dist/GenshinVideo'
    New-Item -ItemType Directory -Force -Path $portable | Out-Null
    Copy-Item -LiteralPath (Join-Path $root ('build/'+$appExecutable)),(Join-Path $root 'third_party/mpv/libmpv-2.dll'),(Join-Path $root 'third_party/yt-dlp.exe') -Destination $portable -Force
    Copy-Item -LiteralPath (Join-Path $root 'README.md'),(Join-Path $root 'THIRD_PARTY.md'),(Join-Path $root 'LICENSE'),(Join-Path $root 'deps.lock.json') -Destination $portable -Force
    Copy-Item -LiteralPath (Join-Path $root 'packaging/Start-GameMode.cmd') -Destination $portable -Force
    if($DiagnosticsBuild){Copy-Item -LiteralPath (Join-Path $root 'packaging/Start-GameMode-Diagnostics.cmd') -Destination $portable -Force}
    New-Item -ItemType Directory -Force -Path (Join-Path $portable 'licenses'),(Join-Path $portable 'docs') | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $root 'licenses') -File | Copy-Item -Destination (Join-Path $portable 'licenses') -Force
    Copy-Item -LiteralPath (Join-Path $root 'docs/verification.md'),(Join-Path $root 'docs/crash-investigation.md') -Destination (Join-Path $portable 'docs') -Force
    Write-Output ('Portable app: ' + $portable)
}
