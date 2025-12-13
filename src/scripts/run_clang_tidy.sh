#!/bin/bash

set -euo pipefail

# Run clang-tidy only on project sources under ./src
# Usage:
#   src/scripts/run_clang_tidy.sh check             # Check with all rules from config
#   src/scripts/run_clang_tidy.sh fix               # Fix with all rules from config
#   src/scripts/run_clang_tidy.sh check modernize-* # Check with just modernize rules

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# src/scripts -> src (parent)
SRC_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# repo root is one level above src
REPO_ROOT="$(cd "${SRC_DIR}/.." && pwd)"
BUILD_DIR="${SRC_DIR}/build"

MODE="${1:-check}"
CHECKS_ARG="${2:-}" # Optional: comma-separated list of checks to run

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
  echo "Error: compile_commands.json not found at ${BUILD_DIR}/compile_commands.json" >&2
  echo "Tip: generate it with: cmake -S src -B src/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 1
fi

# Only allow header edits under selected subdirs in ./src
HEADER_FILTER='^src/(video|util|events)/'

# Configs live alongside this script
DEFAULT_CFG="${SCRIPT_DIR}/clang-tidy-default.yaml"
C_API_CFG="${SCRIPT_DIR}/clang-tidy-c-api.yaml"

if [ ! -f "${DEFAULT_CFG}" ] || [ ! -f "${C_API_CFG}" ]; then
  echo "Error: clang-tidy config files not found next to the script." >&2
  echo "Missing: ${DEFAULT_CFG} or ${C_API_CFG}" >&2
  exit 1
fi

# Gather source files under selected directories only (exclude build dir)
FILES=()
TARGET_DIRS=(video util events)
for d in "${TARGET_DIRS[@]}"; do
  if [ -d "${SRC_DIR}/${d}" ]; then
    while IFS= read -r -d '' f; do
      FILES+=("${f}")
    done < <(find "${SRC_DIR}/${d}" -type f \( \
      -name '*.c' -o -name '*.cc' -o -name '*.cxx' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
    \) ! -path "${SRC_DIR}/build/*" -print0)
  fi
done

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "No source files found under ${SRC_DIR}" >&2
  exit 0
fi

echo "Mode: ${MODE} (targeting only ./src via --header-filter=${HEADER_FILTER})"

run_one() {
  local f="$1"
  local cfg="${DEFAULT_CFG}"
  # This script no longer processes c_api

  local tidy_args=(-p="${BUILD_DIR}" --header-filter="${HEADER_FILTER}" --config-file "${cfg}")
  if [ -n "${CHECKS_ARG}" ]; then
    tidy_args+=(--checks="${CHECKS_ARG}")
    echo "Running with specific checks: ${CHECKS_ARG}"
  fi

  if [ "${MODE}" = "fix" ]; then
    tidy_args+=(--fix --fix-errors)
    clang-tidy "${tidy_args[@]}" "${f}" || true
  else
    clang-tidy "${tidy_args[@]}" "${f}" || true
  fi
}

# Run serially for clarity; switch to parallel if desired
for file in "${FILES[@]}"; do
  echo "Processing: ${file/#${REPO_ROOT}\//}"
  run_one "${file}"
done

echo "Done. Only files under ./src were analyzed/modified."


