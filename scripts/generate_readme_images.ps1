param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateRange(64, 2048)]
    [int]$ImageResolution = 512
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repository 'build-readme-visualization'
$scratch = Join-Path $repository 'out-readme-visualization'
$images = Join-Path $repository 'docs/images'

cmake -S $repository -B $build '-DNEXSDF_BUILD_TESTS=OFF' '-DNEXSDF_BUILD_TOOLS=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
cmake --build $build --config $Configuration --target nexsdfgen nexsdfviz --parallel
if ($LASTEXITCODE -ne 0) { throw 'Visualization tools build failed.' }

New-Item -ItemType Directory -Force -Path $scratch, $images | Out-Null
$generator = Join-Path $build "$Configuration/nexsdfgen.exe"
$visualizer = Join-Path $build "$Configuration/nexsdfviz.exe"
$converter = Join-Path $PSScriptRoot 'ppm_to_png.py'

function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$Failure) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw $Failure }
}

function Convert-Visualization([string]$Asset, [string]$Stem, [string[]]$Arguments) {
    $ppm = Join-Path $scratch "$Stem.ppm"
    $png = Join-Path $images "$Stem.png"
    Invoke-Checked $visualizer (@($Asset, $ppm, '--resolution', "$ImageResolution") + $Arguments) "Visualization failed: $Stem"
    Invoke-Checked 'python' @($converter, $ppm, $png) "PNG conversion failed: $Stem"
}

$sphere = Join-Path $scratch 'pycoco-sphere-gradient.nsdf'
Invoke-Checked $generator @(
    (Join-Path $repository 'models/pycoco/obj_library/sphere.obj'), $sphere,
    '--representation', 'grid', '--reconstruction', 'gradient', '--resolution', '64') 'Sphere asset generation failed.'
Convert-Visualization $sphere 'pycoco-sphere-gradient-distance' @('--axis', 'z', '--mode', 'distance')
Convert-Visualization $sphere 'pycoco-sphere-gradient-normal' @('--axis', 'z', '--mode', 'normal')

$cam = Join-Path $scratch 'pycoco-pressure-lubricated-cam-trilinear.nsdf'
Invoke-Checked $generator @(
    (Join-Path $repository 'models/pycoco/obj_model/complex_geometry/PressureLubricatedCam.obj'), $cam,
    '--representation', 'grid', '--reconstruction', 'trilinear', '--resolution', '64') 'Cam asset generation failed.'
Convert-Visualization $cam 'pycoco-pressure-lubricated-cam-distance' @('--axis', 'x', '--mode', 'distance')

$cone = Join-Path $scratch 'nagata-cone-adaptive.nsdf'
Invoke-Checked $generator @(
    (Join-Path $repository 'models/nagata/cone.nsm'), $cone,
    '--representation', 'adaptive-octree', '--reconstruction', 'tricubic',
    '--max-depth', '7', '--start-depth', '2', '--tolerance', '0.002') 'Cone asset generation failed.'
Convert-Visualization $cone 'nagata-cone-adaptive-depth' @('--axis', 'x', '--mode', 'depth')
Convert-Visualization $cone 'nagata-cone-adaptive-error' @('--axis', 'x', '--mode', 'error')

$gear = Join-Path $scratch 'sdflib-gear-exact.nsdf'
Invoke-Checked $generator @(
    (Join-Path $repository 'models/sdflib/Gear.obj'), $gear,
    '--representation', 'exact-octree', '--reconstruction', 'exact',
    '--max-depth', '8', '--start-depth', '2', '--max-triangles', '48') 'Gear asset generation failed.'
Convert-Visualization $gear 'sdflib-gear-exact-distance' @('--axis', 'z', '--mode', 'distance')

Write-Output "README images written to $images"
