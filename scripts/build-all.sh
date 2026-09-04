#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
of_root="${1:-${OF_ROOT:-}}"
jobs="${JOBS:-2}"

if [[ -z "$of_root" ]]; then
	echo "usage: $0 /path/to/openFrameworks" >&2
	exit 2
fi

if [[ ! -f "$of_root/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk" ]]; then
	echo "not an openFrameworks root: $of_root" >&2
	exit 2
fi

apps=(
	"example-bvh"
	"example-sync-sound"
	"example-pen-graphics"
	"particle-motion-example"
	"motion-visualization"
	"marching-cubes"
	"example-motion-ribbons"
	"example-motion-field"
)

for app in "${apps[@]}"; do
	echo "==> Building $app"
	make -C "$repo_root/$app" Release OF_ROOT="$of_root" -j"$jobs"
done

echo "==> Building regression test suite"
make -C "$repo_root/tests/ofxBvh-boundary" Release OF_ROOT="$of_root" -j"$jobs"

echo "==> Running regression test suite"
"$repo_root/tests/ofxBvh-boundary/bin/ofxBvh-boundary.app/Contents/MacOS/ofxBvh-boundary" \
	"$repo_root/example-bvh/bin/data" \
	"$repo_root/tests/ofxBvh-boundary/fixtures"
