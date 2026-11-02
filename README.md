# livejq
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

A terminal UI to jq a JSON file and see the result live.

## Build

Needs only [Zig](https://ziglang.org) 0.17.0. jq, oniguruma and PDCursesMod are git submodules
under `vendor/`, fetched on the first build if missing, and compiled from source.

```text
zig build
./zig-out/bin/livejq <file>
```

On Linux the binary is fully static (musl). Cross builds:

```text
zig build -Dtarget=x86_64-linux-musl
zig build -Dtarget=aarch64-linux-musl
zig build -Dtarget=x86_64-macos
zig build -Dtarget=aarch64-macos
zig build -Dtarget=x86_64-windows-gnu
```

## Test

```text
zig build test
```

## License

livejq is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.
