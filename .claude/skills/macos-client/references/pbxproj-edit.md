# Editing `Transmission.xcodeproj/project.pbxproj` by hand

Read this before your first edit to the Xcode project. It assumes you have read the "two
independent build systems" and "Editing project.pbxproj from Linux" sections of the parent
`SKILL.md`. The golden rule: **hand-splice strings, never round-trip through a Python library**
(a no-op mod-pbxproj save reshuffles ~314 lines of this repo's file).

## File format in 30 seconds

`project.pbxproj` is one OpenStep-style property list. It is a flat dictionary of **objects**, each
keyed by a **24-hex-character object ID** (Xcode's UUIDs, e.g. `ED6F16B22EB8F1EB007CD864`). The
file is organized into `/* Begin <Type> section */ … /* End <Type> section */` blocks. The
sections you touch when adding a file:

- **PBXFileReference** — one per file on disk (its path + type).
- **PBXBuildFile** — one per file that is *compiled or copied into a target* (references a
  PBXFileReference by `fileRef`). Sources and public headers get one; a plain app header does not.
- **PBXGroup** — the Project-navigator folders; each lists its children by PBXFileReference ID.
- **PBXSourcesBuildPhase / PBXHeadersBuildPhase / PBXResourcesBuildPhase** — a target's compile /
  header / resource lists; each lists PBXBuildFile IDs.

The `/* Foo.mm */` comments are cosmetic (Xcode regenerates them) but **always include them and
keep them accurate** — every hand-written entry in this repo has them, and they make the diff
reviewable.

### Object-ID rules

- 24 uppercase hex characters, **unique within the file**. Xcode normally uses the first 8 as a
  timestamp-ish prefix, but the build does not care — uniqueness is the only requirement.
- Easiest safe approach when adding a batch: pick an unused prefix and increment the last digits,
  exactly as `d1985b05c` did (`ED6F16AF…`, `ED6F16B0…`, `ED6F16B1…`, …). Before committing, confirm
  none of your new IDs already exist: `grep -c '<YOURID>' Transmission.xcodeproj/project.pbxproj`
  must print `0` before you insert (and each ID should appear exactly the expected number of times
  after).
- Each **file** needs one PBXFileReference ID. Each **compiled/copied** file additionally needs one
  PBXBuildFile ID whose `fileRef` points at the file-reference ID.

## Recipe: add a macOS-app source pair `Foo.h` + `Foo.mm`

Pick IDs: `FILEREF_H`, `FILEREF_MM` (file references) and `BUILD_MM` (build file for the `.mm`
only). Then make these edits, each inserted next to an alphabetically/locally adjacent sibling so
the diff stays tiny.

**1. PBXBuildFile section** — the `.mm` only (app `.h` files are not in a Headers phase):

```
BUILD_MM /* Foo.mm in Sources */ = {isa = PBXBuildFile; fileRef = FILEREF_MM /* Foo.mm */; };
```

**2. PBXFileReference section** — both files. File-type tokens (verified in this repo):
`.mm` → `sourcecode.cpp.objcpp`, `.h` → `sourcecode.c.h`, `.cc`/`.cpp` → `sourcecode.cpp.cpp`,
`.c` → `sourcecode.c.c`, `.xib` → `file.xib`, `.strings` → `text.plist.strings`.

```
FILEREF_H  /* Foo.h */  = {isa = PBXFileReference; lastKnownFileType = sourcecode.c.h;       path = Foo.h;  sourceTree = "<group>"; };
FILEREF_MM /* Foo.mm */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.cpp.objcpp; path = Foo.mm; sourceTree = "<group>"; };
```

**3. PBXGroup children** — add both file references to the right group (the navigator folder the
file belongs in, e.g. the "File Outline View" group in the exemplar). Reference the **file-ref**
IDs:

```
    FILEREF_H  /* Foo.h */,
    FILEREF_MM /* Foo.mm */,
```

**4. PBXSourcesBuildPhase** — add the `.mm` to the **Transmission app target's** Sources phase.
Reference the **build-file** ID:

```
    BUILD_MM /* Foo.mm in Sources */,
```

**5. Mirror in CMake** — add `Foo.h` / `Foo.mm` to the `target_sources(${TR_NAME}-mac …)` list in
`macosx/CMakeLists.txt` (keep it sorted the way the surrounding lines are). Do this in the **same
commit**.

A header-only addition stops after steps 2, 3, and 5 (no PBXBuildFile, no Sources entry).

## The exemplar: `d1985b05c macos: View-based FileOutlineView (#7760)`

Run `git show d1985b05c -- Transmission.xcodeproj/project.pbxproj macosx/CMakeLists.txt`. It
replaces `FileNameCell`/`FilePriorityCell` with `FileNameCellView`/`FilePriorityCellView`/
`FileCheckCellView` and shows every section in action. What to notice:

- It allocated six sequential file-ref IDs `ED6F16AF…`–`ED6F16B4…` (three `.h` + three `.mm`), then
  three build-file IDs `ED6F16B5…`–`ED6F16B7…` (one per `.mm`).
- The three `.h` files appear **only** in the PBXFileReference section and in the "File Outline
  View" PBXGroup — **no** PBXBuildFile line, **no** Sources-phase line. That is the proof that app
  headers need just two edits.
- Each `.mm` appears in all four sections. In the Sources phase the entry uses the **build-file**
  ID (`ED6F16B5…`), whose PBXBuildFile line points `fileRef` at the **file-ref** ID (`ED6F16B2…`).
- The removed files (`FileNameCell`, `FilePriorityCell`) were deleted from the exact same four
  sections — removal is the reverse of addition.
- `macosx/CMakeLists.txt` got the parallel `target_sources` edit in the same commit.

## Variant: adding a `libtransmission/` file (this one uses a Headers phase)

If you add `libtransmission/foo.{cc,h}`, the xcodeproj's **`libtransmission` static-library
target** must be updated — this is the pure-backend trap that breaks the mac build without touching
`macosx/`. **Always mirror whatever a recently-added sibling did** rather than trusting a rule of
thumb; the two cases:

- **A `.cc`** must land in the libtransmission target's **Sources** build phase, or it won't
  compile — so it needs all four edits (file-ref, build-file, group child, Sources-phase entry),
  exactly like a mac `.mm`. Copy a neighbor such as `settings.cc` or `torrents.cc`.
- **A `.h`** needs only the **file-reference + PBXGroup** entries in practice. The libtransmission
  target *does* own a `PBXHeadersBuildPhase` (11 targets here do) and many older headers are listed
  in it, but a Headers-phase entry is **not required to build** — the most recent precedent,
  `cbaefab59 refactor: add libtransmission/types.h (#8449)`, added `types.h` with **only** a
  PBXFileReference line and a PBXGroup child (no PBXBuildFile, no Headers-phase entry) and CI
  passed. When in doubt, `git show cbaefab59 -- Transmission.xcodeproj/project.pbxproj` and copy
  that shape.

Either way, still mirror the change in `libtransmission/CMakeLists.txt`.

## Adding a whole new bundled third-party library

Much bigger than adding a file: you must create a **new static-library target** (its own
PBXNativeTarget, build phases, product reference, and a `PBXTargetDependency` + a
`… .a in Frameworks` PBXBuildFile on every target that links it), mirroring an existing lib target
like `psl` (simple) or `archive` (which also carries a Darwin `config.h`). Do it by copying an
existing lib target's objects wholesale and renaming.
This is rare and belongs with the **third-party-deps** skill; treat the CMake side as the source of
truth for what the new lib is, then reproduce it in the xcodeproj.

## Validate before you push (no Mac required)

1. **Structural parse** — `pbxproj` (mod-pbxproj) is installed here (v4.3.3):
   ```bash
   python3 -c "from pbxproj import XcodeProject; XcodeProject.load('Transmission.xcodeproj/project.pbxproj'); print('parse OK')"
   ```
   A second parser, the `xcodeproj` PyPI package, is what the maintainer also loads it in (not
   installed by default — `pip install xcodeproj` if you want the cross-check). Parsing with a
   library is **read-only validation only** — do not use it to *write* the change (that re-churns
   the file).
2. **ID hygiene** — grep each new ID; a file-ref ID should appear in its PBXFileReference line, its
   PBXGroup line, and (for sources/headers) as the `fileRef` of its PBXBuildFile; a build-file ID
   should appear in its PBXBuildFile line and in exactly one build phase.
3. **Balance** — the count of `/* Begin … */` and `/* End … */` markers and the brace balance must
   be intact; a good `git diff` should show **only** your inserted lines, nothing reordered.
4. **`code_style.sh` guards three pbxproj lines** — `./code_style.sh --check` (run by the
   pre-commit hook and CI's `code-style` job) greps `project.pbxproj` for `objectVersion = 54;`,
   `compatibilityVersion = "Xcode 12.0";`, and `BuildIndependentTargetsInParallel = YES;`. A splice
   that drops one fails `code-style` before `xcodebuild` ever runs — run it after every pbxproj edit.
5. **CI is the real gate** — a clean parse does not prove the target wiring is right. Only
   `macos-xcodebuild-universal` in `.github/workflows/actions.yml` compiles it. Push and watch that
   job.

## Demonstrating the churn (why we hand-edit) — reproduce it yourself

```bash
cp Transmission.xcodeproj/project.pbxproj /tmp/orig.pbxproj
python3 -c "from pbxproj import XcodeProject; p=XcodeProject.load('/tmp/orig.pbxproj'); p.save('/tmp/roundtrip.pbxproj')"
diff /tmp/orig.pbxproj /tmp/roundtrip.pbxproj | grep -cE '^[<>]'   # ~314 lines changed for a no-op
```

That reshuffle is exactly what a library-written edit would inflict on your PR. Hand-splicing keeps
the diff to the handful of lines you actually added.
