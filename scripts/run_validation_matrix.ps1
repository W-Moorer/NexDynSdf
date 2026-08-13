param(
    [ValidateSet('Smoke', 'Full')]
    [string]$Profile = 'Smoke',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$OutputDirectory = '',
    [int]$Seed = 20260813
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repository 'build-validation'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repository 'out-validation-matrix'
}

cmake -S $repository -B $build '-DNEXSDF_BUILD_TESTS=ON' '-DNEXSDF_BUILD_TOOLS=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
cmake --build $build --config $Configuration --target nexsdfvalidate --parallel
if ($LASTEXITCODE -ne 0) { throw 'nexsdfvalidate build failed.' }

$validator = Join-Path $build "$Configuration/nexsdfvalidate.exe"
if (-not (Test-Path -LiteralPath $validator)) {
    $validator = Join-Path $build 'nexsdfvalidate'
}
if (-not (Test-Path -LiteralPath $validator)) { throw "Validator not found: $validator" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

if ($Profile -eq 'Full') {
    $resolutions = @(32, 64, 128, 256)
    $fieldSamples = 16384
    $surfaceSamples = 16384
    $models = @(
        'models/pycoco/obj_library/sphere.obj',
        'models/pycoco/obj_library/cone.obj',
        'models/sdfmodel/nsm/cam.nsm',
        'models/sdfmodel/nsm/gear.nsm'
    )
} else {
    $resolutions = @(16, 32)
    $fieldSamples = 1024
    $surfaceSamples = 1024
    $models = @(
        'models/pycoco/obj_library/cube.obj',
        'models/pycoco/obj_library/sphere.obj'
    )
}

$reconstructions = @('trilinear', 'tricubic', 'gradient')
$combinedOutput = Join-Path $OutputDirectory 'validation-matrix.tsv'
$firstResult = $true
foreach ($modelRelative in $models) {
    $model = Join-Path $repository $modelRelative
    if (-not (Test-Path -LiteralPath $model)) { throw "Model not found: $model" }
    $modelName = [System.IO.Path]::GetFileNameWithoutExtension($model)
    foreach ($reconstruction in $reconstructions) {
        foreach ($resolution in $resolutions) {
            $arguments = @(
                $modelRelative, $combinedOutput,
                '--representation', 'grid', '--reconstruction', $reconstruction,
                '--resolution', $resolution,
                '--field-samples', $fieldSamples, '--surface-samples', $surfaceSamples,
                '--query-repetitions', 10, '--seed', $Seed
            )
            if (-not $firstResult) { $arguments += '--append' }
            Push-Location $repository
            try {
                & $validator @arguments
                if ($LASTEXITCODE -ne 0) {
                    throw "Validation failed for $modelName/$reconstruction/$resolution."
                }
            } finally {
                Pop-Location
            }
            $firstResult = $false
        }
    }
}

Write-Host "Validation matrix written to $OutputDirectory"
