param(
    [string]$ToolchainPath = $env:MSYS2_UCRT64
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ($ToolchainPath) {
    $toolchain = [System.IO.Path]::GetFullPath($ToolchainPath)
    $gxx = Join-Path $toolchain 'bin\g++.exe'
} else {
    $compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
    if (-not $compiler) {
        throw 'g++.exe was not found. Add MSYS2 UCRT64 bin to PATH or pass -ToolchainPath.'
    }
    $gxx = $compiler.Source
    $toolchain = Split-Path -Parent (Split-Path -Parent $gxx)
}

$msysRoot = Split-Path -Parent $toolchain
$env:Path = "$(Join-Path $toolchain 'bin');$(Join-Path $msysRoot 'usr\bin');$env:Path"
$outputDirectory = Join-Path $root 'build\tests'
$output = Join-Path $outputDirectory 'online_client_live_smoke.exe'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& $gxx -std=c++23 -O2 -Wall -Wextra -pedantic `
    (Join-Path $root 'client\tests\online_client_live_smoke.cpp') `
    "-I$(Join-Path $root 'client\src')" `
    -o $output -lwinhttp -static
if ($LASTEXITCODE -ne 0) { throw 'Native online smoke compilation failed.' }

& $output
if ($LASTEXITCODE -ne 0) { throw 'Native online lifecycle smoke failed.' }
