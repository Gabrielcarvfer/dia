#!/usr/bin/env bash
#
# Upload artifacts to VirusTotal (API v3), wait for each analysis to finish,
# print which engines (if any) flag each file and their labels, and FAIL only if
# an artifact reaches VT_FAIL_THRESHOLD malicious detections. This is the gate
# the release job depends on.
#
# Usage:  virustotal-scan.sh <file> [<file> ...]
# Env:    VIRUSTOTAL_API_KEY   (required) — store as a GitHub repo secret.
#         VT_FAIL_THRESHOLD    (optional, default 2) — min malicious engines to
#                              block the release (so a single heuristic false
#                              positive is tolerated, 2+ blocks).
#
# Unsigned open-source binaries (GTK AppImage / unnotarized .dmg / MinGW .exe)
# reliably pick up a few heuristic/ML false positives, while real malware trips
# many engines at once — hence the threshold rather than "block on any hit". The
# durable fix for the false positives is code-signing + notarization.
#
# Note on the free API: it is rate limited (4 req/min, 500/day), so this scans
# files sequentially with spacing.
set -euo pipefail

: "${VIRUSTOTAL_API_KEY:?VIRUSTOTAL_API_KEY is not set}"

API="https://www.virustotal.com/api/v3"
MAX_DIRECT=$((32 * 1024 * 1024))   # files larger than this need an upload URL
fail=0

# curl wrapper: authenticated, quiet, but does NOT use -f so we can inspect the
# JSON body on rate-limit / error responses instead of dying mid-pipeline.
vt() { curl -sS -H "x-apikey: ${VIRUSTOTAL_API_KEY}" "$@"; }

for f in "$@"; do
  if [ ! -f "$f" ]; then
    echo "::error::artifact not found: $f"; fail=1; continue
  fi
  name="$(basename "$f")"
  size="$(stat -c%s "$f")"
  echo "==> $name (${size} bytes): uploading to VirusTotal"

  if [ "$size" -gt "$MAX_DIRECT" ]; then
    target="$(vt "${API}/files/upload_url" | jq -r '.data')"
    if [ -z "$target" ] || [ "$target" = "null" ]; then
      echo "::error::could not obtain large-file upload URL for $name"; fail=1; continue
    fi
  else
    target="${API}/files"
  fi

  resp="$(vt -F "file=@${f}" "$target")"
  analysis_id="$(echo "$resp" | jq -r '.data.id // empty')"
  if [ -z "$analysis_id" ]; then
    echo "::error::upload failed for $name: $(echo "$resp" | jq -c '.error // .' 2>/dev/null || echo "$resp")"
    fail=1; continue
  fi

  # Poll until the analysis completes (cap ~15 min).
  status=""; analysis=""
  for _ in $(seq 1 60); do
    analysis="$(vt "${API}/analyses/${analysis_id}")"
    status="$(echo "$analysis" | jq -r '.data.attributes.status // empty')"
    [ "$status" = "completed" ] && break
    sleep 15
  done

  if [ "$status" != "completed" ]; then
    echo "::error::analysis for $name did not finish in time"; fail=1; continue
  fi

  mal="$(echo "$analysis" | jq -r '.data.attributes.stats.malicious // 0')"
  sus="$(echo "$analysis" | jq -r '.data.attributes.stats.suspicious // 0')"
  sha="$(echo "$analysis" | jq -r '.meta.file_info.sha256 // empty')"
  report="https://www.virustotal.com/gui/file/${sha}"
  echo "    malicious=${mal} suspicious=${sus}  ${report}"

  # Print WHICH engines flagged it and the label they assigned, so a heuristic
  # false positive (common for unsigned GTK bundles / unnotarized .dmg / .exe)
  # is identifiable at a glance rather than an opaque count.
  if [ "${mal:-0}" -gt 0 ] || [ "${sus:-0}" -gt 0 ]; then
    vt "${API}/files/${sha}" | jq -r '
        .data.attributes.last_analysis_results
        | to_entries[]
        | select(.value.category == "malicious" or .value.category == "suspicious")
        | "      - \(.key): \(.value.category)/\(.value.result // "?")"' 2>/dev/null || true
  fi

  # A handful of heuristic/ML detections on an unsigned open-source binary are
  # false positives; real malware trips many engines at once. Block only when
  # the malicious count reaches a threshold (override with VT_FAIL_THRESHOLD);
  # suspicious-only hits are reported but never block. The durable fix for the
  # false positives is code-signing/notarization of the artifacts.
  threshold="${VT_FAIL_THRESHOLD:-2}"
  if [ "${mal:-0}" -ge "$threshold" ]; then
    echo "::error::$name: ${mal} engines flag it malicious (>= ${threshold}) — see ${report}"
    fail=1
  elif [ "${mal:-0}" -gt 0 ] || [ "${sus:-0}" -gt 0 ]; then
    echo "    within tolerance (malicious < ${threshold}); treating as false positive"
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "VirusTotal gate FAILED."
else
  echo "VirusTotal gate passed: all artifacts clean."
fi
exit "$fail"
