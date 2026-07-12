# The tr_sys_* portability layer and the Win32 modules (detail)

Companion to `../SKILL.md`. Verified against `libtransmission/file.h`, `file-win32.cc`,
`file-posix.cc`, `string-utils.cc`/`.h`, `subprocess-win32.cc`, `watchdir-win32.cc`,
`platform.cc`, and `tr-strbuf.h`. Re-verify function spellings before relying on them.

## The rule: one declaration, two implementations

Portable code includes **`libtransmission/file.h`** and calls `tr_sys_*`. Each function is
implemented **twice** and both must stay in lockstep:

- `libtransmission/file-posix.cc` — POSIX (`int` fds, `open`/`read`/`stat`/`opendir`)
- `libtransmission/file-win32.cc` — Win32 (`HANDLE`s, `CreateFileW`/`ReadFile`/`FindFirstFileW`)

Canonical example of the split — `tr_sys_path_native_separators(char* path)`:

```cpp
// file-posix.cc: separators are already native
char* tr_sys_path_native_separators(char* path) { return path; }

// file-win32.cc: rewrite '/' -> '\'
char* tr_sys_path_native_separators(char* path) {
    for (char* s = strchr(path, '/'); s; s = strchr(s, '/')) { *s = '\\'; }
    return path;
}
```

When you extend the API, edit **both** files (and add a case to
`tests/libtransmission/file-test.cc`). Never `#ifdef _WIN32` a raw Win32 call into otherwise-portable
code — that is what these files are for.

**Build wiring:** these platform pairs are **not** gated by a plain `if(WIN32)` in
`libtransmission/CMakeLists.txt`; they go through the project's
`tr_allow_compile_if([=[[WIN32]]=] file-win32.cc … [=[[NOT WIN32]]=] file-posix.cc …)` macro. A
genuinely new `*-win32.cc`/`*-posix.cc` sibling belongs in that block, not a bespoke
`if(WIN32) target_sources()` call.

## Platform types (`file.h`)

```cpp
#ifndef _WIN32
using tr_sys_file_t = int;               // #define TR_BAD_SYS_FILE (-1)
using tr_sys_dir_t  = void*;
#else
using tr_sys_file_t = HANDLE;            // #define TR_BAD_SYS_FILE INVALID_HANDLE_VALUE
using tr_sys_dir_t  = tr_sys_dir_win32*; // opaque struct
#endif
#define TR_BAD_SYS_DIR ((tr_sys_dir_t) nullptr)
```

**Gotcha:** an invalid handle is `INVALID_HANDLE_VALUE` on Windows but `-1` on POSIX — both are
`TR_BAD_SYS_FILE`, so test with `handle != TR_BAD_SYS_FILE`, never `handle >= 0` (a `HANDLE` is a
pointer). `file.h` includes `<windows.h>` under `_WIN32` so `HANDLE` is defined.

## The tr_sys_* API surface

Spellings verified in `file.h`. Files:

- `tr_sys_file_open(std::string_view path, int flags, int permissions, tr_error* = nullptr)`
  — `flags` from `tr_sys_file_open_flags_t` (`TR_SYS_FILE_READ`, `TR_SYS_FILE_WRITE`,
  `TR_SYS_FILE_CREATE`, `TR_SYS_FILE_TRUNCATE`, …)
- `tr_sys_file_open_temp(char* path_template, …)`
- `tr_sys_file_close`, `tr_sys_file_read`, `tr_sys_file_read_at`, `tr_sys_file_write`,
  `tr_sys_file_write_at`, `tr_sys_file_truncate`, `tr_sys_file_preallocate`

Paths (all take/return UTF-8 `std::string_view`/`std::string`):

- `tr_sys_path_get_info(path, flags, …) -> std::optional<tr_sys_path_info>`
  (`flags`: `TR_SYS_PATH_NO_FOLLOW`)
- `tr_sys_path_is_relative`, `tr_sys_path_copy`, `tr_sys_path_rename`, `tr_sys_path_remove`,
  `tr_sys_path_native_separators`

Directories:

- `tr_sys_dir_open`, `tr_sys_dir_read_name` (returns `char const*`, UTF-8), `tr_sys_dir_close`

Errors flow through `tr_error*` out-params, not `errno`/`GetLastError` at the call site — see
**cpp-conventions** for `tr_error`.

## The UTF-8 ⇄ UTF-16 boundary

The whole codebase is **UTF-8** (`std::string`/`std::string_view`). Win32 `…W` APIs are **UTF-16**
(`std::wstring`). Convert only at the boundary, with the two helpers declared in `string-utils.h`
and defined in `string-utils.cc`:

```cpp
std::wstring tr_win32_utf8_to_native(std::string_view);  // MultiByteToWideChar(CP_UTF8, …)
std::string  tr_win32_native_to_utf8(std::wstring_view); // WideCharToMultiByte(CP_UTF8, …)
```

Both use the standard **two-call idiom**: call once with a null output buffer to measure the
required length, `resize`, then call again to fill. `CP_UTF8` is the codepage on both sides —
never the ANSI codepage. There is also `tr_win32_format_message(uint32_t code)` for turning a
`GetLastError()` value into a UTF-8 message (via `FormatMessageW`).

**Do not** call the ANSI (`…A`) Win32 functions or build `std::wstring` by hand — round-trip
through these helpers so non-ASCII filenames survive. The `/utf-8` compiler flag (see `../SKILL.md`)
ensures the *source literals* you feed in are already UTF-8.

## Windows path handling (`file-win32.cc`)

`path_to_native_path(std::string_view)` is the internal funnel that turns a UTF-8 path into the
`std::wstring` handed to `CreateFileW` et al. The transform itself lives in the helper
`path_to_fixed_native_path`, which:

1. Converts to UTF-16 via `tr_win32_utf8_to_native`.
2. Rewrites `/` → `\` and **squashes consecutive separators** (multiple `\\` cause
   `ERROR_INVALID_NAME`).

`path_to_native_path` then calls that helper and prepends the **long-path prefix** so paths can
exceed the ~260-char `MAX_PATH` limit:
   - local absolute path → `\\?\` (`NativeLocalPathPrefix`)
   - UNC path (`\\server\share`) → `\\?\UNC\` (`NativeUncPathPrefix`)
   Relative paths get neither prefix (the fixed path is returned as-is).

`native_path_to_path` reverses it for values coming back out (e.g. directory listings). Build paths
with **`tr_pathbuf`** (`= tr_strbuf<char, 4096>`, from `tr-strbuf.h`) — a small-buffer-optimized
path builder — and normalize separators with `tr_sys_path_native_separators` rather than hard-coding
`\` or `/`.

## `subprocess-win32.cc` — process spawning

`tr_spawn_async` builds a command line and environment, converting **every** piece with
`tr_win32_utf8_to_native`, then calls `CreateProcessW` with `CREATE_UNICODE_ENVIRONMENT`
(and `CREATE_NO_WINDOW`, `STARTF_USESHOWWINDOW`+`SW_HIDE`). Failures are reported via
`set_system_error(error, GetLastError(), …)`. It includes `<fmt/xchar.h>` for `wchar_t` formatting.
There is a known limitation flagged in-source: argument escaping for some inputs is imperfect
(`FIXME` in `construct_cmd_line`). The POSIX sibling is `subprocess-posix.cc` (`fork`/`exec`,
`ssize_t`).

## `watchdir-win32.cc` — the auto-add watch directory

Watches a directory for new `.torrent` files using `ReadDirectoryChangesW` on a handle opened with
`CreateFileW(… FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED)`, on a thread started with
`_beginthreadex` (from `<process.h>`), delivering events into the libevent loop via a
`bufferevent`. Filter mask: `FILE_NOTIFY_CHANGE_FILE_NAME | …_SIZE | …_LAST_WRITE`. Unlike file/
subprocess there is **no** `watchdir-posix.cc`; the POSIX watchers are separate backends
(`watchdir-inotify.cc`, `watchdir-kqueue.cc`, `watchdir-generic.cc`) selected by CMake.

## `platform.cc` — config/data directories and a macro-collision workaround

The Windows branch resolves standard directories with `SHGetKnownFolderPath` (`FOLDERID_*`, from
`<shlobj.h>`) via `win32_get_known_folder*`, converting results with `tr_win32_native_to_utf8` and
freeing with `CoTaskMemFree`. Note the top-of-file workaround:

```cpp
#ifdef small // workaround name collision between libsmall and rpcndr.h
#undef small
#endif
```

`<windows.h>`'s `rpcndr.h` `#define`s `small` as `char`, which collides with the bundled
`third-party/small` library — the same class of bug as the `min`/`max` macros. If you add a header
that transitively pulls in `<windows.h>` near code that uses `small`, keep this `#undef`.

## Header / include conventions for `_WIN32` code

- Guard Windows includes with `#ifdef _WIN32` and put `#include <windows.h>` **before** the more
  specific Win32 headers (`<shlobj.h>`, `<process.h>`).
- `-DWIN32_LEAN_AND_MEAN` and `-DNOMINMAX` are set globally in `CMakeLists.txt`; rely on them, and
  do not undo them by defining `min`/`max` or including the fat `<windows.h>` surface.
- Prefer `wchar_t`/`std::wstring` only inside a `*-win32.cc` file; everything crossing back into
  portable code is UTF-8.
- Use `<fmt/xchar.h>` (not bare `<fmt/format.h>`) when formatting wide strings.

Adding or moving any of these files also touches the macOS Xcode project — see **macos-client**
(the Xcode project lists libtransmission sources independently of CMake).
