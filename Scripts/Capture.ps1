# Captures a screenshot of a running project window, for verifying rendering changes.
#
# Usage:
#   ./Scripts/Capture.ps1                                    # Debug, D3D12, editor enabled
#   ./Scripts/Capture.ps1 -Backend vulkan
#   ./Scripts/Capture.ps1 -NoEditor -Output noeditor.png
#   ./Scripts/Capture.ps1 -Config Release -KeepLayout
#
# Screenshots land in Build/Screenshots, which is git ignored.

[CmdletBinding()]
param(
	[ValidateSet('d3d12', 'vulkan')]
	[string]$Backend = 'd3d12',

	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Config = 'Debug',

	[string]$Project = 'HelloTriangle',
	[string]$Output = '',
	[int]$Width = 1200,
	[int]$Height = 675,

	# Runs without the editor, so the scene goes straight to the swap chain.
	[switch]$NoEditor,
	# Keeps Saved/EditorLayout.ini instead of starting from the default dock layout.
	[switch]$KeepLayout,
	# Seconds to wait after the window appears, so the first frames and the layout settle.
	[int]$SettleSeconds = 4,
	# Leaves the process running instead of closing it after the capture.
	[switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Per-monitor DPI awareness. Without it GetWindowRect reports logical coordinates that do not match
# the physical pixels CopyFromScreen reads, and the capture ends up offset and cropped.
$InteropSource = @"
using System;
using System.Runtime.InteropServices;
public static class LimeCaptureNative {
	[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
	public struct RECT { public int Left, Top, Right, Bottom; }
	public static readonly IntPtr PerMonitorAwareV2 = new IntPtr(-4);
}
"@
if (-not ('LimeCaptureNative' -as [type]))
{
	Add-Type -TypeDefinition $InteropSource
}
[LimeCaptureNative]::SetProcessDpiAwarenessContext([LimeCaptureNative]::PerMonitorAwareV2) | Out-Null

$RootDir = Split-Path -Parent $PSScriptRoot
$ExeDir = Join-Path $RootDir "Build/ninja/Bin/$Config"
$ExePath = Join-Path $ExeDir "$Project.exe"

if (-not (Test-Path $ExePath))
{
	Write-Error "$ExePath not found. Build it first: ./Scripts/Build.ps1 -Config $Config"
}

if (-not $KeepLayout)
{
	# A stale layout would hide newly added panels, which is usually the opposite of what a capture
	# is meant to verify.
	$LayoutFile = Join-Path $ExeDir 'Saved/EditorLayout.ini'
	if (Test-Path $LayoutFile)
	{
		Remove-Item $LayoutFile
	}
}

if (-not $Output)
{
	$Mode = if ($NoEditor) { 'noeditor' } else { 'editor' }
	$Output = "$Project-$Backend-$Mode.png"
}

$ShotDir = Join-Path $RootDir 'Build/Screenshots'
New-Item -ItemType Directory -Force -Path $ShotDir | Out-Null
$ShotPath = Join-Path $ShotDir $Output

$Arguments = @("--rhi=$Backend", "--width=$Width", "--height=$Height")
if ($NoEditor)
{
	$Arguments += '--no-editor'
}

$LogPath = Join-Path $ShotDir "$([System.IO.Path]::GetFileNameWithoutExtension($Output)).log"
$Process = Start-Process -FilePath $ExePath -ArgumentList $Arguments -PassThru `
	-WorkingDirectory $ExeDir -RedirectStandardOutput $LogPath

try
{
	# The window handle only becomes available once the message loop is up.
	$Window = [IntPtr]::Zero
	for ($Attempt = 0; $Attempt -lt 25 -and $Window -eq [IntPtr]::Zero; $Attempt++)
	{
		Start-Sleep -Milliseconds 500
		if ($Process.HasExited)
		{
			Write-Host "Process exited early with code $($Process.ExitCode). Log:" -ForegroundColor Red
			Get-Content $LogPath -ErrorAction SilentlyContinue | Select-Object -Last 20
			exit 1
		}
		$Process.Refresh()
		$Window = $Process.MainWindowHandle
	}

	if ($Window -eq [IntPtr]::Zero)
	{
		Write-Error 'Timed out waiting for the application window'
	}

	[LimeCaptureNative]::SetForegroundWindow($Window) | Out-Null
	Start-Sleep -Seconds $SettleSeconds

	# The window is captured where it already is: moving it would trigger a resize and rebuild the
	# dock layout mid-capture.
	$Rect = New-Object LimeCaptureNative+RECT
	[LimeCaptureNative]::GetWindowRect($Window, [ref]$Rect) | Out-Null
	$CaptureWidth = $Rect.Right - $Rect.Left
	$CaptureHeight = $Rect.Bottom - $Rect.Top

	$Bitmap = New-Object System.Drawing.Bitmap $CaptureWidth, $CaptureHeight
	try
	{
		$Graphics = [System.Drawing.Graphics]::FromImage($Bitmap)
		try
		{
			$Graphics.CopyFromScreen($Rect.Left, $Rect.Top, 0, 0, $Bitmap.Size)
		}
		finally
		{
			$Graphics.Dispose()
		}
		$Bitmap.Save($ShotPath, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally
	{
		$Bitmap.Dispose()
	}

	Write-Host "Saved $ShotPath ($CaptureWidth x $CaptureHeight)" -ForegroundColor Green

	$Issues = Get-Content $LogPath -ErrorAction SilentlyContinue | Select-String -Pattern '\[error\]|\[warning\]|\[critical\]'
	if ($Issues)
	{
		Write-Host "$($Issues.Count) issue(s) in the log:" -ForegroundColor Yellow
		$Issues | Select-Object -First 10 | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
	}
	else
	{
		Write-Host 'No warnings or errors logged.' -ForegroundColor Green
	}
}
finally
{
	if (-not $KeepRunning -and -not $Process.HasExited)
	{
		Stop-Process -Id $Process.Id -Force
	}
}
