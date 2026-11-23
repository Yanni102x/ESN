param([switch]$Clean)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null

if (Test-Path $vswhere) {
  $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($vsPath) { $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat" }
}
if (-not $vcvars -or -not (Test-Path $vcvars)) {
  $fallback = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  $fallback2 = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  if (Test-Path $fallback) { $vcvars = $fallback }
  elseif (Test-Path $fallback2) { $vcvars = $fallback2 }
}
if (-not $vcvars) { throw "找不到 vcvars64.bat，请安装 Visual Studio 2022 Build Tools" }

if ($Clean) { Remove-Item (Join-Path $root "build") -Recurse -Force -ErrorAction SilentlyContinue }

$build = Join-Path $root "build"
$gen = if (Test-Path (Join-Path $build "CMakeCache.txt")) { "" } else { "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release" }

$cmd = @(
  "call `"$vcvars`" >nul",
  "cd /d `"$root`"",
  "cmake -S . -B build $($gen -join ' ')",
  "cmake --build build"
) -join " && "

cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "构建失败（exit $LASTEXITCODE）" }

$exe = Join-Path $build "dist\ESN.exe"
Write-Host ""
Write-Host "OK: $exe" -ForegroundColor Green
if (Test-Path $exe) {
  $size = [math]::Round((Get-Item $exe).Length / 1KB, 0)
  Write-Host "    大小: $size KB"
}
