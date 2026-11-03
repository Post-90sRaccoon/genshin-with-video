$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$directory=Join-Path $root 'licenses'
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$files=@{
 'https://www.gnu.org/licenses/gpl-3.0.txt'=(Join-Path $root 'LICENSE')
 'https://raw.githubusercontent.com/mpv-player/mpv/e471496c56/Copyright'=(Join-Path $directory 'mpv-Copyright')
 'https://raw.githubusercontent.com/mpv-player/mpv/e471496c56/LICENSE.GPL'=(Join-Path $directory 'mpv-LICENSE.GPL')
 'https://raw.githubusercontent.com/mpv-player/mpv/e471496c56/LICENSE.LGPL'=(Join-Path $directory 'mpv-LICENSE.LGPL')
 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT'=(Join-Path $directory 'json-LICENSE.MIT')
 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/2026.10.26/LICENSE'=(Join-Path $directory 'yt-dlp-LICENSE')
 'https://www.7-zip.org/license.txt'=(Join-Path $directory '7zip-license.txt')
}
foreach($url in $files.Keys){if(!(Test-Path -LiteralPath $files[$url])){Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $files[$url]}}
Write-Output ('License files ready: '+$files.Count)
