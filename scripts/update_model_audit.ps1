param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repository 'build-test-static-final'
}
$auditor = Join-Path $BuildDirectory "$Configuration/nexsdfmodelaudit.exe"
if (-not (Test-Path -LiteralPath $auditor -PathType Leaf)) {
    throw "Model auditor is missing: $auditor"
}

$modelRoot = Join-Path $repository 'models'
$lines = @(& $auditor $modelRoot --expect-files 32 --expect-ready 29)
if ($LASTEXITCODE -ne 0) { throw 'Model audit failed.' }
$content = ($lines -join "`n") + "`n"
$auditPath = Join-Path $modelRoot 'AUDIT.tsv'
[IO.File]::WriteAllText($auditPath, $content, [Text.UTF8Encoding]::new($false))
Write-Output "wrote $auditPath with $($lines.Count - 1) model rows"
