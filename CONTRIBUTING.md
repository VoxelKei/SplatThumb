# Contributing to SplatThumb

Thanks for your interest.

**Pull requests are not accepted for now.** SplatThumb is a small project maintained by one
person, who is still getting used to running it in public. Bug reports and ideas are very welcome
as [issues](../../issues); please open an issue instead of a pull request. Pull requests may be
opened up later, and this page will say so.

The rest of this page describes how the project is built and tested, for anyone reading or
forking the code.

SplatThumb SplatThumb runs inside Windows' thumbnail process and reads files that
may be broken or malicious, so changes are held to two rules above everything else:

1. **Safety.** Every size, count and offset read from a file is checked against a limit in
   `core/include/splatthumb/limits.h` before it is used. Code that touches file data must not
   crash, hang or read out of bounds on any input.
2. **Reversibility.** Anything the installer changes on a user's system must be undone by
   uninstalling. No new system changes that cannot be fully reverted.

SplatThumb also never modifies the files it reads, makes no network connections, collects no
telemetry, and does not render non-3DGS PLY files.

## Reporting bugs and ideas

Open an issue: the bug report form for problems, or a blank issue for ideas. Security problems go through private reporting instead;
see [SECURITY.md](SECURITY.md).

**Do not attach real-world scans** to issues or pull requests: they can show people, homes or
other private places. If a file reproduces the problem, please make a small synthetic one, or
share only the header (`splatthumb-cli.exe info <file>` prints it).

## Building and testing

See "Building from source" in the [README](README.md). The checks that every change must pass:

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

## Changes to the code

- Keep each change to one thing, and add or update tests for it.
- CI must pass: Windows debug, release (including the MSI install/uninstall round trip) and
  ASan builds, the Linux sanitizer build, and the fuzzers.
- Match the style of the surrounding code; `.clang-format` is the reference.
- Design changes go with a short note explaining why.

The code is licensed under the [MIT License](LICENSE).
