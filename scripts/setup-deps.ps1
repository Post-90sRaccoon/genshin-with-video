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
Write-Output ('Pinned dependencies verified: mpv ' + $lock.mpv.tag + ', yt-dlp ' + $lock.yt_dlp.tag)
