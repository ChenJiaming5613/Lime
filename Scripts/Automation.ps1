# Runs the Python automation suite or a project script.
#
# A thin wrapper so the automation entry points look like the other scripts in this directory. It only
# resolves Python and forwards arguments; the logic lives in Automation/lime_automation.

[CmdletBinding()]
param(
	# test: the pytest suite. run/list/info/shell: the lime_automation CLI.
	[Parameter(Position = 0)]
	[ValidateSet('test', 'run', 'list', 'info', 'shell')]
	[string]$Action = 'test',

	[string[]]$Backend,
	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Config = 'Debug',
	[string]$Project = 'HelloTriangle',

	# Everything after the known parameters is handed to pytest or the CLI unchanged.
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]]$Remaining
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$AutomationDir = Join-Path $RepoRoot 'Automation'

$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python)
{
	$Python = Get-Command py -ErrorAction SilentlyContinue
	if (-not $Python)
	{
		throw 'No Python interpreter found. Install Python 3.10 or newer.'
	}
}

Push-Location $AutomationDir
try
{
	if ($Action -eq 'test')
	{
		& $Python.Source -c 'import pytest' 2>$null
		if ($LASTEXITCODE -ne 0)
		{
			throw "pytest is not installed. Run: $($Python.Source) -m pip install -r Automation/requirements.txt"
		}

		$Arguments = @('-m', 'pytest', '--config', $Config)
		foreach ($Item in $Backend)
		{
			$Arguments += @('--backend', $Item)
		}
		$Arguments += @('--project', $Project)
		if ($Remaining) { $Arguments += $Remaining }

		& $Python.Source @Arguments
		exit $LASTEXITCODE
	}

	$Arguments = @('-m', 'lime_automation', $Action, '--config', $Config, '--project', $Project)
	# The CLI takes a single backend, unlike pytest which accepts a matrix.
	if ($Backend) { $Arguments += @('--backend', $Backend[0]) }
	if ($Remaining) { $Arguments += $Remaining }

	& $Python.Source @Arguments
	exit $LASTEXITCODE
}
finally
{
	Pop-Location
}
