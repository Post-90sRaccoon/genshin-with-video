param([switch]$TestOnly,[switch]$IntegrationOnly,[switch]$ControllerOnly,[switch]$DiagnosticsBuild,[switch]$NoPackage,[string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio C++ Build Tools and Windows SDK first.' }
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$flags = '/nologo /std:c++17 /EHsc /utf-8 /W4 /O2 /MP2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /I..\third_party\include /I..\third_party\mpv\include /I..\third_party\webview2\include /I..\src'
$webview = Join-Path $root 'third_party/webview2'
if ((!$TestOnly -or $ControllerOnly) -and !(Test-Path -LiteralPath (Join-Path $webview 'include/WebView2.h'))) {
    throw 'Run scripts/setup-deps.ps1 to install the pinned WebView2 SDK.'
}
$appExecutable = if ($DiagnosticsBuild) { 'GenshinVideo-diagnostics.exe' } else { 'GenshinVideo.exe' }
$sessionSources = '..\src\bili_session.cpp ..\src\platform.cpp'
$extractSources = "..\src\extraction.cpp $sessionSources"
$playbackSources = "..\src\core.cpp ..\src\source.cpp $extractSources ..\src\player.cpp ..\src\diagnostics.cpp"
$appSources = "$playbackSources ..\src\dialogs.cpp ..\src\login_window.cpp"
$sessionLibs = 'winhttp.lib ole32.lib crypt32.lib advapi32.lib'
$appLibs = "user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib dwmapi.lib $sessionLibs"
$resourceCommands = @('rc /nologo /fo app.res ..\src\app.rc', 'if errorlevel 1 exit /b 1')
$controllerCommand = "cl $flags ..\tests\app_tests.cpp $appSources app.res /Fe:app_tests.exe /link /SUBSYSTEM:CONSOLE $appLibs"
$commands = @('@echo off', ('call "' + $vcvars + '" >nul'), ('cd /d "' + $build + '"'))
if ($ControllerOnly) {
    $commands += $resourceCommands + @($controllerCommand,'if errorlevel 1 exit /b 1','app_tests.exe','exit /b %errorlevel%')
    $batch = Join-Path $build 'controller-tests.cmd'
    [IO.File]::WriteAllLines($batch,$commands,[Text.Encoding]::Default)
    & cmd.exe /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw 'Controller tests failed' }
    return
}
$commands += @(
    "cl $flags ..\tests\core_tests.cpp ..\src\core.cpp /Fe:core_tests.exe",
    'if errorlevel 1 exit /b 1','core_tests.exe','if errorlevel 1 exit /b 1'
)
if (!$TestOnly) {
    $commands += $resourceCommands
    if (!$IntegrationOnly) {
        $commands += @("cl $flags ..\src\main.cpp $appSources app.res /Fe:$appExecutable /link /SUBSYSTEM:WINDOWS $appLibs", 'if errorlevel 1 exit /b 1')
    }
    $commands += @(
        "cl $flags ..\tests\bili_session_tests.cpp $sessionSources /Fe:bili_session_tests.exe /link /SUBSYSTEM:CONSOLE $sessionLibs",
        'if errorlevel 1 exit /b 1','bili_session_tests.exe','if errorlevel 1 exit /b 1',
        "cl $flags ..\tests\player_headers_tests.cpp ..\src\core.cpp $extractSources ..\src\diagnostics.cpp /Fe:player_headers_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib $sessionLibs",
        'if errorlevel 1 exit /b 1','player_headers_tests.exe','if errorlevel 1 exit /b 1',
        "cl $flags ..\tests\extraction_tests.cpp ..\src\core.cpp $extractSources ..\src\diagnostics.cpp /Fe:extraction_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib $sessionLibs",
        'if errorlevel 1 exit /b 1','extraction_tests.exe','if errorlevel 1 exit /b 1',
        "cl $flags ..\tests\playback_diagnostics_tests.cpp ..\src\core.cpp ..\src\platform.cpp ..\src\diagnostics.cpp /Fe:playback_diagnostics_tests.exe /link /SUBSYSTEM:CONSOLE user32.lib winhttp.lib shell32.lib ole32.lib",
        'if errorlevel 1 exit /b 1','playback_diagnostics_tests.exe','if errorlevel 1 exit /b 1',
        "cl $flags ..\tests\integration.cpp $playbackSources /Fe:integration.exe /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib shell32.lib $sessionLibs",
        'if errorlevel 1 exit /b 1','integration.exe','if errorlevel 1 exit /b 1',
        $controllerCommand,'if errorlevel 1 exit /b 1','app_tests.exe','if errorlevel 1 exit /b 1'
    )
}
$commands += 'exit /b 0'
$batch = Join-Path $build 'compile.cmd'
[IO.File]::WriteAllLines($batch,$commands,[Text.Encoding]::Default)
& cmd.exe /d /c $batch
if ($LASTEXITCODE -ne 0) { throw ('Build failed with code ' + $LASTEXITCODE) }
if (!$TestOnly -and !$IntegrationOnly -and !$NoPackage) {
    $portable = if (!$OutputDirectory) {
        Join-Path $root 'dist/GenshinVideo'
    } elseif ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    } else {
        Join-Path $root $OutputDirectory
    }
    New-Item -ItemType Directory -Force -Path $portable | Out-Null
    Copy-Item -LiteralPath (Join-Path $build $appExecutable),(Join-Path $root 'third_party/mpv/libmpv-2.dll'),(Join-Path $root 'third_party/yt-dlp.exe'),(Join-Path $webview 'x64/WebView2Loader.dll') -Destination $portable -Force
    Copy-Item -LiteralPath (Join-Path $root 'README.md'),(Join-Path $root 'THIRD_PARTY.md'),(Join-Path $root 'LICENSE'),(Join-Path $root 'deps.lock.json') -Destination $portable -Force
    Copy-Item -LiteralPath (Join-Path $root 'packaging/Start-GameMode.cmd') -Destination $portable -Force
    if ($DiagnosticsBuild) { Copy-Item -LiteralPath (Join-Path $root 'packaging/Start-GameMode-Diagnostics.cmd') -Destination $portable -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $portable 'licenses'),(Join-Path $portable 'docs') | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $root 'licenses'),(Join-Path $webview 'notices') -File | Copy-Item -Destination (Join-Path $portable 'licenses') -Force
    Copy-Item -LiteralPath (Join-Path $root 'docs/verification.md'),(Join-Path $root 'docs/crash-investigation.md') -Destination (Join-Path $portable 'docs') -Force
    Write-Output ('Portable app: ' + $portable)
}
