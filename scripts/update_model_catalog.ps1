param(
    [string]$PyCoCoRoot = 'E:\workspace\PyCoCoFastSDF',
    [string]$NagataRoot = 'E:\workspace\enhanced-nagata-sdf',
    [string]$SdfModelRoot = 'E:\workspace\SDFmodel',
    [string]$SdfLibRoot = 'E:\workspace\SdfLib'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$modelRoot = Join-Path $repository 'models'
$rows = [System.Collections.Generic.List[object]]::new()

function Get-Revision([string]$Root) {
    if (-not (Test-Path -LiteralPath (Join-Path $Root '.git'))) {
        throw "Source repository is missing: $Root"
    }
    $revision = (& git -C $Root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Cannot read source revision: $Root" }
    return $revision
}

function Add-Asset(
    [string]$CatalogPath,
    [string]$SourceProject,
    [string]$SourceRevision,
    [string]$SourceState,
    [string]$SourcePath,
    [string]$SourceFile,
    [string]$Ownership,
    [string]$License,
    [bool]$NexSdfInput
) {
    $catalogFile = Join-Path $modelRoot ($CatalogPath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $catalogFile -PathType Leaf)) {
        throw "Catalog asset is missing: $CatalogPath"
    }
    if (-not (Test-Path -LiteralPath $SourceFile -PathType Leaf)) {
        throw "Source asset is missing: $SourceFile"
    }
    $catalogHash = (Get-FileHash -LiteralPath $catalogFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceHash = (Get-FileHash -LiteralPath $SourceFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($catalogHash -ne $sourceHash) {
        throw "Catalog asset differs from source: $CatalogPath"
    }
    $extension = [IO.Path]::GetExtension($catalogFile).TrimStart('.').ToLowerInvariant()
    $rows.Add([pscustomobject]@{
        path = $CatalogPath
        format = $extension
        nexsdf_input = if ($NexSdfInput) { 'yes' } else { 'no' }
        ownership = $Ownership
        license = $License
        source_project = $SourceProject
        source_revision = $SourceRevision
        source_state = $SourceState
        source_path = $SourcePath
        sha256 = $catalogHash
        bytes = (Get-Item -LiteralPath $catalogFile).Length
    })
}

function Add-SelfGeneratedAsset([string]$CatalogPath, [string]$SourcePath) {
    $catalogFile = Join-Path $modelRoot ($CatalogPath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $catalogFile -PathType Leaf)) {
        throw "Generated catalog asset is missing: $CatalogPath"
    }
    $catalogHash = (Get-FileHash -LiteralPath $catalogFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $rows.Add([pscustomobject]@{
        path = $CatalogPath
        format = [IO.Path]::GetExtension($catalogFile).TrimStart('.').ToLowerInvariant()
        nexsdf_input = 'yes'
        ownership = 'first-party'
        license = 'no separate model license'
        source_project = 'NexDynSdf'
        source_revision = 'nexsdf-reference-models-v1'
        source_state = 'generated-reproducible'
        source_path = $SourcePath
        sha256 = $catalogHash
        bytes = (Get-Item -LiteralPath $catalogFile).Length
    })
}

$pycocoRevision = Get-Revision $PyCoCoRoot
$nagataRevision = Get-Revision $NagataRoot
$sdfModelRevision = Get-Revision $SdfModelRoot
$sdfLibRevision = Get-Revision $SdfLibRoot

foreach ($directory in @('obj_library', 'obj_model')) {
    $sourceDirectory = Join-Path $PyCoCoRoot $directory
    Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File -Filter '*.obj' |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($PyCoCoRoot.Length + 1).Replace('\', '/')
            Add-Asset "pycoco/$relative" 'PyCoCoFastSDF' $pycocoRevision 'tracked' `
                $relative $_.FullName 'first-party' 'no separate model license' $true
        }
}

foreach ($name in @('box.nsm', 'cone.nsm', 'sphere.nsm')) {
    $sourceFile = Join-Path $NagataRoot "models/$name"
    Add-Asset "nagata/$name" 'enhanced-nagata-sdf' $nagataRevision 'tracked' `
        "models/$name" $sourceFile 'first-party' 'no separate model license' $true
}
Add-Asset 'nagata/cone.eng' 'enhanced-nagata-sdf' $nagataRevision 'ignored-local-cache' `
    'models/cone.eng' (Join-Path $NagataRoot 'models/cone.eng') `
    'first-party' 'no separate model license' $false

$regenerator = Join-Path $repository 'scripts/regenerate_reference_models.py'
& python $regenerator --output-dir (Join-Path $modelRoot 'sdfmodel') --check
if ($LASTEXITCODE -ne 0) { throw 'Generated cam/gear models are stale.' }
Add-SelfGeneratedAsset 'sdfmodel/cam.nsm' `
    'scripts/regenerate_reference_models.py --output-dir models/sdfmodel'
Add-SelfGeneratedAsset 'sdfmodel/cam.stl' `
    'scripts/regenerate_reference_models.py --output-dir models/sdfmodel'
Add-SelfGeneratedAsset 'sdfmodel/gear.nsm' `
    'scripts/regenerate_reference_models.py --output-dir models/sdfmodel'

$sdfModelAssets = @(
    @('sdfmodel/validation_coarse.nsm', 'models/_val_coarse.nsm', 'experiments/nagata_eikonal_validation.py -> enhanced-nagata-sdf/models/_val_coarse.nsm'),
    @('sdfmodel/validation_fine.nsm', 'models/_val_fine.nsm', 'experiments/nagata_eikonal_validation.py -> enhanced-nagata-sdf/models/_val_fine.nsm')
)
foreach ($asset in $sdfModelAssets) {
    Add-Asset $asset[0] 'SDFmodel' $sdfModelRevision 'generated-untracked' $asset[2] `
        (Join-Path $NagataRoot $asset[1]) 'first-party' 'no separate model license' $true
}
Add-Asset 'sdflib/Gear.obj' 'SdfLib' $sdfLibRevision 'ignored-local-model' `
    'models/Gear.obj' (Join-Path $SdfLibRoot 'models/Gear.obj') `
    'third-party' 'models/licenses/SdfLib-LICENSE.txt' $true

$assetFiles = Get-ChildItem -LiteralPath $modelRoot -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in @('.obj', '.nsm', '.eng', '.stl') }
if ($assetFiles.Count -ne $rows.Count) {
    throw "Catalog rows ($($rows.Count)) do not cover every model asset ($($assetFiles.Count))."
}

$catalogPath = Join-Path $modelRoot 'CATALOG.tsv'
$lines = [System.Collections.Generic.List[string]]::new()
$columns = @('path', 'format', 'nexsdf_input', 'ownership', 'license',
    'source_project', 'source_revision', 'source_state', 'source_path', 'sha256', 'bytes')
$lines.Add(($columns -join "`t"))
foreach ($row in ($rows | Sort-Object path)) {
    $values = foreach ($column in $columns) {
        $value = [string]$row.$column
        if ($value.Contains("`t") -or $value.Contains("`r") -or $value.Contains("`n")) {
            throw "Catalog field contains unsupported whitespace: $column"
        }
        $value
    }
    $lines.Add(($values -join "`t"))
}
[IO.File]::WriteAllText($catalogPath, ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "wrote $catalogPath with $($rows.Count) assets"
