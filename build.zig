const std = @import("std");
const builtin = @import("builtin");
const vendor = @import("build/vendor.zig");

const c_flags: []const []const u8 = &.{ "-std=c99", "-Wall", "-Wextra", "-Wpedantic" };

pub fn build(b: *std.Build) void {
  ensureSubmodules(b);

  const target = b.standardTargetOptions(.{
    .default_target = .{ .abi = if (builtin.os.tag == .linux) .musl else null },
  });
  const optimize = b.standardOptimizeOption(.{});

  const onig = vendor.oniguruma(b, target, optimize);
  const jq = vendor.jq(b, target, optimize, onig);
  const pdcurses = vendor.pdcurses(b, target, optimize);

  const core_mod = b.createModule(.{
    .target = target,
    .optimize = optimize,
    .link_libc = true,
  });
  core_mod.addIncludePath(b.path("include"));
  core_mod.addCSourceFiles(.{ .root = b.path("src/core"), .files = cSources(b, "src/core"), .flags = c_flags });
  core_mod.addCMacro("_POSIX_C_SOURCE", "200809L");
  if (target.result.os.tag == .macos) core_mod.addCMacro("_DARWIN_C_SOURCE", "1");
  core_mod.addCMacro("LIVEJQ_VERSION", "\"" ++ @import("build.zig.zon").version ++ "\"");
  core_mod.addCMacro("JQ_VERSION", "\"" ++ vendor.jq_version ++ "\"");
  core_mod.addCMacro("ONIG_VERSION", "\"" ++ vendor.onig_version ++ "\"");
  core_mod.addCMacro("PDC_VERSION", "\"" ++ vendor.pdc_version ++ "\"");
  core_mod.linkLibrary(jq);
  const core = b.addLibrary(.{ .name = "livejq_core", .root_module = core_mod });

  const exe_mod = b.createModule(.{
    .target = target,
    .optimize = optimize,
    .link_libc = true,
  });
  exe_mod.addIncludePath(b.path("include"));
  exe_mod.addCSourceFiles(.{ .root = b.path("src"), .files = cSources(b, "src"), .flags = c_flags });
  exe_mod.addIncludePath(b.path("src/tui"));
  exe_mod.addCSourceFiles(.{ .root = b.path("src/tui"), .files = cSources(b, "src/tui"), .flags = c_flags });
  for (vendor.pdc_macros) |m| exe_mod.addCMacro(m[0], m[1]);
  exe_mod.addCMacro("PDC_NCMOUSE", "1");
  exe_mod.linkLibrary(core);
  exe_mod.linkLibrary(jq);
  exe_mod.linkLibrary(onig);
  exe_mod.linkLibrary(pdcurses);

  const exe = b.addExecutable(.{ .name = "livejq", .root_module = exe_mod });
  if (target.result.os.tag == .linux) exe.linkage = .static;
  b.installArtifact(exe);

  const run = b.addRunArtifact(exe);
  run.addPassthruArgs();
  b.step("run", "Build and run livejq").dependOn(&run.step);

  const test_step = b.step("test", "Build and run the tests");
  for (cSources(b, "tests")) |file| {
    if (!std.mem.startsWith(u8, file, "test_")) continue;

    const mod = b.createModule(.{
      .target = target,
      .optimize = optimize,
      .link_libc = true,
    });
    mod.addIncludePath(b.path("include"));
    mod.addIncludePath(b.path("tests"));
    mod.addCSourceFile(.{ .file = b.path(b.fmt("tests/{s}", .{file})), .flags = c_flags });
    mod.linkLibrary(core);
    mod.linkLibrary(jq);
    mod.linkLibrary(onig);

    const t = b.addExecutable(.{ .name = file[0 .. file.len - 2], .root_module = mod });
    const run_t = b.addRunArtifact(t);
    run_t.setCwd(b.path("."));
    test_step.dependOn(&run_t.step);
  }
}

fn cSources(b: *std.Build, dir: []const u8) []const []const u8 {
  const io = b.graph.io;
  b.dependOnDirectoryContents(b.path(dir));
  var d = b.root.openDir(io, dir, .{ .iterate = true }) catch |err|
    std.debug.panic("cannot open {s}: {t}", .{ dir, err });
  defer d.close(io);

  var files: std.ArrayList([]const u8) = .empty;
  var it = d.iterate();
  while (it.next(io) catch |err| std.debug.panic("cannot list {s}: {t}", .{ dir, err })) |entry| {
    if (entry.kind != .file or !std.mem.endsWith(u8, entry.name, ".c")) continue;
    files.append(b.allocator, b.dupe(entry.name)) catch @panic("OOM");
  }
  std.mem.sort([]const u8, files.items, {}, struct {
    fn lt(_: void, a: []const u8, c: []const u8) bool {
      return std.mem.lessThan(u8, a, c);
    }
  }.lt);
  return files.items;
}

fn ensureSubmodules(b: *std.Build) void {
  const io = b.graph.io;
  const markers: []const []const u8 = &.{
    "vendor/jq/src/jv.c",
    "vendor/jq/vendor/oniguruma/src/oniguruma.h",
    "vendor/PDCursesMod/curses.h",
  };
  for (markers) |m| {
    b.dependOnFileMetadata(b.path(m));
    _ = b.root.statFile(io, m) catch {
      std.log.info("fetching vendored submodules", .{});
      b.graph.poisonCache();
      _ = b.run(&.{ "git", "-C", b.root.joinString(b.allocator, ".") catch @panic("OOM"), "submodule", "update", "--init", "--recursive" });
      return;
    };
  }
}
