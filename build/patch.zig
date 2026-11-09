const std = @import("std");

// applies unified diffs to a file, all under the build root, each to what the
// ones before it made, and returns the patched text. a hunk's context and
// removed lines must match where it says, shifted only by what the hunks
// before it added or removed, so a submodule bump that moves the code fails
// the build instead of patching the wrong spot. anything before the first
// hunk is free text, saying why the patch exists.
pub fn patched(b: *std.Build, path: []const u8, patch_paths: []const []const u8) []const u8 {
  var src = read(b, path);
  for (patch_paths) |p| src = apply(b, path, src, p);
  return src;
}

fn apply(b: *std.Build, path: []const u8, src: []const u8, patch_path: []const u8) []const u8 {
  const diff = read(b, patch_path);
  var out: std.ArrayList(u8) = .empty;
  const body = if (std.mem.endsWith(u8, src, "\n")) src[0 .. src.len - 1] else src;
  var lines = std.mem.splitScalar(u8, body, '\n');
  var at: usize = 1; // the next source line, counted from 1
  var hunks = std.mem.splitScalar(u8, diff, '\n');
  var n: usize = 0;

  while (hunks.next()) |head| {
    if (!std.mem.startsWith(u8, head, "@@ -")) continue;
    n += 1;

    var old = range(head[4..]) orelse
      std.debug.panic("{s}: hunk {d}: bad header: {s}", .{ patch_path, n, head });
    if (old.len == 0) old.start += 1;
    if (old.start < at)
      std.debug.panic("{s}: hunk {d} starts at line {d}, before the end of the last one", .{ patch_path, n, old.start });

    while (at < old.start) : (at += 1) put(b, &out, lines.next() orelse
      std.debug.panic("{s}: hunk {d}: {s} ends at line {d}", .{ patch_path, n, path, at }));

    var left = old.len;
    while (left > 0 or (hunks.peek() != null and std.mem.startsWith(u8, hunks.peek().?, "+"))) {
      const l = hunks.next() orelse break;
      const kind: u8 = if (l.len == 0) ' ' else l[0];
      const text = if (l.len == 0) l else l[1..];

      switch (kind) {
        '+' => put(b, &out, text),
        ' ', '-' => {
          const got = lines.next() orelse
            std.debug.panic("{s}: hunk {d}: {s} ends at line {d}", .{ patch_path, n, path, at });
          if (!std.mem.eql(u8, got, text))
            std.debug.panic("{s}: hunk {d} does not match {s} at line {d}:\nwant: {s}\nhave: {s}\nrecheck the patch for this version", .{ patch_path, n, path, at, text, got });
          if (kind == ' ') put(b, &out, got);
          at += 1;
          left -= 1;
        },
        else => std.debug.panic("{s}: hunk {d}: unexpected line: {s}", .{ patch_path, n, l }),
      }
    }
  }

  if (n == 0) std.debug.panic("{s}: no hunks", .{patch_path});
  while (lines.next()) |l| put(b, &out, l);
  return out.items;
}

const Range = struct { start: usize, len: usize };

// "a,b" or "a" at the start of s, as in "12,7 +12,8 @@".
fn range(s: []const u8) ?Range {
  const end = std.mem.indexOfScalar(u8, s, ' ') orelse return null;
  const spec = s[0..end];
  const comma = std.mem.indexOfScalar(u8, spec, ',');
  const start = std.fmt.parseInt(usize, spec[0 .. comma orelse spec.len], 10) catch return null;
  const len = if (comma) |c| std.fmt.parseInt(usize, spec[c + 1 ..], 10) catch return null else 1;
  return .{ .start = start, .len = len };
}

fn put(b: *std.Build, out: *std.ArrayList(u8), line: []const u8) void {
  out.appendSlice(b.allocator, line) catch @panic("OOM");
  out.append(b.allocator, '\n') catch @panic("OOM");
}

fn read(b: *std.Build, path: []const u8) []const u8 {
  b.dependOnFileContents(b.path(path));
  const file = b.root.join(b.allocator, path) catch @panic("OOM");
  return file.root_dir.handle.readFileAlloc(b.graph.io, file.sub_path, b.allocator, .unlimited) catch |err|
    std.debug.panic("cannot read {s}: {t}", .{ path, err });
}
