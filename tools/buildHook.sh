#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas
#
# Claude Code PostToolUse hook (see .claude/settings.json): rebuilds after an
# edit to a C++ or CMake file. On a failed build it prints the error lines to
# stderr and exits 2, which passes them back to Claude. Other files (docs,
# notes, JSON data) are skipped.

file=$(jq -r '.tool_input.file_path // empty')

case "$file" in
    *.cpp|*.h|*.hpp|*/CMakeLists.txt|*.cmake)
        ;;
    *)
        exit 0
        ;;
esac

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

if ! out=$(cmake --build build -j"$(nproc)" 2>&1); then
    echo "Build failed after editing $file:" >&2
    echo "$out" | grep -E 'error|Error' | head -30 >&2
    exit 2
fi

exit 0
