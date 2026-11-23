param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(ESN VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
  throw "无法从 CMakeLists.txt 读取版本号"
}
$version = $Matches[1]
Write-Host "ESN $version" -ForegroundColor Cyan

if (-not $SkipBuild) {
  & (Join-Path $PSScriptRoot "build.ps1")
  if ($LASTEXITCODE -ne 0) { throw "构建失败" }
}

$exe = Join-Path $root "build\dist\ESN.exe"
if (-not (Test-Path $exe)) { throw "找不到 $exe" }

$outDir = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stage = Join-Path $outDir "ESN-$version-x64-portable"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe (Join-Path $stage "ESN.exe")
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "README.md") (Join-Path $stage "README.md")
# 便携标记：数据写到 exe 同目录的 data\
Set-Content -Path (Join-Path $stage "ESN.portable") -Value "portable" -Encoding ASCII

$zip = Join-Path $outDir "ESN-$version-x64-portable.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "portable: $zip" -ForegroundColor Green

$iscc = $null
foreach ($p in @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    (Get-Command iscc.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source))) {
  if ($p -and (Test-Path $p)) { $iscc = $p; break }
}
if ($iscc) {
  & $iscc "/DAppVersion=$version" (Join-Path $root "installer\ESN.iss") | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "Inno Setup 打包失败" }
  Write-Host "installer: dist\ESN-$version-x64-setup.exe" -ForegroundColor Green
} else {
  Write-Host "未找到 Inno Setup（免费），跳过安装包；装好后运行本脚本即可生成 setup.exe" -ForegroundColor Yellow
}

$files = Get-ChildItem $outDir -File | Where-Object { $_.Extension -in ".zip", ".exe" }
$lines = foreach ($f in $files) {
  $hash = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
  "$hash  $($f.Name)"
}
Set-Content -Path (Join-Path $outDir "SHA256SUMS.txt") -Value ($lines -join "`n") -Encoding ASCII
Write-Host "checksums: dist\SHA256SUMS.txt" -ForegroundColor Green
Get-ChildItem $outDir -File | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 2) } } | Format-Table -AutoSize
