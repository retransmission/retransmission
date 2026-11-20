#!/usr/bin/env python3

import json
import os
import re
import sys
import traceback


def main() -> int:
    database_path, pattern, root = sys.argv[1:]
    # Bare "\n" line endings: Python on Windows would otherwise write "\r\n",
    # and the "\r" rides along into the path ctcache hashes, where it fails
    # the exists() check and leaves the file's content out of the digest.
    sys.stdout.reconfigure(newline="\n")
    matcher = re.compile(pattern)
    with open(database_path, encoding="utf-8") as database_file:
        commands = json.load(database_file)

    seen = set()
    for command in commands:
        filename = command["file"]
        try:
            relative = os.path.relpath(filename, root)
        except Exception as e:
            if os.name == "nt" and isinstance(e, ValueError):
                e.add_note(f"Skipping '{filename}' as it is not in '{root}'")
                traceback.print_exc()
                continue
            e.add_note(f"Failed to compute relative path for '{filename}' from '{root}'")
            raise
        relative = relative.replace(os.sep, "/")
        if matcher.match(relative) and filename not in seen:
            seen.add(filename)
            print(filename)

    return 0


if __name__ == "__main__":
    sys.exit(main())
