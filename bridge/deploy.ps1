<#
  DrFX GodMode - one-command Cloudflare deploy (Windows PowerShell)

      cd C:\Users\Ultimate\Documents\GitHub\GeekMagic-Ultra
      .\bridge\deploy.ps1

  Does the whole of SETUP.md Part 1:
    - installs wrangler if missing
    - signs you into Cloudflare (opens your browser once)
    - creates the godmode-signals KV namespace and writes its id into wrangler.toml
    - uploads WEBHOOK_KEY and DEVICE_KEY, read straight out of SECRETS.local.md
    - deploys, then checks the Worker actually answers

  Safe to re-run. Existing resources are reused, not duplicated.
#>
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Step($n, $msg) { Write-Host "`n[$n] $msg" -ForegroundColor Cyan }

# --- wrangler -------------------------------------------------------------
Step 1 "Checking for wrangler"
if (-not (Get-Command wrangler -ErrorAction SilentlyContinue)) {
  if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw "Node.js is not installed. Get it from https://nodejs.org, then re-run this script."
  }
  Write-Host "    installing wrangler globally..."
  npm install -g wrangler | Out-Null
  # a global npm install does not update PATH in the session that ran it
  $npmRoot = (npm root -g)
  $env:Path = "$(Split-Path $npmRoot -Parent);$env:Path"
  if (-not (Get-Command wrangler -ErrorAction SilentlyContinue)) {
    throw "wrangler installed but is not on PATH yet. Close this window, open a new PowerShell, and re-run the script."
  }
}
Write-Host "    wrangler $(wrangler --version)"

# --- login ----------------------------------------------------------------
Step 2 "Checking Cloudflare login"
wrangler whoami *> $null
if ($LASTEXITCODE -ne 0) {
  Write-Host "    a browser window will open - approve the request, then come back here"
  wrangler login
}

# --- secrets file ---------------------------------------------------------
Step 3 "Reading your keys from SECRETS.local.md"
$secretsPath = Join-Path (Split-Path $PSScriptRoot -Parent) "SECRETS.local.md"
if (-not (Test-Path $secretsPath)) {
  throw "SECRETS.local.md not found at $secretsPath - it holds the two generated keys."
}
$secretsText = Get-Content $secretsPath -Raw

function Get-Key($name) {
  foreach ($line in ($secretsText -split "`n")) {
    if ($line -match [regex]::Escape($name)) {
      $hits = [regex]::Matches($line, '`([A-Za-z0-9]{20,})`')
      foreach ($h in $hits) {
        if ($h.Groups[1].Value -ne $name) { return $h.Groups[1].Value }
      }
    }
  }
  throw "Could not find a value for $name in SECRETS.local.md"
}

$webhookKey = Get-Key "WEBHOOK_KEY"
$deviceKey  = Get-Key "DEVICE_KEY"
if ($webhookKey -eq $deviceKey) { throw "WEBHOOK_KEY and DEVICE_KEY must differ." }
Write-Host "    found both keys ($($webhookKey.Length) and $($deviceKey.Length) chars)"

# --- KV namespace ---------------------------------------------------------
Step 4 "Setting up the KV namespace"
if ((Get-Content wrangler.toml -Raw) -match '<PASTE_KV_ID_HERE>') {
  $out = (wrangler kv namespace create SIGNALS 2>&1 | Out-String)
  $id  = [regex]::Match($out, '[0-9a-f]{32}').Value
  if (-not $id) { Write-Host $out; throw "Could not read the namespace id from the output above." }
  (Get-Content wrangler.toml -Raw).Replace('<PASTE_KV_ID_HERE>', $id) | Set-Content wrangler.toml -NoNewline
  Write-Host "    created, id $id written into wrangler.toml"
  Write-Host "    (wrangler names it 'drfx-godmode-bridge-SIGNALS' rather than"
  Write-Host "     'godmode-signals' - the binding is what matters, not the title)"
} else {
  Write-Host "    wrangler.toml already has a namespace id - reusing it"
}

# --- secrets --------------------------------------------------------------
Step 5 "Uploading the two secrets"
$webhookKey | wrangler secret put WEBHOOK_KEY
$deviceKey  | wrangler secret put DEVICE_KEY

# --- deploy ---------------------------------------------------------------
Step 6 "Deploying"
$deployOut = (wrangler deploy 2>&1 | Out-String)
Write-Host $deployOut
$url = [regex]::Match($deployOut, 'https://[a-z0-9.\-]+\.workers\.dev').Value

# --- verify ---------------------------------------------------------------
if ($url) {
  Step 7 "Checking it answers"
  try {
    $health = Invoke-RestMethod "$url/health" -TimeoutSec 20
    if ($health -eq "ok") { Write-Host "    /health -> ok" -ForegroundColor Green }

    $body = '{"symbol":"XAUUSD","side":"BUY","score":96,"conf":94,"tp1":"3378","tp2":"3386","sl":"3362"}'
    Invoke-RestMethod "$url/tv?key=$webhookKey" -Method Post -ContentType "application/json" -Body $body -TimeoutSec 20 | Out-Null
    Write-Host "    test signal accepted" -ForegroundColor Green

    $latest = Invoke-RestMethod "$url/latest?key=$deviceKey&device=main" -TimeoutSec 20
    Write-Host "    device poll returned $($latest.symbol) $($latest.side)" -ForegroundColor Green
  } catch {
    Write-Host "    verification failed: $_" -ForegroundColor Yellow
    Write-Host "    open $url in a browser to see what the Worker says."
  }

  Write-Host "`n============================================================" -ForegroundColor Green
  Write-Host " Bridge is live:  $url"
  Write-Host ""
  Write-Host " Device  -> Bridge tab, 'Bridge URL':"
  Write-Host "   $url"
  Write-Host ""
  Write-Host " TradingView -> alert -> Webhook URL:"
  Write-Host "   $url/tv?key=$webhookKey"
  Write-Host "============================================================" -ForegroundColor Green
  Write-Host " Both of these are also written to SECRETS.local.md."
}
