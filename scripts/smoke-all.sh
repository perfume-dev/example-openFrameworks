#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
capture_dir="${1:-}"
if [[ -z "$capture_dir" ]]; then
	echo "usage: $0 /path/to/capture-directory" >&2
	exit 2
fi
mkdir -p "$capture_dir"
capture_dir="$(cd "$capture_dir" && pwd)"

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

render_app() {
	local app="$1"
	local artifact="$2"
	shift 2
	local binary app_pid watchdog_pid result
	case "$(uname -s)" in
		Darwin) binary="$repo_root/$app/bin/$app.app/Contents/MacOS/$app" ;;
		Linux) binary="$repo_root/$app/bin/$app" ;;
		*) echo "Unsupported platform: $(uname -s)" >&2; exit 2 ;;
	esac
	if [[ ! -x "$binary" ]]; then
		echo "Build $app first with scripts/build-all.sh." >&2
		exit 1
	fi
	echo "Rendering $app"
	# Linux resolves data relative to the working directory; use the same bin layout on both platforms.
	(
		cd "$repo_root/$app/bin"
		exec "$binary" --smoke-test "--capture=$capture_dir/$artifact.png" "$@"
	) >"$capture_dir/$artifact.log" 2>&1 &
	app_pid=$!
	# Bound an unresponsive renderer without terminating unrelated processes.
	(
		sleep 90
		kill -TERM "$app_pid" 2>/dev/null || true
	) &
	watchdog_pid=$!
	result=0
	wait "$app_pid" || result=$?
	kill "$watchdog_pid" 2>/dev/null || true
	wait "$watchdog_pid" 2>/dev/null || true
	if [[ "$result" -ne 0 ]] || ! grep -q 'SMOKE_TEST_OK' "$capture_dir/$artifact.log" \
		|| [[ ! -s "$capture_dir/$artifact.png" ]]; then
		cat "$capture_dir/$artifact.log" >&2
		echo "$app rendering failed (exit $result)." >&2
		exit 1
	fi
	grep 'SMOKE_TEST_OK' "$capture_dir/$artifact.log"
}

for app in "${apps[@]}"; do
	render_app "$app" "$app"
done

# A second pose verifies that each new shader study produces changing artwork.
for app in example-motion-ribbons example-motion-field; do
	render_app "$app" "$app-later" --capture-frame=420
	if cmp -s "$capture_dir/$app.png" "$capture_dir/$app-later.png"; then
		echo "$app produced identical captures at different motion frames." >&2
		exit 1
	fi
done
