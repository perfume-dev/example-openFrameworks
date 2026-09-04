#include "ofApp.h"

void ofApp::Trail::update() {
	if (!joint->getBvh()->isFrameNew()) return;
	points.push_front(glm::vec3(joint->getPosition()));
	if (points.size() > 15) points.pop_back();
	mesh.clear();
	mesh.setMode(OF_PRIMITIVE_LINE_STRIP);
	for (size_t i = 0; i + 1 < points.size(); ++i) {
		const float age = float(i) / float(points.size() - 1);
		const float alpha = (1.0f - age) * ofMap(glm::distance(points[i], points[i + 1]), 3, 5, 0, 1, true);
		mesh.addVertex(points[i]);
		mesh.addColor(ofFloatColor(1, 1, 1, alpha));
	}
}

void ofApp::setup() {
	runtime.setup();
	ofBackground(0);
	playback.load("example-sync-sound", false, runtime.isSmokeTest());
	if (!playback.ready) return;
	for (auto& motion : playback.motions) {
		for (int i = 0; i < motion.getNumJoints(); ++i) {
			Trail trail;
			trail.joint = motion.getJoint(i);
			trails.push_back(std::move(trail));
		}
	}
}

void ofApp::update() {
	const float dt = runtime.deltaSeconds();
	rotation += 6.0f * dt;
	playbackSpeed = glm::mix(playbackSpeed, targetSpeed, 1.0f - std::exp(-21.4f * dt));
	playback.update(dt, playbackSpeed);
	for (auto& trail : trails) trail.update();
}

void ofApp::draw() {
	ofEnableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofPushMatrix();
	ofTranslate(ofGetWidth() * 0.5f, ofGetHeight() * 0.5f + 150.0f);
	ofRotateXDeg(-15);
	ofRotateYDeg(rotation);
	ofScale(1, -1, 1);
	ofSetColor(255);
	ofDrawLine(glm::vec3(-100, 0, 0), glm::vec3(100, 0, 0));
	ofDrawLine(glm::vec3(0, 0, -100), glm::vec3(0, 0, 100));
	for (auto& motion : playback.motions) motion.draw();
	ofDisableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	for (auto& trail : trails) trail.mesh.draw();
	ofPopMatrix();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);
	ofDrawBitmapString("Hold a key to scratch\nPlayback speed: " + ofToString(playbackSpeed, 1), 10, 20);
	playback.drawStatus(10, 65);
	runtime.finishFrame(playback.ready, "example-sync-sound", playback.motions.front().getNumJoints() * 48);
}

void ofApp::keyPressed(int) { targetSpeed = -1.0f; }
void ofApp::keyReleased(int) { targetSpeed = 1.0f; }
