param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$Shared
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$suffix = if ($Shared) { '-shared-final' } else { '-static-final' }
$build = Join-Path $repository ("build-test" + $suffix)
$sharedValue = if ($Shared) { 'ON' } else { 'OFF' }

cmake -S $repository -B $build `
    "-DNEXSDF_BUILD_TESTS=ON" `
    "-DNEXSDF_BUILD_TOOLS=ON" `
    "-DNEXSDF_BUILD_SHARED=$sharedValue"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

ctest --test-dir $build -C $Configuration -L unit --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }

ctest --test-dir $build -C $Configuration -L regression --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed.' }
