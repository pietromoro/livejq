# livejq
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

A terminal UI to jq a JSON file and see the result live.

## Usage

```text
livejq data.json
livejq --query '.[] | select(.level == "error")' --watch app.json
livejq --raw-input --slurp notes.txt
livejq --query-folder queries/ data.json
livejq --latest '*.save' store-archive/
```

Type a jq filter in the box at the top: the outputs follow as you type, as a tree you can fold,
search and copy from. Tab moves between the query, the tree and the saved queries; F1, or `?`
outside the query, lists every key of the focused zone.

### Options

```text
-q, --query TEXT         start with this query instead of .
-w, --watch [SECS]       reload the file when it changes, checked every
                         SECS seconds (default 1, fractions allowed)
-l, --latest PATTERN     take a folder instead of a file and follow its
                         newest file named like PATTERN (* and ?
                         wildcards), watching it as --watch does
-d, --query-folder PATH  list the .query files in PATH as saved queries
-s, --slurp              read all inputs into one array
-R, --raw-input          read each line of the file as a string
    --arg NAME VALUE     bind $NAME to the string VALUE
    --argjson NAME JSON  bind $NAME to the JSON value
```

With `--latest`, the status bar names the file being shown. A newer file that does not parse
yet, because its writer is not done, keeps the last data on screen until it does; one that is
still broken and unchanged at the next look is shown with its error.

### Keys

In the tree:

| Keys              | Action                                                         |
| ----------------- | -------------------------------------------------------------- |
| `j` `k` `↓` `↑`   | move                                                           |
| `h` `l` `←` `→`   | fold, unfold                                                   |
| `space` `enter`   | toggle a fold                                                  |
| `H` `L`           | fold, unfold the whole subtree                                 |
| `g` `G`           | top, bottom                                                    |
| `/` `n` `N`       | search keys and values, next, previous match                   |
| `v` `V`           | next, previous view: hex, sizes, times, base64, JWT, query, …  |
| `0`               | the value as jq prints it                                      |
| `y` `Y` `r`       | copy the value as JSON, its path, its raw text                 |
| `p`               | pipe the query into the path under the cursor                  |
| `q`               | quit                                                           |

In the query, the usual line editing keys work (`ctrl-a` `ctrl-e` `ctrl-w` `ctrl-u` `ctrl-k`,
word moves with `ctrl` or `alt`), and `↓` or `ctrl-space` completes the key or builtin at the
cursor. With `--query-folder`, `ctrl-s` saves the query, `alt-s` saves it under a new name,
`ctrl-n` starts a new one and `ctrl-b` hides or shows the list; `enter` in the list loads a
query and `d` deletes it.

Copying goes through the terminal's clipboard sequence (OSC 52), which the terminal has to allow.

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
