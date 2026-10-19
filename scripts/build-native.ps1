param([int]$Jobs = 4, [switch]$Run)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$nativeSdk = (Join-Path $projectRoot '.tools/native-sdk/mingw64').Replace('\', '/')
if (-not (Test-Path -LiteralPath (Join-Path $nativeSdk 'bin/g++.exe'))) {
    throw 'Run python scripts/bootstrap_native_sdk.py first.'
}
$savedPath = $env:PATH
$savedPkgConfig = $env:PKG_CONFIG_LIBDIR
$pythonExe = (Get-Command python -ErrorAction Stop).Source
Push-Location $projectRoot
try {
    & $pythonExe scripts/fetch_deps.py
    if ($LASTEXITCODE) { throw 'Dependency download failed' }
    & $pythonExe scripts/patch_deps.py
    if ($LASTEXITCODE) { throw 'Dependency patch failed' }
    $env:PATH = (Join-Path $nativeSdk 'bin') + ';' + $env:PATH
    $env:PKG_CONFIG_LIBDIR = Join-Path $nativeSdk 'lib/pkgconfig'
    $cmakeExe = (Get-Command cmake -ErrorAction Stop).Source
    $ctestExe = (Get-Command ctest -ErrorAction Stop).Source
    $configure = @('-S', '.', '-B', 'build-native', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', '-DUU_NATIVE_TESTS=ON',
        "-DCMAKE_C_COMPILER=$nativeSdk/bin/gcc.exe", "-DCMAKE_CXX_COMPILER=$nativeSdk/bin/g++.exe",
        "-DPKG_CONFIG_EXECUTABLE=$nativeSdk/bin/pkgconf.exe")
    $localNinja = Join-Path $projectRoot '.tools/ninja/ninja.exe'
    if (Test-Path -LiteralPath $localNinja) { $configure += "-DCMAKE_MAKE_PROGRAM=$($localNinja.Replace('\', '/'))" }
    & $cmakeExe @configure
    if ($LASTEXITCODE) { throw 'Native configuration failed' }
    & $cmakeExe --build build-native -j $Jobs
    if ($LASTEXITCODE) { throw 'Native build failed' }
    & $ctestExe --test-dir build-native --output-on-failure
    if ($LASTEXITCODE) { throw 'Native media tests failed' }
    if ($Run) {
        if (-not (Test-Path -LiteralPath 'romfs/signing.key')) { throw 'Extract the signing key from your reference APK first.' }
        & (Join-Path $projectRoot 'build-native/UURemote-ns.exe')
    }
} finally {
    Pop-Location
    $env:PATH = $savedPath
    $env:PKG_CONFIG_LIBDIR = $savedPkgConfig
}
