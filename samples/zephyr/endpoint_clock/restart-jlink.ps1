# SPDX-License-Identifier: Apache-2.0
# Restart one Windows J-Link device binding; this does not power-cycle USB.
[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('(?i)^USB\\VID_1366&PID_[0-9A-F]{4}\\[^\\*?]+$')]
  [string]$InstanceId
)

$ErrorActionPreference = 'Stop'
$devices = @(Get-PnpDevice -InstanceId $InstanceId -ErrorAction Stop |
  Where-Object { $_.InstanceId -eq $InstanceId })
if ($devices.Count -ne 1) { throw 'Specify exactly one J-Link USB instance ID.' }
$device = $devices[0]
$device | Format-List Status, FriendlyName, InstanceId
if (!$PSCmdlet.ShouldProcess($device.InstanceId, 'Restart J-Link device binding')) {
  return
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run from an elevated PowerShell or administrator SSH session.'
}

# No parent hub, driver replacement, /force, /reboot, or automatic retry loop.
# Close the debugger first; do not interrupt an active flash operation.
& "$env:SystemRoot\System32\pnputil.exe" /restart-device $device.InstanceId
$restartCode = $LASTEXITCODE
if ($restartCode -ne 0) {
  throw "PnP restart returned $restartCode. No computer reboot was requested."
}

$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
  Start-Sleep -Milliseconds 250
  $ready = @(Get-PnpDevice -PresentOnly -InstanceId $InstanceId -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -eq $InstanceId -and $_.Status -eq 'OK' })
  if ($ready.Count -eq 1) {
    Write-Output 'J-Link is present and reports OK. Retry the debugger connection.'
    return
  }
} while ([DateTime]::UtcNow -lt $deadline)
throw 'J-Link did not return ready within 10 seconds; unplug/replug the probe.'
