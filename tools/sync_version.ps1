<#
.SYNOPSIS
    Sincroniza la version de la app: toma la mas alta entre CMakeLists.txt y el primer "vX.YY:" de
    Docs/Changelog.md y la escribe en los tres lugares (CMakeLists.txt, el changelog y VERSION).
.DESCRIPTION
    Modelo de version continua de las apps LGA (el de LinkRedirector): cada entrada nueva del
    changelog ES una version nueva. El orden importa: PRIMERO la entrada nueva con su encabezado
    "vX.YY:", DESPUES este script. Al reves, el script le renombraria el encabezado a una entrada ya
    publicada.

    En esta app la version va de a centesimos: 2.01, 2.02... Si el numero no tiene exactamente dos
    decimales el script falla, porque "2.1" y "2.10" se leen distinto segun quien compare.

    Sin Python a proposito: la app no depende de nada instalado en la maquina salvo Qt.
.PARAMETER Check
    No escribe nada. Sale 1 si algun lugar difiere de la version resuelta.
#>
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$changelogPath = Join-Path $root 'Docs\Changelog.md'
$cmakePath = Join-Path $root 'CMakeLists.txt'
$versionPath = Join-Path $root 'VERSION'
$utf8 = New-Object System.Text.UTF8Encoding($false)

$headerPattern = '(?m)^v([0-9]+\.[0-9]+)\s*:'
$cmakePattern = 'project\(\s*LGA_MightyTools\s+VERSION\s+([0-9]+\.[0-9]+)'

function Get-Key([string]$version) {
    $parts = $version.Split('.')
    return [int]$parts[0] * 1000 + [int]$parts[1]
}

try {
    $changelog = [IO.File]::ReadAllText($changelogPath, $utf8)
    $cmake = [IO.File]::ReadAllText($cmakePath, $utf8)

    $m = [regex]::Match($changelog, $headerPattern)
    if (-not $m.Success) { throw 'No se encontro un encabezado "vX.YY:" en Docs/Changelog.md' }
    $changelogVersion = $m.Groups[1].Value
    $m = [regex]::Match($cmake, $cmakePattern)
    if (-not $m.Success) { throw 'No se encontro project(LGA_MightyTools VERSION x.yy) en CMakeLists.txt' }
    $cmakeVersion = $m.Groups[1].Value

    foreach ($v in @($changelogVersion, $cmakeVersion)) {
        if ($v -notmatch '^[0-9]+\.[0-9]{2}$') {
            throw "La version '$v' no tiene dos decimales (se numera 2.01, 2.02...)"
        }
    }

    $resolved = if ((Get-Key $changelogVersion) -ge (Get-Key $cmakeVersion)) { $changelogVersion } else { $cmakeVersion }
    $versionFile = if (Test-Path $versionPath) { ([IO.File]::ReadAllText($versionPath, $utf8)).Trim() } else { $null }

    if ($Check) {
        $diffs = @()
        if ($cmakeVersion -ne $resolved) { $diffs += "CMakeLists.txt ($cmakeVersion)" }
        if ($changelogVersion -ne $resolved) { $diffs += "Docs/Changelog.md ($changelogVersion)" }
        if ($versionFile -ne $resolved) { $diffs += "VERSION ($(if ($versionFile) { $versionFile } else { 'falta' }))" }
        if ($diffs.Count -gt 0) {
            Write-Output "[sync_version] ERROR: desincronizado contra $resolved."
            $diffs | ForEach-Object { Write-Output "    $_" }
            exit 1
        }
        Write-Output "[sync_version] OK: todo en $resolved."
        exit 0
    }

    $newChangelog = ([regex]$headerPattern).Replace($changelog, "v${resolved}:", 1)
    $newCmake = ([regex]$cmakePattern).Replace($cmake, { param($x) $x.Value.Substring(0, $x.Value.Length - $x.Groups[1].Value.Length) + $resolved }, 1)
    if ($newChangelog -ne $changelog) { [IO.File]::WriteAllText($changelogPath, $newChangelog, $utf8) }
    if ($newCmake -ne $cmake) { [IO.File]::WriteAllText($cmakePath, $newCmake, $utf8) }
    if ($versionFile -ne $resolved) { [IO.File]::WriteAllText($versionPath, "$resolved`n", $utf8) }

    Write-Output "[sync_version] CMake: $cmakeVersion | Changelog: $changelogVersion | Resuelta: $resolved"
    Write-Output '[sync_version] Sincronizados: CMakeLists.txt, Docs/Changelog.md, VERSION'
    exit 0
}
catch {
    Write-Output "[sync_version] ERROR: $($_.Exception.Message)"
    exit 1
}
