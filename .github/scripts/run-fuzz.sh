#!/usr/bin/env bash
# Runs both fuzzers for the given number of seconds each. Crashing inputs go to fuzz-out/.
# Usage: run-fuzz.sh <seconds per fuzzer>
set -euo pipefail

seconds="${1:?seconds per fuzzer}"
bin=build/linux-clang-fuzz/bin
mkdir -p fuzz-out/header fuzz-out/splats

"$bin/fuzz_header" -dict=fuzz/ply.dict -max_len=70000 -max_total_time="$seconds" \
  -timeout=10 -rss_limit_mb=2048 -artifact_prefix=fuzz-out/ fuzz-out/header fuzz/corpus

"$bin/fuzz_splats" -max_len=70000 -max_total_time="$seconds" \
  -timeout=10 -rss_limit_mb=2048 -artifact_prefix=fuzz-out/ fuzz-out/splats fuzz/corpus_splats
