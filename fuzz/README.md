# fuzz

libFuzzer harness for the PLY header parser and classifier.

Requires clang on Linux:

```sh
cmake --preset linux-clang-fuzz
cmake --build --preset linux-clang-fuzz
mkdir -p /tmp/corpus
./build/linux-clang-fuzz/bin/fuzz_header -dict=fuzz/ply.dict -max_total_time=60 /tmp/corpus fuzz/corpus
```

`fuzz/corpus/` holds small synthetic seed inputs. CI runs the fuzzer for 60 seconds on every
push and pull request.
