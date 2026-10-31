# fuzz

libFuzzer harnesses. Both need clang on Linux.

| Harness | Input | What it covers |
| --- | --- | --- |
| `fuzz_header` | Arbitrary bytes | Header parser and classifier, then loading and rendering any file they accept |
| `fuzz_splats` | Two layout bytes, then vertex data behind a fixed valid 3DGS header | `render_thumbnail`, the same path Explorer uses, with arbitrary record values (NaN, infinities, huge scales, zero rotations) |

```sh
cmake --preset linux-clang-fuzz
cmake --build --preset linux-clang-fuzz
.github/scripts/run-fuzz.sh 60   # 60 seconds per harness
```

`fuzz/corpus/` and `fuzz/corpus_splats/` hold small synthetic seed inputs; no real scans.
CI runs each harness for 60 seconds on every push and pull request, and for 20 minutes once a
week (`.github/workflows/fuzz-weekly.yml`). Crashing inputs are uploaded as the `fuzz-crash`
artifact.
