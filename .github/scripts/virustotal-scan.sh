#!/usr/bin/env bash
#
# Upload artifacts to VirusTotal (API v3), wait for each analysis to finish and
# FAIL if any engine flags a file as malicious or suspicious. This is the gate
# the release job depends on: a single detection on any artifact blocks release.
#
# Usage:  virustotal-scan.sh <file> [<file> ...]
# Env:    VIRUSTOTAL_API_KEY   (required) — store as a GitHub repo secret.
#
# Note on the free API: it is rate limited (4 req/min, 500/day), so this scans
# files sequentially with spacing. Detections include false positives, which is
# why GTK binaries are usually code-signed; with the "block on any detection"
# policy a heuristic hit will stop the release until investigated.
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

  if [ "${mal:-0}" -gt 0 ] || [ "${sus:-0}" -gt 0 ]; then
    echo "::error::$name flagged by VirusTotal (malicious=${mal}, suspicious=${sus}) — see ${report}"
    fail=1
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "VirusTotal gate FAILED."
else
  echo "VirusTotal gate passed: all artifacts clean."
fi
exit "$fail"
