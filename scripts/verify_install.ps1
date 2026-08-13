param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repository 'build-install-source'
$prefix = Join-Path $repository 'out-install-prefix'
$consumer = Join-Path $repository 'build-install-consumer-cxx-link'

# The install tree is disposable validation output. Recreate it so removed or
# renamed model assets cannot survive from an older catalog and mask a package
# layout regression.
$repositoryFull = [IO.Path]::GetFullPath($repository).TrimEnd('\') + '\'
$prefixFull = [IO.Path]::GetFullPath($prefix)
if (-not $prefixFull.StartsWith($repositoryFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean install prefix outside the repository: $prefixFull"
}
if (Test-Path -LiteralPath $prefixFull) {
    Remove-Item -LiteralPath $prefixFull -Recurse -Force
}

cmake -S $repository -B $build `
    "-DNEXSDF_BUILD_TESTS=OFF" `
    "-DNEXSDF_BUILD_TOOLS=OFF" `
    "-DNEXSDF_BUILD_SHARED=OFF"
if ($LASTEXITCODE -ne 0) { throw 'Package configure failed.' }

cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Package build failed.' }

cmake --install $build --config $Configuration --prefix $prefix
if ($LASTEXITCODE -ne 0) { throw 'Package install failed.' }

$installedModels = Join-Path $prefix 'share/NexDynSdf/models'
$requiredModelFiles = @('MANIFEST.md', 'CATALOG.tsv', 'AUDIT.tsv',
    'pycoco/obj_model/complex_geometry/PressureLubricatedCam.obj',
    'sdfmodel/cam.nsm', 'sdfmodel/cam.stl',
    'nagata/cone.nsm', 'nagata/cone.eng',
    'sdflib/Gear.obj', 'licenses/SdfLib-LICENSE.txt')
foreach ($relativePath in $requiredModelFiles) {
    $installedPath = Join-Path $installedModels $relativePath
    if (-not (Test-Path -LiteralPath $installedPath -PathType Leaf)) {
        throw "Installed model file is missing: $relativePath"
    }
}

$catalogPath = Join-Path $installedModels 'CATALOG.tsv'
$catalog = @(Import-Csv -LiteralPath $catalogPath -Delimiter "`t")
if ($catalog.Count -ne 34) {
    throw "Installed catalog contains $($catalog.Count) assets; expected 34."
}
foreach ($row in $catalog) {
    $installedPath = Join-Path $installedModels ($row.path -replace '/', '\')
    if (-not (Test-Path -LiteralPath $installedPath -PathType Leaf)) {
        throw "Catalogued installed model is missing: $($row.path)"
    }
    $actualBytes = (Get-Item -LiteralPath $installedPath).Length
    if ($actualBytes -ne [long]$row.bytes) {
        throw "Installed model size differs from catalog: $($row.path)"
    }
    $actualHash = (Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $row.sha256) {
        throw "Installed model hash differs from catalog: $($row.path)"
    }
}

$forbiddenLicenseFiles = @(
    'licenses/PyCoCoFastSDF-LICENSE.txt',
    'licenses/enhanced-nagata-sdf-LICENSE.txt'
)
foreach ($relativePath in $forbiddenLicenseFiles) {
    $installedPath = Join-Path $installedModels $relativePath
    if (Test-Path -LiteralPath $installedPath) {
        throw "Unexpected first-party model license was installed: $relativePath"
    }
}

cmake -S (Join-Path $repository 'tests/install_consumer') -B $consumer `
    "-DCMAKE_PREFIX_PATH=$prefix"
if ($LASTEXITCODE -ne 0) { throw 'Consumer configure failed.' }

cmake --build $consumer --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Consumer build failed.' }

$executable = Join-Path $consumer "$Configuration/nexsdf_install_consumer.exe"
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Installed-package consumer failed.' }
Write-Output 'installed 34-asset model catalog, hashes, and ownership layout are valid'
