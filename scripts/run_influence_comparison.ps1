param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$Model = 'models/sdflib/Gear.obj',
    [string]$Output = 'out-influence-comparison/influence.tsv',
    [int]$Seed = 20260813
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repository 'build-validation'
cmake -S $repository -B $build '-DNEXSDF_BUILD_TESTS=ON' '-DNEXSDF_BUILD_TOOLS=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
cmake --build $build --config $Configuration --target nexsdfvalidate --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'nexsdfvalidate build failed.' }

$validator = Join-Path $build "$Configuration/nexsdfvalidate.exe"
if (-not (Test-Path -LiteralPath $validator)) { $validator = Join-Path $build 'nexsdfvalidate' }
$destination = Join-Path $repository $Output
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null

$first = $true
foreach ($filter in @('aabb', 'gjk', 'frank-wolfe')) {
    $arguments = @(
        $Model, $Output,
        '--representation', 'exact-octree', '--reconstruction', 'exact',
        '--influence', $filter, '--start-depth', 1, '--max-depth', 5,
        '--max-triangles', 64, '--field-samples', 4096,
        '--surface-samples', 4096, '--query-repetitions', 10, '--seed', $Seed
    )
    if (-not $first) { $arguments += '--append' }
    Push-Location $repository
    try {
        & $validator @arguments
        if ($LASTEXITCODE -ne 0) { throw "Influence validation failed for $filter." }
    } finally {
        Pop-Location
    }
    $first = $false
}

Write-Host "Influence comparison written to $destination"
