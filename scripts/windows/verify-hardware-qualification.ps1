[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$CampaignDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$campaign = (Resolve-Path $CampaignDirectory).Path
$verifier = Join-Path $root 'scripts\qualification\verify_qualification_campaign.py'
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $python) { throw 'Python 3 is required.' }
& $python.Source $verifier $campaign
if ($LASTEXITCODE -ne 0) { throw 'Qualification campaign verification failed.' }
