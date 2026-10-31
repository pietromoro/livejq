const std = @import("std");

pub const jq_version = "1.8.2";

const vendor_flags: []const []const u8 = &.{ "-std=gnu99", "-w" };

fn vendorModule(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Module {
  return b.createModule(.{
    .target = target,
    .optimize = optimize,
    .link_libc = true,
    .sanitize_c = .off,
  });
}

const onig_dir = "vendor/jq/vendor/oniguruma/src";

const onig_sources: []const []const u8 = &.{
  "regparse.c",          "regcomp.c",          "regexec.c",          "regenc.c",
  "regerror.c",          "regext.c",           "regsyntax.c",        "regtrav.c",
  "regversion.c",        "st.c",               "reggnu.c",           "unicode.c",
  "unicode_unfold_key.c", "unicode_fold1_key.c", "unicode_fold2_key.c", "unicode_fold3_key.c",
  "ascii.c",             "utf8.c",             "utf16_be.c",         "utf16_le.c",
  "utf32_be.c",          "utf32_le.c",         "euc_jp.c",           "euc_jp_prop.c",
  "sjis.c",              "sjis_prop.c",        "iso8859_1.c",        "iso8859_2.c",
  "iso8859_3.c",         "iso8859_4.c",        "iso8859_5.c",        "iso8859_6.c",
  "iso8859_7.c",         "iso8859_8.c",        "iso8859_9.c",        "iso8859_10.c",
  "iso8859_11.c",        "iso8859_13.c",       "iso8859_14.c",       "iso8859_15.c",
  "iso8859_16.c",        "euc_tw.c",           "euc_kr.c",           "big5.c",
  "gb18030.c",           "koi8_r.c",           "cp1251.c",           "onig_init.c",
};

fn onigConfig(b: *std.Build, target: std.Build.ResolvedTarget) *std.Build.Step.ConfigHeader {
  const t = target.result;
  const windows = t.os.tag == .windows;
  return b.addConfigHeader(.{ .style = .blank, .include_path = "config.h" }, .{
    .HAVE_ALLOCA = 1,
    .HAVE_ALLOCA_H = if (windows) null else @as(?i32, 1),
    .HAVE_STDINT_H = 1,
    .HAVE_INTTYPES_H = 1,
    .HAVE_SYS_TYPES_H = 1,
    .HAVE_SYS_TIME_H = if (windows) null else @as(?i32, 1),
    .HAVE_SYS_TIMES_H = if (windows) null else @as(?i32, 1),
    .HAVE_UNISTD_H = if (windows) null else @as(?i32, 1),
    .SIZEOF_INT = t.cTypeByteSize(.int),
    .SIZEOF_LONG = t.cTypeByteSize(.long),
    .SIZEOF_LONG_LONG = t.cTypeByteSize(.longlong),
    .SIZEOF_VOIDP = t.ptrBitWidth() / 8,
  });
}

pub fn oniguruma(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
  const mod = vendorModule(b, target, optimize);
  mod.addConfigHeader(onigConfig(b, target));
  mod.addIncludePath(b.path(onig_dir));
  mod.addCMacro("ONIG_STATIC", "1");
  mod.addCSourceFiles(.{ .root = b.path(onig_dir), .files = onig_sources, .flags = vendor_flags });

  const lib = b.addLibrary(.{ .name = "onig", .root_module = mod });
  lib.installHeader(b.path(onig_dir ++ "/oniguruma.h"), "oniguruma.h");
  return lib;
}

const jq_dir = "vendor/jq";

const jq_sources: []const []const u8 = &.{
  "src/builtin.c",   "src/bytecode.c",  "src/compile.c",
  "src/jv.c",       "src/jv_alloc.c",  "src/jv_aux.c",      "src/jv_dtoa.c",
  "src/jv_file.c",   "src/jv_parse.c",  "src/jv_print.c",    "src/jv_unicode.c",
  "src/linker.c",    "src/locfile.c",   "src/util.c",        "src/jv_dtoa_tsd.c",
  "src/lexer.c",     "src/parser.c",    "vendor/decNumber/decContext.c",
  "vendor/decNumber/decNumber.c",
};

const jq_math_common: []const []const u8 = &.{
  "ACOS",  "ACOSH",     "ASIN",      "ASINH",     "ATAN",   "ATAN2",     "ATANH",
  "CBRT",  "CEIL",      "COPYSIGN",  "COS",       "COSH",   "ERF",       "ERFC",
  "EXP",   "EXP2",      "EXPM1",     "FABS",      "FDIM",   "FLOOR",     "FMA",
  "FMAX",  "FMIN",      "FMOD",      "FREXP",     "HYPOT",  "J0",        "J1",
  "JN",    "LDEXP",     "LGAMMA",    "LOG",       "LOG10",  "LOG1P",     "LOG2",
  "LOGB",  "MODF",      "NEARBYINT", "NEXTAFTER", "NEXTTOWARD", "POW",   "REMAINDER",
  "RINT",  "ROUND",     "SCALBLN",   "SCALBN",    "ILOGB",  "SIN",       "SINH",
  "SQRT",  "TAN",       "TANH",      "TGAMMA",    "TRUNC",  "Y0",        "Y1",
  "YN",
};

fn jqDefines(mod: *std.Build.Module, target: std.Build.ResolvedTarget) void {
  for (jq_math_common) |f| mod.addCMacro(mod.owner.fmt("HAVE_{s}", .{f}), "1");

  mod.addCMacro("USE_DECNUM", "1");
  mod.addCMacro("HAVE_LIBONIG", "1");
  mod.addCMacro("ONIG_STATIC", "1");
  mod.addCMacro("HAVE_SETLOCALE", "1");
  mod.addCMacro("HAVE_STRFTIME", "1");
  mod.addCMacro(switch (target.result.cpu.arch.endian()) {
    .little => "IEEE_8087",
    .big => "IEEE_MC68k",
  }, "1");

  const posix: []const []const u8 = &.{
    "HAVE_PTHREAD",   "HAVE_PTHREAD_KEY_CREATE", "HAVE_PTHREAD_ONCE", "HAVE_ATEXIT",
    "HAVE_ISATTY",    "HAVE_STRPTIME",           "HAVE_SETENV",       "HAVE_TIMEGM",
    "HAVE_GMTIME_R",  "HAVE_GMTIME",             "HAVE_LOCALTIME_R",  "HAVE_LOCALTIME",
    "HAVE_GETTIMEOFDAY", "HAVE_TM_TM_GMT_OFF",  "HAVE_MEMMEM",       "HAVE_ALLOCA_H",
    "HAVE_SCALB",     "HAVE_LGAMMA_R",           "HAVE_SIGNIFICAND",  "HAVE_DREM",
  };

  switch (target.result.os.tag) {
    .linux => {
      mod.addCMacro("_GNU_SOURCE", "1");
      for (posix) |m| mod.addCMacro(m, "1");
      mod.addCMacro("HAVE_EXP10", "1");
      mod.addCMacro("HAVE_GETENTROPY", "1");
    },
    .macos => {
      mod.addCMacro("_REENTRANT", "1");
      for (posix) |m| mod.addCMacro(m, "1");
      mod.addCMacro("HAVE___EXP10", "1");
      mod.addCMacro("HAVE_ARC4RANDOM", "1");
      mod.addCMacro("HAVE_GETENTROPY", "1");
    },
    .windows => {
      mod.addCMacro("WIN32", "1");
      mod.addCMacro("HAVE__ISATTY", "1");
      mod.addCMacro("HAVE_GMTIME", "1");
      mod.addCMacro("HAVE_LOCALTIME", "1");
      mod.addCMacro("HAVE_GETTIMEOFDAY", "1");
      mod.addCMacro("HAVE_PTHREAD", "1");
      mod.addCMacro("HAVE_PTHREAD_KEY_CREATE", "1");
      mod.addCMacro("HAVE_PTHREAD_ONCE", "1");
      mod.addCMacro("HAVE_ATEXIT", "1");
    },
    else => @panic("unsupported target OS"),
  }
}

const Splice = struct { anchor: []const u8, before: []const u8 = "", after: []const u8 = "" };

// jq_next cannot be interrupted upstream: a flag polled once per opcode halts
// the running program, so a superseded query stops instead of leaking a thread.
const cancel_splices: []const Splice = &.{
  .{ .anchor = "  int halted;\n", .after = "  int *cancel_flag;\n" },
  .{ .anchor = "  jq->halted = 0;\n  jq->exit_code = jv_invalid();\n", .after = "  jq->cancel_flag = NULL;\n" },
  .{
    .anchor = "    uint16_t opcode = *pc;\n    raising = 0;\n",
    .before =
    \\    if (jq->cancel_flag && __atomic_load_n(jq->cancel_flag, __ATOMIC_RELAXED)) {
    \\      jq_halt(jq, jv_invalid(), jv_string("cancelled"));
    \\      return jv_invalid();
    \\    }
    \\
    ,
  },
};

const cancel_setter =
  \\
  \\void jq_set_cancel_flag(jq_state *jq, int *flag) {
  \\  jq->cancel_flag = flag;
  \\}
  \\
;

fn cancellableExecute(b: *std.Build) []const u8 {
  const src = spliced(b, jq_dir ++ "/src/execute.c", cancel_splices);
  return std.mem.concat(b.allocator, u8, &.{ src, cancel_setter }) catch @panic("OOM");
}

// the build fails on a missing or repeated anchor, so a submodule bump that
// moves the code cannot drop a patch silently.
fn spliced(b: *std.Build, path: []const u8, splices: []const Splice) []const u8 {
  var src: []const u8 = b.build_root.handle.readFileAlloc(b.graph.io, path, b.allocator, .unlimited) catch |err|
    std.debug.panic("cannot read {s}: {t}", .{ path, err });

  for (splices) |s| {
    const at = std.mem.indexOf(u8, src, s.anchor) orelse
      std.debug.panic("{s}: patch anchor not found, recheck the patch for this version:\n{s}", .{ path, s.anchor });
    if (std.mem.indexOfPos(u8, src, at + 1, s.anchor) != null)
      std.debug.panic("{s}: patch anchor is not unique:\n{s}", .{ path, s.anchor });
    src = std.mem.concat(b.allocator, u8, &.{
      src[0..at], s.before, s.anchor, s.after, src[at + s.anchor.len ..],
    }) catch @panic("OOM");
  }
  return src;
}

pub fn jq(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, onig: *std.Build.Step.Compile) *std.Build.Step.Compile {
  const embed = b.addExecutable(.{
    .name = "embed",
    .root_module = b.createModule(.{
      .target = b.graph.host,
      .optimize = .ReleaseFast,
      .link_libc = true,
    }),
  });
  embed.root_module.addIncludePath(b.path("include"));
  embed.root_module.addCSourceFile(.{ .file = b.path("tools/embed.c"), .flags = &.{"-std=c99"} });
  const run_embed = b.addRunArtifact(embed);
  run_embed.addFileArg(b.path(jq_dir ++ "/src/builtin.jq"));
  const builtin_inc = run_embed.addOutputFileArg("builtin.inc");

  const generated = b.addWriteFiles();
  const execute_c = generated.add("src/execute.c", cancellableExecute(b));
  _ = generated.addCopyFile(builtin_inc, "src/builtin.inc");
  _ = generated.add("src/version.h", "#define JQ_VERSION \"" ++ jq_version ++ "\"\n");
  _ = generated.add("src/config_opts.inc", "#define JQ_CONFIG \"livejq\"\n");

  const mod = vendorModule(b, target, optimize);
  mod.addIncludePath(generated.getDirectory());
  mod.addIncludePath(b.path(jq_dir));
  mod.addIncludePath(b.path(jq_dir ++ "/src"));
  mod.addIncludePath(b.path(jq_dir ++ "/vendor"));
  mod.linkLibrary(onig);
  jqDefines(mod, target);
  mod.addCSourceFiles(.{ .root = b.path(jq_dir), .files = jq_sources, .flags = vendor_flags });
  mod.addCSourceFile(.{ .file = execute_c, .flags = vendor_flags });
  if (target.result.os.tag == .windows) mod.linkSystemLibrary("shlwapi", .{});

  const lib = b.addLibrary(.{ .name = "jq", .root_module = mod });
  lib.installHeader(b.path(jq_dir ++ "/src/jq.h"), "jq.h");
  lib.installHeader(b.path(jq_dir ++ "/src/jv.h"), "jv.h");
  return lib;
}

const pdc_dir = "vendor/PDCursesMod";

const pdc_core: []const []const u8 = &.{
  "addch.c",   "addchstr.c", "addstr.c",  "attr.c",     "beep.c",    "bkgd.c",
  "border.c",  "clear.c",    "color.c",   "debug.c",    "delch.c",   "deleteln.c",
  "getch.c",   "getstr.c",   "getyx.c",   "inch.c",     "inchstr.c", "initscr.c",
  "inopts.c",  "insch.c",    "insstr.c",  "instr.c",    "kernel.c",  "keyname.c",
  "mouse.c",   "move.c",     "outopts.c", "overlay.c",  "pad.c",     "panel.c",
  "printw.c",  "refresh.c",  "scanw.c",   "scr_dump.c", "scroll.c",  "slk.c",
  "termattr.c", "terminfo.c", "touch.c",  "util.c",     "window.c",
};

const pdc_port: []const []const u8 = &.{
  "pdcclip.c", "pdcdisp.c", "pdcgetsc.c", "pdckbd.c", "pdcscrn.c", "pdcsetsc.c", "pdcutil.c",
};

// the vt port sends every color as 38;5;N or 38;2;R;G;B, so the terminal's
// theme never applies. the 16 basic colors go out as their ansi codes instead,
// as jq prints them. it never sends dim either, only darkens an explicit color:
// dim goes out as 2m, and turning it off resets the attributes like bold does.
const ansi_splices: []const Splice = &.{
  .{
    .anchor = "static void reset_color( char *obuff, const chtype ch)\n",
    .before =
    \\PACKED_RGB PDC_default_color( int idx);
    \\
    \\static int ansi_color( const PACKED_RGB rgb)
    \\{
    \\    int i;
    \\
    \\    for( i = 0; i < 16; i++)
    \\        if( rgb == PDC_default_color( i))
    \\            return( (i & 8) | ((i & COLOR_RED) ? 1 : 0)
    \\                    | ((i & COLOR_GREEN) ? 2 : 0) | ((i & COLOR_BLUE) ? 4 : 0));
    \\    return( -1);
    \\}
    \\
    \\static void ansi_string( char *otext, const int idx, const int base)
    \\{
    \\    sprintf( otext, CSI "%dm", (idx & 8 ? base + 60 : base) + (idx & 7));
    \\}
    \\
    \\
    ,
  },
  .{
    .anchor = "            strcpy( obuff, CSI \"49m\");\n",
    .after =
    \\        else if( ansi_color( bg) >= 0)
    \\            ansi_string( obuff, ansi_color( bg), 40);
    \\
    ,
  },
  .{
    .anchor = "            strcpy( obuff, CSI \"39m\");\n",
    .after =
    \\        else if( ansi_color( fg) >= 0)
    \\            ansi_string( obuff, ansi_color( fg), 30);
    \\
    ,
  },
  .{
    .anchor = "       if( changes & (A_REVERSE | A_STRIKEOUT | A_BOLD))\n",
    .before =
    \\       if( changes & A_DIM)
    \\          changes |= A_BOLD;
    \\
    ,
  },
  .{
    .anchor = "          strcat( obuff, BOLD_ON);\n",
    .after =
    \\       if( *srcp & A_DIM)
    \\          strcat( obuff, DIM_ON);
    \\
    ,
  },
};

pub const pdc_macros: []const [2][]const u8 = &.{
  .{ "PDC_WIDE", "1" },
  .{ "PDC_FORCE_UTF8", "1" },
};

pub fn pdcurses(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
  const mod = vendorModule(b, target, optimize);
  mod.addIncludePath(b.path(pdc_dir));
  for (pdc_macros) |m| mod.addCMacro(m[0], m[1]);
  if (target.result.os.tag == .macos) mod.addCMacro("_DEFAULT_SOURCE", "1");
  if (target.result.os.tag == .linux) mod.addCMacro("_GNU_SOURCE", "1");
  mod.addCSourceFiles(.{ .root = b.path(pdc_dir ++ "/pdcurses"), .files = pdc_core, .flags = vendor_flags });
  if (target.result.os.tag == .windows) {
    mod.addCSourceFiles(.{ .root = b.path(pdc_dir ++ "/wincon"), .files = pdc_port, .flags = vendor_flags });
  } else {
    const generated = b.addWriteFiles();
    const disp_c = generated.add("pdcdisp.c", spliced(b, pdc_dir ++ "/vt/pdcdisp.c", ansi_splices));
    mod.addIncludePath(b.path(pdc_dir ++ "/vt"));
    mod.addCSourceFiles(.{ .root = b.path(pdc_dir ++ "/vt"), .files = pdc_port[0..1] ++ pdc_port[2..], .flags = vendor_flags });
    mod.addCSourceFile(.{ .file = disp_c, .flags = vendor_flags });
  }
  if (target.result.os.tag == .windows) {
    mod.linkSystemLibrary("user32", .{});
    mod.linkSystemLibrary("winmm", .{});
  }

  const lib = b.addLibrary(.{ .name = "pdcurses", .root_module = mod });
  lib.installHeader(b.path(pdc_dir ++ "/curses.h"), "curses.h");
  lib.installHeader(b.path(pdc_dir ++ "/panel.h"), "panel.h");
  return lib;
}
