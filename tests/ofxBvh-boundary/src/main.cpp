#include "ofMain.h"
#include "ofxBvh.h"
#include "ofxMarchingCubes.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {

bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "FAIL: " << message << '\n';
	return condition;
}

} // namespace

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "usage: ofxBvh-boundary <sample-data-directory> <fixture-directory>\n";
		return 2;
	}

	const std::filesystem::path sampleDirectory = argv[1];
	const std::filesystem::path fixtureDirectory = argv[2];
	ofxBvh motion;
	bool passed = expect(motion.load(sampleDirectory / "A_test.bvh"), "sample BVH should load");
	passed &= expect(motion.isLoaded(), "loaded state should be true");
	passed &= expect(motion.getNumFrames() > 1, "sample should contain multiple frames");

	const int lastFrame = motion.getNumFrames() - 1;
	motion.setFrame(-1);
	passed &= expect(motion.getFrame() == 0, "negative frame should be ignored");
	motion.setFrame(motion.getNumFrames());
	passed &= expect(motion.getFrame() == 0, "one-past-end frame should be ignored");
	motion.setPosition(1.0f);
	passed &= expect(motion.getFrame() == lastFrame, "position 1 should select the last frame");

	motion.setLoop(false);
	motion.setRate(1.0f);
	motion.play();
	motion.update(motion.getDuration());
	passed &= expect(motion.getFrame() == lastFrame, "non-looping playback should clamp at the last frame");
	passed &= expect(!motion.isPlaying(), "non-looping playback should stop at the end");

	motion.setLoop(true);
	motion.play();
	motion.update(motion.getDuration() * 2.5f);
	passed &= expect(motion.getFrame() >= 0 && motion.getFrame() <= lastFrame,
		"looping playback should wrap to a valid frame");
	const int frameBeforeInvalidDelta = motion.getFrame();
	motion.update(std::numeric_limits<float>::infinity());
	passed &= expect(motion.getFrame() == frameBeforeInvalidDelta,
		"non-finite delta should be ignored");
	motion.setRate(std::numeric_limits<float>::max());
	motion.update(std::numeric_limits<float>::max());
	passed &= expect(motion.isLoaded()
		&& motion.getFrame() >= 0 && motion.getFrame() <= lastFrame,
		"large finite playback values should remain safe");

	motion.setLoop(false);
	motion.setRate(-1.0f);
	motion.setFrame(0);
	motion.play();
	motion.update(1.0f);
	passed &= expect(motion.getFrame() == 0, "reverse playback should clamp at frame zero");
	passed &= expect(!motion.isPlaying(), "reverse playback should stop at frame zero");

	motion.unload();
	passed &= expect(!motion.isLoaded(), "unload should clear loaded state");
	passed &= expect(motion.getFrame() == 0, "empty motion should have a safe frame value");
	passed &= expect(motion.load(fixtureDirectory / "motion-in-joint-name.bvh"),
		"MOTION inside a joint name should not be mistaken for the section marker");
	motion.unload();

	const std::array<std::filesystem::path, 7> malformedFiles = {
		"truncated-hierarchy.bvh",
		"short-channel-name.bvh",
		"malformed-motion-header.bvh",
		"channel-count-mismatch.bvh",
		"trailing-hierarchy-data.bvh",
		"malformed-header-prefix.bvh",
		"invalid-channel-suffix.bvh",
	};
	for (const auto& file : malformedFiles) {
		passed &= expect(!motion.load(fixtureDirectory / file),
			("malformed fixture should be rejected: " + file.string()).c_str());
		passed &= expect(!motion.isLoaded(), "failed load should leave the parser unloaded");
	}
	passed &= expect(!motion.load(fixtureDirectory / "does-not-exist.bvh"),
		"a missing file should be rejected");

	ofxMarchingCubes marchingCubes;
	marchingCubes.clear();
	marchingCubes.clear();
	marchingCubes.update(0.5f);
	passed &= expect(marchingCubes.getGridRes() == ofPoint(0, 0, 0),
		"an uninitialized marching-cubes grid should remain safely empty");
	marchingCubes.init(ofPoint(0, 0, 0), ofPoint(2, 2, 2), 2, 2, 2);
	const ofPoint gridPoint = marchingCubes.getGrid()[0][0][0];
	marchingCubes.addMetaBall(gridPoint, 1.0f);
	passed &= expect(std::isfinite(marchingCubes.getIsoValue(0, 0, 0)),
		"a metaball exactly on a grid point should remain finite");
	marchingCubes.clear();
	marchingCubes.update(0.5f);
	passed &= expect(marchingCubes.getNumTriangles() == 0,
		"a cleared marching-cubes grid should update safely");

	if (!passed) return 1;
	std::cout << "regression tests passed\n";
	return 0;
}
