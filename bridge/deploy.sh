#!/usr/bin/env bash
# One-shot deploy of the DrFX GodMode bridge to Cloudflare Workers.
#
#   cd bridge && ./deploy.sh
#
# It creates the KV namespace, writes its id into wrangler.toml, uploads the
# two secrets, and deploys. Safe to re-run: existing resources are reused.
set -euo pipefail
cd "$(dirname "$0")"

command -v wrangler >/dev/null || { echo "Installing wrangler..."; npm install -g wrangler; }

wrangler whoami >/dev/null 2>&1 || wrangler login

if grep -q '<PASTE_KV_ID_HERE>' wrangler.toml; then
  echo "Creating KV namespace 'SIGNALS'..."
  OUT=$(wrangler kv namespace create SIGNALS 2>&1) || { echo "$OUT"; exit 1; }
  echo "$OUT"
  ID=$(echo "$OUT" | grep -oE '[0-9a-f]{32}' | head -1)
  [ -n "$ID" ] || { echo "Could not read the namespace id from the output above."; exit 1; }
  sed -i.bak "s/<PASTE_KV_ID_HERE>/$ID/" wrangler.toml && rm -f wrangler.toml.bak
  echo "KV id $ID written to wrangler.toml"
fi

# Read the keys straight out of SECRETS.local.md so there is nothing to paste.
SEC="../SECRETS.local.md"
getkey() {
  grep -F "$1" "$SEC" | grep -oE '`[A-Za-z0-9]{20,}`' | tr -d '`' | grep -v "^$1$" | head -1
}
if [ -f "$SEC" ]; then
  WK=$(getkey WEBHOOK_KEY); DK=$(getkey DEVICE_KEY)
fi
if [ -n "${WK:-}" ] && [ -n "${DK:-}" ]; then
  echo "Uploading the two secrets from SECRETS.local.md..."
  printf '%s' "$WK" | wrangler secret put WEBHOOK_KEY
  printf '%s' "$DK" | wrangler secret put DEVICE_KEY
else
  echo "Paste the two keys from SECRETS.local.md when prompted."
  echo "  1/2  WEBHOOK_KEY  (TradingView uses this)"
  wrangler secret put WEBHOOK_KEY
  echo "  2/2  DEVICE_KEY   (the SmallTV uses this)"
  wrangler secret put DEVICE_KEY
fi

wrangler deploy
echo
echo "Done. Open the printed workers.dev URL - you should see the DRFX GODMODE BRIDGE page."
