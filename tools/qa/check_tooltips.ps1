<#
.SYNOPSIS
    Falla si queda un tooltip nativo de Qt en el codigo de la app.
.DESCRIPTION
    Los tooltips de la app son propios (src/ui/CustomTooltip): se registran con
    CustomTooltip::instance()->setToolTip(widget, texto). El setToolTip nativo de Qt y QToolTip no se usan.
    Unica excepcion: el icono de la bandeja (QSystemTrayIcon), cuyo tooltip lo dibuja el sistema.

    Uso:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\qa\check_tooltips.ps1
    Sale 0 si no hay ninguno, 1 si encuentra alguno (los lista con archivo y linea).
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src = Join-Path $root 'src'

# Lo que se permite: el componente propio y el tooltip del icono de la bandeja.
$allowedFiles = @('src\ui\CustomTooltip.cpp', 'src\ui\CustomTooltip.h')
$allowedLines = @('m_tray->setToolTip(')

$found = @()
Get-ChildItem -Path $src -Recurse -Include *.cpp, *.h, *.mm | ForEach-Object {
    $relative = $_.FullName.Substring($root.Length + 1)
    if ($allowedFiles -contains $relative) { return }
    $number = 0
    foreach ($line in [System.IO.File]::ReadAllLines($_.FullName)) {
        $number++
        $code = $line.Trim()
        if ($code.StartsWith('//')) { continue }
        $native = ($code -match '(?<!CustomTooltip::instance\(\))->setToolTip\(' -or $code -match '(^|[^>:\w])setToolTip\(' -or $code -match '\bQToolTip\b')
        if (-not $native) { continue }
        $allowed = $false
        foreach ($ok in $allowedLines) { if ($code.Contains($ok)) { $allowed = $true } }
        if (-not $allowed) { $found += ('{0}:{1}  {2}' -f $relative, $number, $code) }
    }
}

if ($found.Count -gt 0) {
    Write-Host ('check_tooltips: {0} tooltips nativos (usar CustomTooltip::instance()->setToolTip)' -f $found.Count)
    $found | ForEach-Object { Write-Host ('  ' + $_) }
    exit 1
}
Write-Host 'check_tooltips: ningun tooltip nativo'
exit 0
