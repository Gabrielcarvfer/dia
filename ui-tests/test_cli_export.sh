#!/usr/bin/env bash
#
# Headless CLI test: `dia --export` renders a sample .dia to PNG and PDF and
# produces valid, non-empty files. Needs no display. Run via meson test.
#
# Args: $1 = path to a sample .dia
# Env:  DIA_BIN = the built dia binary
set -eu

DIA="${DIA_BIN:?DIA_BIN must point at the dia binary}"
SAMPLE="${1:?usage: test_cli_export.sh SAMPLE.dia}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# PNG
"$DIA" --export "$TMP/out.png" "$SAMPLE"
test -s "$TMP/out.png" || { echo "FAIL: no PNG produced"; exit 1; }
# PNG signature: bytes 2-4 are 'PNG'
head -c 8 "$TMP/out.png" | grep -q 'PNG' || { echo "FAIL: not a PNG"; exit 1; }

# PDF
"$DIA" -e "$TMP/out.pdf" "$SAMPLE"
test -s "$TMP/out.pdf" || { echo "FAIL: no PDF produced"; exit 1; }
head -c 5 "$TMP/out.pdf" | grep -q '%PDF' || { echo "FAIL: not a PDF"; exit 1; }

echo "CLI export OK (PNG + PDF)"
