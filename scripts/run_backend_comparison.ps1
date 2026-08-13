param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$Model = 'models/pycoco/obj_library/cube.obj',
    [string]$OutputDirectory = 'out-backend-comparison',
    [int]$Resolution = 48,
    [int]$Threads = 8,
    [switch]$Cuda
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repository $(if ($Cuda) { 'build-backend-cuda' } else { 'build-backend-cpu' })
$cudaValue = if ($Cuda) { 'ON' } else { 'OFF' }
cmake -S $repository -B $build '-DNEXSDF_BUILD_TESTS=ON' '-DNEXSDF_BUILD_TOOLS=ON' `
    "-DNEXSDF_ENABLE_CUDA=$cudaValue"
if ($LASTEXITCODE -ne 0) { throw 'Backend comparison configure failed.' }
cmake --build $build --config $Configuration --target nexsdfvalidate --parallel
if ($LASTEXITCODE -ne 0) { throw 'Backend comparison build failed.' }

$validator = Join-Path $build "$Configuration/nexsdfvalidate.exe"
$destination = Join-Path $repository $OutputDirectory
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$output = Join-Path $destination 'backends.tsv'
$cases = @(
    @('scalar', 1, 'scalar', 1),
    @('parallel', $Threads, 'scalar', $Threads),
    @('parallel', $Threads, 'simd', 1)
)
if ($Cuda) { $cases += ,@('cuda', 1, 'cuda', 1) }

$first = $true
foreach ($case in $cases) {
    $arguments = @(
        $Model, $output,
        '--representation', 'grid', '--reconstruction', 'trilinear',
        '--resolution', $Resolution,
        '--field-samples', 4096, '--surface-samples', 4096,
        '--query-repetitions', 20, '--seed', 20260813,
        '--build-backend', $case[0], '--threads', $case[1],
        '--query-backend', $case[2], '--query-threads', $case[3]
    )
    if (-not $first) { $arguments += '--append' }
    Push-Location $repository
    try {
        & $validator @arguments
        if ($LASTEXITCODE -ne 0) { throw "Backend validation failed for $($case -join '/')." }
    } finally { Pop-Location }
    $first = $false
}
Write-Output "Backend comparison written to $output"
