#include "ofApp.h"

void ofApp::setup() {
	runtime.setup();
	ofBackground(0);
	playback.load("example-bvh", true, runtime.isSmokeTest());
	camera.setTarget(glm::vec3(0, 80, 0));
	camera.setDistance(450);
}

void ofApp::update() {
	playback.update(runtime.deltaSeconds());
}

void ofApp::draw() {
	ofEnableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	camera.begin();
	for (auto& motion : playback.motions) motion.draw();
	camera.end();
	ofDisableDepthTest();
	if (!playback.ready) {
		ofSetColor(255);
		ofDrawBitmapString("Unable to load the bundled BVH files.", 20, 30);
	}
	runtime.finishFrame(playback.ready, "example-bvh", playback.motions.front().getNumJoints() * 48);
}
