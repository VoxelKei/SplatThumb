# Contributing to SplatThumb

Thanks for your interest. SplatThumb runs inside Windows' thumbnail process and reads files that
may be broken or malicious, so changes are held to two rules above everything else:

1. **Safety.** Every size, count and offset read from a file is checked against a limit in
   `core/include/splatthumb/limits.h` before it is used. Code that touches file data must not
   crash, hang or read out of bounds on any input.
2. **Reversibility.** Anything the installer changes on a user's system must be undone by
   uninstalling. No new system changes that cannot be fully reverted.

SplatThumb also never modifies the files it reads, makes no network connections, collects no
telemetry, and does not render non-3DGS PLY files.

## Reporting bugs

Open an issue with the bug report form. Security problems go through private reporting instead;
see [SECURITY.md](SECURITY.md).

**Do not attach real-world scans** to issues or pull requests: they can show people, homes or
other private places. If a file reproduces the problem, please make a small synthetic one, or
share only the header (`splatthumb-cli.exe info <file>` prints it).

## Building and testing

See "Building from source" in the [README](README.md). Before opening a pull request:

```powershell
cmake --workflow --preset msvc-debug     # build and run the tests
```

On Linux with clang, the sanitizer build and the fuzzers run the core library:

```sh
cmake --workflow --preset linux-clang-sanitize
cmake --preset linux-clang-fuzz && cmake --build --preset linux-clang-fuzz
.github/scripts/run-fuzz.sh 60
```

Tests build their PLY input in memory (`tests/ply_builder.h`); do not add PLY files to the
repository.

## Pull requests

- Keep each pull request to one change, and add or update tests for it.
- CI must pass: Windows debug, release (including the MSI install/uninstall round trip) and
  ASan builds, the Linux sanitizer build, and the fuzzers.
- Match the style of the surrounding code; `.clang-format` is the reference.
- Design changes go with a short note in the pull request explaining why.

By contributing, you agree that your contribution is licensed under the [MIT License](LICENSE).
