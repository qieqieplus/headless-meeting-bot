#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# src/scripts -> src (parent)
SRC_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# repo root is one level above src
REPO_ROOT="$(cd "${SRC_DIR}/.." && pwd)"

if [ ! -f "${SRC_DIR}/.clang-format" ]; then
  echo ".clang-format not found at project root; formatting may use clang-format defaults" >&2
fi

mapfile -t FILES < <(find "${SRC_DIR}" -type f \( \
  -name '*.c' -o -name '*.cc' -o -name '*.cxx' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
\) ! -path "${SRC_DIR}/build/*" | sort)

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "No source files found under ${SRC_DIR}" >&2
  exit 0
fi

echo "Formatting ${#FILES[@]} files under ./src"

for f in "${FILES[@]}"; do
  echo "Formatting: ${f/#${REPO_ROOT}\//}"
  clang-format -i "${f}"
done

echo "Done. Only files under ./src were formatted."
