$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vendor = Join-Path $root 'third_party'
$lock = Get-Content -LiteralPath (Join-Path $root 'deps.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $vendor,(Join-Path $vendor 'include'),(Join-Path $vendor 'downloads'),(Join-Path $vendor 'mpv'),(Join-Path $vendor 'tools') | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$headers = @{ 'User-Agent' = 'GenshinVideo-build' }
function Get-Asset($url, $path, $hash) {
    if ($hash -notmatch '^[0-9a-fA-F]{64}$') { throw "Missing SHA256: $path" }
    if (!(Test-Path -LiteralPath $path)) { Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $url -OutFile $path }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -and $actual -ne $hash.ToLowerInvariant()) { throw "SHA256 mismatch: $path" }
    return $actual
}
$archive = Join-Path $vendor ('downloads/' + $lock.mpv.asset)
$null = Get-Asset $lock.mpv.url $archive $lock.mpv.sha256
if (!(Test-Path -LiteralPath (Join-Path $vendor 'mpv/libmpv-2.dll'))) {
    $seven = Join-Path $vendor 'tools/7zr.exe'
    $null = Get-Asset $lock.archiver.url $seven $lock.archiver.sha256
    & $seven x $archive ('-o' + (Join-Path $vendor 'mpv')) -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'mpv extraction failed' }
}
if ((Get-FileHash -LiteralPath (Join-Path $vendor 'mpv/libmpv-2.dll') -Algorithm SHA256).Hash -ne $lock.mpv.dll_sha256) { throw 'libmpv DLL SHA256 mismatch' }
$null = Get-Asset $lock.yt_dlp.url (Join-Path $vendor 'yt-dlp.exe') $lock.yt_dlp.sha256
$null = Get-Asset $lock.json.url (Join-Path $vendor 'include/json.hpp') $lock.json.sha256
$sdk = Join-Path $vendor ('downloads/microsoft.web.webview2.' + $lock.webview2.version + '.nupkg')
$null = Get-Asset $lock.webview2.url $sdk $lock.webview2.sha256
Add-Type -AssemblyName System.IO.Compression.FileSystem
$webview = Join-Path $vendor 'webview2'
New-Item -ItemType Directory -Force -Path (Join-Path $webview 'include'),(Join-Path $webview 'x64'),(Join-Path $webview 'notices') | Out-Null
$entries = @{
    'build/native/include/WebView2.h' = 'include/WebView2.h'
    'build/native/x64/WebView2Loader.dll' = 'x64/WebView2Loader.dll'
    'LICENSE.txt' = 'notices/WebView2-LICENSE.txt'
    'NOTICE.txt' = 'notices/WebView2-NOTICE.txt'
}
$zip = [IO.Compression.ZipFile]::OpenRead($sdk)
try {
    foreach ($entry in $entries.GetEnumerator()) {
        $file = $zip.GetEntry($entry.Key)
        if ($null -eq $file) { throw "Missing WebView2 SDK member: $($entry.Key)" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($file, (Join-Path $webview $entry.Value), $true)
    }
} finally { $zip.Dispose() }
Write-Output ('Pinned dependencies verified: mpv ' + $lock.mpv.tag + ', yt-dlp ' + $lock.yt_dlp.tag + ', WebView2 SDK ' + $lock.webview2.version)
