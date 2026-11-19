param([string]$Apk = $env:UU_REFERENCE_APK, [int]$Jobs = 6, [switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if (-not $env:DEVKITPRO) {
        $localSdk = Join-Path $projectRoot '.tools/sdk/opt/devkitpro'
        if (-not (Test-Path (Join-Path $localSdk 'devkitA64/bin/aarch64-none-elf-gcc.exe'))) {
            throw 'Set DEVKITPRO or run python scripts/bootstrap_sdk.py first.'
        }
        $env:DEVKITPRO = $localSdk.Replace('\', '/')
    }
    python scripts/fetch_deps.py
    if ($LASTEXITCODE) { throw 'Dependency download failed' }
    python scripts/patch_deps.py
    if ($LASTEXITCODE) { throw 'Dependency patch failed' }
    if (-not $CompileOnly) {
        if ($Apk) {
            python scripts/extract_key.py $Apk
            if ($LASTEXITCODE) { throw 'APK extraction failed' }
        } elseif (-not (Test-Path -LiteralPath 'romfs/signing.key')) {
            throw 'Provide -Apk with your UU TV 3.5.0 APK path.'
        }
    }
    $cmakeExe = (Get-Command cmake -ErrorAction Stop).Source
    $configure = @('-S', '.', '-B', 'build-nx', '-DCMAKE_TOOLCHAIN_FILE=cmake/switch.cmake', '-DCMAKE_BUILD_TYPE=Release')
    $cache = 'build-nx/CMakeCache.txt'
    $hasCache = Test-Path -LiteralPath $cache
    if (-not $hasCache) { $configure += @('-G', 'Ninja') }
    $localNinja = Join-Path $projectRoot '.tools/ninja/ninja.exe'
    if ((Test-Path -LiteralPath $localNinja) -and
        (-not $hasCache -or (Select-String -LiteralPath $cache -Pattern '^CMAKE_GENERATOR:INTERNAL=Ninja$' -Quiet))) {
        $configure += "-DCMAKE_MAKE_PROGRAM=$($localNinja.Replace('\','/'))"
    }
    & $cmakeExe @configure
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    $build = @('--build', 'build-nx', '-j', $Jobs)
    if ($CompileOnly) { $build += @('--target', 'UURemote-ns') }
    & $cmakeExe @build
    if ($LASTEXITCODE) { throw 'Switch build failed' }
    if (-not $CompileOnly) {
        python scripts/package.py
        if ($LASTEXITCODE) { throw 'Packaging failed' }
    }
} finally { Pop-Location }
