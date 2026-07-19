param(
    [switch]$TestOnly,
    [string]$ToolchainPath = $env:MSYS2_UCRT64
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$client = Join-Path $root 'client'

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

$raylib = Join-Path $toolchain 'lib\libraylib.a'
$glfwLibrary = Join-Path $toolchain 'lib\libglfw3.dll.a'
$glfwRuntime = Join-Path $toolchain 'bin\glfw3.dll'
foreach ($required in $gxx, $raylib, $glfwLibrary, $glfwRuntime) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required toolchain file was not found: $required"
    }
}

$msysRoot = Split-Path -Parent $toolchain
$env:Path = "$(Join-Path $toolchain 'bin');$(Join-Path $msysRoot 'usr\bin');$env:Path"
$testOutput = Join-Path $root 'build\tests'
$gameOutput = Join-Path $root 'dist\windows'
$fontSource = Join-Path $client 'assets\fonts'
$fontOutput = Join-Path $gameOutput 'assets\fonts'
$fontFiles = @(
    'AtkinsonHyperlegible-Regular.ttf',
    'AtkinsonHyperlegible-Bold.ttf',
    'AtkinsonHyperlegible-OFL.txt',
    'NotoEmoji.ttf',
    'NotoEmoji-OFL.txt'
)
foreach ($font in $fontFiles) {
    $fontPath = Join-Path $fontSource $font
    if (-not (Test-Path -LiteralPath $fontPath)) {
        throw "Required bundled font asset was not found: $fontPath"
    }
}
New-Item -ItemType Directory -Force -Path $testOutput, $gameOutput, $fontOutput | Out-Null

& $gxx -std=c++23 -O2 -Wall -Wextra -pedantic `
    (Join-Path $client 'tests\connect_four_tests.cpp') `
    "-I$(Join-Path $client 'src')" `
    -o (Join-Path $testOutput 'connect_four_tests.exe') -static
if ($LASTEXITCODE -ne 0) { throw 'Logic test compilation failed.' }
& (Join-Path $testOutput 'connect_four_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Connect Four logic tests failed.' }

if (-not $TestOnly) {
    $gameSources = Get-ChildItem (Join-Path $client 'src') -Recurse -Filter '*.cpp' |
        ForEach-Object { $_.FullName }
    & $gxx -std=c++23 -O3 -Wall -Wextra -pedantic `
        $gameSources `
        -o (Join-Path $gameOutput 'connect_four.exe') `
        "-I$(Join-Path $client 'src')" `
        "-I$toolchain\include" `
        $raylib `
        $glfwLibrary `
        -lopengl32 -lgdi32 -lwinmm -lwinhttp -static -mwindows
    if ($LASTEXITCODE -ne 0) { throw 'Game compilation failed.' }
    Copy-Item $glfwRuntime $gameOutput -Force
    foreach ($font in $fontFiles) {
        Copy-Item (Join-Path $fontSource $font) $fontOutput -Force
    }
    Write-Host "Built: $(Join-Path $gameOutput 'connect_four.exe')"
}
