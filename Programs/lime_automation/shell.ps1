# Opens an interactive Python shell against a running or freshly launched engine.
#
# One entry point for the common case: attach to whatever engine is already running, or start one
# when there is none. The heavy lifting lives in the lime_automation package; this only resolves
# Python, puts the package on the path and forwards the arguments.
#
# Parameters are declared rather than passed through as a raw remainder. PowerShell parses tokens
# starting with "-" before a script sees them, so a bare "--attach" reaches the script mangled and
# the underlying argparse never gets it. Declaring them means "-Port 5613" works the way the rest of
# the scripts in this repository do.
#
#   ./shell.ps1                     # attach to a running engine, or launch one
#   ./shell.ps1 -Port 5613          # attach to a specific port, skipping discovery
#   ./shell.ps1 -Launch             # always start a new engine
#   ./shell.ps1 -Launch -Backend vulkan -NoEditor

[CmdletBinding()]
param(
	# Attach to this port directly. Useful when several engines run, or when the endpoint file is
	# missing because the engine came from a different build tree.
	[int]$Port,

	# Start a new engine instead of attaching to one. Its window closes when the shell exits.
	[switch]$Launch,

	[ValidateSet('d3d12', 'vulkan')]
	[string]$Backend,
	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Config = 'Debug',
	[string]$Project = 'HelloTriangle',
	[switch]$NoEditor,

	# Anything else is handed to the CLI unchanged.
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]]$Remaining
)

$ErrorActionPreference = 'Stop'

# The script sits inside the package, so its parent is Programs, which is what has to be importable.
$ProgramsDir = Split-Path -Parent $PSScriptRoot

$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python)
{
	$Python = Get-Command py -ErrorAction SilentlyContinue
	if (-not $Python)
	{
		throw 'No Python interpreter found. Install Python 3.10 or newer.'
	}
}

$Arguments = @('-m', 'lime_automation', 'shell', '--config', $Config, '--project', $Project)

if ($PSBoundParameters.ContainsKey('Port'))
{
	# The CLI treats a port as implying attach, so -Launch and -Port together are contradictory.
	if ($Launch)
	{
		throw 'Use either -Port to attach to an existing engine or -Launch to start a new one, not both.'
	}
	$Arguments += @('--port', $Port)
}
elseif (-not $Launch)
{
	$Arguments += '--attach'
}

if ($Backend) { $Arguments += @('--backend', $Backend) }
if ($NoEditor) { $Arguments += '--no-editor' }
if ($Remaining) { $Arguments += $Remaining }

# Importable without installing, which keeps a checkout usable straight away.
$OriginalPythonPath = $env:PYTHONPATH
if ($OriginalPythonPath)
{
	$env:PYTHONPATH = "$ProgramsDir;$OriginalPythonPath"
}
else
{
	$env:PYTHONPATH = $ProgramsDir
}

try
{
	& $Python.Source @Arguments
	exit $LASTEXITCODE
}
finally
{
	$env:PYTHONPATH = $OriginalPythonPath
}
