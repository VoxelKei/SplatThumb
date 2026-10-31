#!/usr/bin/env bash
# Runs the fuzzers for the given number of seconds each (the small view parser gets at most
# 30 seconds). Crashing inputs go to fuzz-out/.
# Usage: run-fuzz.sh <seconds per fuzzer>
set -euo pipefail

seconds="${1:?seconds per fuzzer}"
bin=build/linux-clang-fuzz/bin
mkdir -p fuzz-out/header fuzz-out/splats fuzz-out/view

"$bin/fuzz_header" -dict=fuzz/ply.dict -max_len=70000 -max_total_time="$seconds" \
  -timeout=10 -rss_limit_mb=2048 -artifact_prefix=fuzz-out/ fuzz-out/header fuzz/corpus

"$bin/fuzz_splats" -max_len=70000 -max_total_time="$seconds" \
  -timeout=10 -rss_limit_mb=2048 -artifact_prefix=fuzz-out/ fuzz-out/splats fuzz/corpus_splats

view_seconds=$(( seconds < 30 ? seconds : 30 ))
"$bin/fuzz_view" -dict=fuzz/view.dict -max_len=2048 -max_total_time="$view_seconds" \
  -timeout=10 -rss_limit_mb=2048 -artifact_prefix=fuzz-out/ fuzz-out/view fuzz/corpus_view
