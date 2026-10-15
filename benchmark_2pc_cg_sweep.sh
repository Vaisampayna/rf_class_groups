#!/usr/bin/env bash
set -euo pipefail

# Legacy entrypoint. The current two-machine benchmark driver is
# benchmark_2pc_paper_all.sh.

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "Running benchmark_2pc_paper_all.sh $*"

exec bash "$ROOT/benchmark_2pc_paper_all.sh" "$@"
