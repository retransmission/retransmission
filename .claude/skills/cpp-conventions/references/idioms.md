# Core idioms — worked reference

Read this the first time you touch one of these idioms. Every claim below is anchored to a
real file; open the citation and imitate it rather than working from memory. All line
numbers were accurate at authoring time (2026-07); if a file has drifted, trust the file.

---

## 1. `tr_error` — error propagation (libtransmission/error.h)

`struct tr_error` holds a `tr_error_code_t code_` (an `int` on POSIX, `int64_t` on Win32 —
see `error-types.h`) plus a `std::string message_`. It is *not* thrown; it is an **out
parameter**.

**Signature shape** — trailing `tr_error* error = nullptr`, boolean/optional return:

```cpp
// file-utils.h
bool tr_file_read(std::string_view filename, std::vector<char>& contents, tr_error* error = nullptr);
bool tr_file_save(std::string_view filename, std::string_view contents, tr_error* error = nullptr);
```

**In headers, forward-declare** so you don't pull in `error.h`: `struct tr_error;` (see
`file.h:21`).

**Setting an error** — two shapes. When the implementation wants to inspect the error
itself, install a local fallback up front so `error` is never null (`file-utils.cc:31-34`),
then set it unguarded (`file-utils.cc:49`):

```cpp
auto local_error = tr_error{};
if (error == nullptr) {
    error = &local_error;
}
// …
if (!info || !info->isFile()) {
    error->set(TR_ERROR_EISDIR, "Not a regular file"sv);
    return false;
}
```

When the implementation never reads the error back, skip the fallback and guard the set
with the caller's null check instead (`magnet-metainfo.cc:205`):

```cpp
if (!parsed || parsed->scheme != "magnet"sv) {
    if (error != nullptr) {
        error->set(TR_ERROR_EINVAL, "Error parsing URL"sv);
    }
    return false;
}
```

`tr_error::set` is overloaded for `std::string&&`, `std::string_view`, and `char const*`.
For an errno, use the convenience `error.set_from_errno(errno)` (declared in `error.h:68`).
`prefix_message()` prepends context as an error bubbles up. `tr_error` is truthy when it
holds a non-zero code (`explicit operator bool`).

**Callers that don't care** pass nothing (the `= nullptr` default) and just check the bool.

Use platform-neutral code constants `TR_ERROR_EINVAL` / `TR_ERROR_EISDIR` and the
`tr_error_is_enoent()` / `tr_error_is_enospc()` helpers (`error-types.h`) instead of raw
`ENOENT` etc., so Windows keeps working.

---

## 2. `tr_quark` — interned strings (libtransmission/quark.h)

`using tr_quark = size_t`. A quark is a 2-way map between a compile-time interned string and
an integer id, so well-known strings (settings keys, RPC field names, BT protocol keys) are
cheap to store, compare, and `switch` on.

- Predefined quarks are the `TR_KEY_*` enumerators in `quark.h`. **New entries use
  snake_case** (`TR_KEY_sequential_download`).
- Entries suffixed `_camel`, `_kebab`, or `_APICOMPAT` are **deprecated** back-compat
  aliases (old camelCase/kebab-case spellings kept alive only for `api_compat`). Never
  reference them in new code.
- Runtime lookups: `tr_quark_lookup(sv) -> std::optional<tr_quark>`,
  `tr_quark_get_string_view(q)` (result is NUL-terminated at `view[size()]`),
  `tr_quark_new(sv)` (interns, dedup-safe).

Adding a new settings/RPC key touches this file plus several others (the C-API accessors,
`session-settings.h`, the RPC handlers, and each front-end). See the `add-session-setting`
and `rpc-api` skills for that end-to-end checklist, which follows an existing key through
those files.

---

## 3. `tr_variant` + `tr::serializer` — benc/json data model
(libtransmission/variant.h, libtransmission/serializer.h)

`tr_variant` is a tagged union over the benc/json types (bool, int, double, string,
string_view, `Vector`, `Map`). `tr_variant::Map` is keyed by `tr_quark`. Typed access is
via `value_if<T>()` returning an optional (`converters.cc:106`):

```cpp
if (auto const val = src.value_if<std::string_view>()) { /* use *val */ }
if (auto const val = src.value_if<int64_t>())          { /* use *val */ }
```

`tr::serializer` (in the `tr::serializer` namespace) maps a plain struct to/from a
`tr_variant::Map` declaratively. A serializable type declares a static `Fields` tuple of
`Field<&Type::member>{ TR_KEY_… }`. Real example — `session-settings.h:161`:

```cpp
static constexpr auto Fields = std::make_tuple(
    Field<&SessionSettings::announce_ip>{ TR_KEY_announce_ip },
    Field<&SessionSettings::announce_ip_enabled>{ TR_KEY_announce_ip_enabled },
    Field<&SessionSettings::bind_address_ipv4>{ TR_KEY_bind_address_ipv4 },
    /* … */ );
```

The app-layer prefs do the same (`libtransmission-app/prefs.h:86`). Prefer this over
hand-writing per-field load/save code — it's the pattern the recent serializer refactors
(`81261c519`, `82920ad17`, `09822cc1b`) standardized on.

---

## 4. `tr::Values` — bytes, storage, speeds (libtransmission/values.h)

Instead of passing bare integers for byte counts and rates, use the strong types in
`namespace tr::Values`:

```cpp
using Memory  = Value<MemoryUnits,  Config::memory>;
using Storage = Value<StorageUnits, Config::storage>;
using Speed   = Value<SpeedUnits,   Config::speed>;
```

Units are `enum class`es with an explicit width (`enum class SpeedUnits : uint8_t {...}`).
Construct with a value + unit (`Speed{ 5, SpeedUnits::MByps }`), do arithmetic with the
overloaded operators, read back with `.base_quantity()` / `.count(unit)`, and render for the
UI with `.to_string()` (which picks the unit and precision, using `fmt` + locale). This
keeps Kilo-vs-Kibi (`Config::Base`) and human formatting in one place.

---

## 5. `TR_ASSERT` — invariants only (libtransmission/tr-assert.h)

```cpp
TR_ASSERT(n_added <= n_wanted_blocks);              // peer-mgr-wishlist.cc
TR_ASSERT_MSG(cond, "explanatory message");
```

Compiled in only when `NDEBUG` is undefined **or** `TR_FORCE_ASSERTIONS` is defined; in a
normal release build they expand to `(void)0`. So:

- Use them for programmer-invariant checks that should never fire in a correct program.
- **Never** put side effects inside a `TR_ASSERT` — they vanish in release.
- Do **not** use them for runtime/user errors (bad input, failed I/O, network) — that's
  what `tr_error` / `std::optional` returns are for.

---

## 6. `fmt` — string formatting (fmt library, angle-bracket include)

Transmission formats strings with the **fmt** library, not `std::format`. `#include
<fmt/format.h>` and call `fmt::format(...)`, `fmt::format_to_n(...)`, etc. (~60 `.cc` files
in the core use it; `std::format` is absent). `values.h:167` shows the `format_to_n`
into-a-fixed-buffer idiom used in hot paths. Follow the existing usage in the file you're
editing.

---

## 7. `macros.h` — feature-gated constexpr + project constants
(libtransmission/macros.h)

- `TR_CONSTEXPR_STR` / `TR_CONSTEXPR_VEC` / `TR_CONSTEXPR23` expand to `constexpr` only when
  the toolchain's stdlib supports `constexpr` `std::string` / `std::vector` / C++23
  respectively, else to nothing. Use them on members that *want* to be `constexpr` but touch
  `std::string`/`std::vector` (see `error.h`'s `tr_error` methods, and the many
  `TR_CONSTEXPR_VEC` methods in `peer-mgr-wishlist.h`).
- `TR_PROJ_*` are the project identity constants added in
  `0acc01d3a feat: project macros (#90)`: `TR_PROJ_APPNAME` (`"transmission"`),
  `TR_PROJ_APPNAME_CAPITALIZED`, the domain/URL macros (`TR_PROJ_URL_HOMEPAGE`,
  `TR_PROJ_URL_GIT`, …), the D-Bus IDs, and the RPC header names
  (`TR_PROJ_RPC_SESSION_ID_HEADER`). **Build identifiers from these, don't hardcode** the
  app name, URLs, or headers — it keeps a rebrand/fork to one file. They are `#define`d
  string literals (so they concatenate at compile time); note they are `const char*`
  literals, which matters when feeding them through Qt `QString::arg` — concatenate them
  into the `QStringLiteral` format string rather than passing them as `.arg()` values (see
  the `qt-client` skill, which covers this).

---

## Naming quick-reference (enforced by libtransmission/.clang-tidy)

From the `CheckOptions` block of `libtransmission/.clang-tidy`:

- `readability-identifier-naming.VariableCase = lower_case`
- `readability-identifier-naming.ParameterCase = lower_case`
- `readability-identifier-naming.ConstexprVariableCase = CamelCase`
- `readability-identifier-naming.PrivateMemberSuffix = _`
- `readability-identifier-naming.ProtectedMemberSuffix = _`
- uppercase numeric-literal suffixes (`readability-implicit-bool-conversion.UseUpperCaseLiteralSuffix = true`, and `.clang-format` `NumericLiteralCase.Suffix: Upper`)

Class/type PascalCase and C-API `tr_`+camelCase aren't machine-checked but are uniform in
practice (`transmission.h` for the C API; `peer-mgr-wishlist.h`, `values.h`, `serializer.h`
for modern classes). Match the file you're in.

The per-directory `.clang-tidy` files each set their own `HeaderFilterRegex` and disable a
different subset of checks (the `libtransmission-app/`, `gtk/`, `qt/`, and `tests/…`
variants are looser in places where old code isn't cleaned up yet). There is **no root
`.clang-tidy`**. Running tidy is covered in the `building` skill.
