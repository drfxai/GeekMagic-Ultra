# Windows PowerShell equivalent of deploy.sh
# Run from the repo root:  .\bridge\deploy.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command wrangler -ErrorAction SilentlyContinue)) {
  Write-Host "Installing wrangler..."; npm install -g wrangler
}

wrangler whoami *> $null; if ($LASTEXITCODE -ne 0) { wrangler login }

if ((Get-Content wrangler.toml -Raw) -match '<PASTE_KV_ID_HERE>') {
  Write-Host "Creating KV namespace 'SIGNALS'..."
  $out = wrangler kv namespace create SIGNALS 2>&1 | Out-String
  Write-Host $out
  $id = [regex]::Match($out, '[0-9a-f]{32}').Value
  if (-not $id) { throw "Could not read the namespace id from the output above." }
  (Get-Content wrangler.toml -Raw).Replace('<PASTE_KV_ID_HERE>', $id) | Set-Content wrangler.toml -NoNewline
  Write-Host "KV id $id written to wrangler.toml"
}

Write-Host "`nPaste the two keys from SECRETS.local.md when prompted."
Write-Host "  1/2  WEBHOOK_KEY  (TradingView uses this)"
wrangler secret put WEBHOOK_KEY
Write-Host "  2/2  DEVICE_KEY   (the SmallTV uses this)"
wrangler secret put DEVICE_KEY

wrangler deploy
Write-Host "`nDone. Open the printed workers.dev URL."
