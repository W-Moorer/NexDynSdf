param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repository 'build-install-source'
$prefix = Join-Path $repository 'out-install-prefix'
$consumer = Join-Path $repository 'build-install-consumer-cxx-link'

cmake -S $repository -B $build `
    "-DNEXSDF_BUILD_TESTS=OFF" `
    "-DNEXSDF_BUILD_TOOLS=OFF" `
    "-DNEXSDF_BUILD_SHARED=OFF"
if ($LASTEXITCODE -ne 0) { throw 'Package configure failed.' }

cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Package build failed.' }

cmake --install $build --config $Configuration --prefix $prefix
if ($LASTEXITCODE -ne 0) { throw 'Package install failed.' }

cmake -S (Join-Path $repository 'tests/install_consumer') -B $consumer `
    "-DCMAKE_PREFIX_PATH=$prefix"
if ($LASTEXITCODE -ne 0) { throw 'Consumer configure failed.' }

cmake --build $consumer --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Consumer build failed.' }

$executable = Join-Path $consumer "$Configuration/nexsdf_install_consumer.exe"
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Installed-package consumer failed.' }
