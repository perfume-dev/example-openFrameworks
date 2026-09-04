#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
	export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
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
	project="$repo_root/$app/$app.xcodeproj"
	pbxproj="$project/project.pbxproj"

	test -f "$pbxproj"
	xcodebuild -project "$project" -list >/dev/null

	grep -Fq '"path": "src/main.cpp"' "$pbxproj"
	grep -Fq '"name": "ofApp.cpp"' "$pbxproj"
	grep -Fq '"name": "ofApp.h"' "$pbxproj"
	grep -Fq '"path": "../ofxBvh/src/ofxBvh.cpp"' "$pbxproj"
	if grep -Eq 'testApp\.(cpp|h)' "$pbxproj"; then
		echo "$app references a removed testApp source" >&2
		exit 1
	fi

	if [[ "$app" == "marching-cubes" ]]; then
		grep -Fq '"path": "../third_party/ofxMarchingCubes/src/ofxMarchingCubes.cpp"' "$pbxproj"
	fi

	echo "$app Xcode project: OK"
done
