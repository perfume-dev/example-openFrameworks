#include "ofApp.h"

void ofApp::setup() {
	runtime.setup();
	playback.load("motion-visualization", false, runtime.isSmokeTest());
	if (playback.ready) {
		for (auto& motion : playback.motions) {
			for (int i = 0; i < motion.getNumJoints(); ++i) trails.push_back({motion.getJoint(i), {}});
		}
	}
	camera.setFov(45);
	camera.setDistance(520);
	camera.disableMouseInput();
	if (ofFile::doesFileExist("background.png")) background.load("background.png");
	ribbons.setMode(OF_PRIMITIVE_TRIANGLES);
}

void ofApp::update() {
	rotation += runtime.deltaSeconds() * 2.4f;
	playback.update(runtime.deltaSeconds());
	if (!playback.ready) return;
	for (auto& trail : trails) {
		const glm::vec3 position(trail.joint->getPosition());
		if (trail.points.empty() || glm::distance(position, trail.points.front()) > 1.0f) {
			trail.points.push_front(position);
			if (trail.points.size() > 200) trail.points.pop_back();
		}
	}
}

void ofApp::draw() {
	ofBackground(0);
	ofSetColor(255);
	if (background.isAllocated()) background.draw(0, 0, ofGetWidth(), ofGetHeight());
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	camera.begin();
	ofPushMatrix();
	ofTranslate(0, -80);
	ofRotateYDeg(rotation);

	// Camera-facing triangle ribbons replace unsupported wide core-profile lines.
	const glm::mat4 inverseView = glm::inverse(ofGetCurrentMatrix(OF_MATRIX_MODELVIEW));
	const glm::vec3 viewDirection = example::safeNormalize(glm::vec3(inverseView[2]));
	ribbons.clear();
	for (const auto& trail : trails) {
		for (size_t i = 0; i + 1 < trail.points.size(); ++i) {
			const auto& a = trail.points[i];
			const auto& b = trail.points[i + 1];
			const float distance = glm::distance(a, b);
			if (distance <= 1e-6f || distance >= 40.0f) continue;
			const auto side = example::safeNormalize(glm::cross(b - a, viewDirection))
				* ofMap(distance, 0, 30, 0.2f, 2.4f, true);
			const ofFloatColor color(ofColor(ofClamp(distance * 20, 0, 255),
				ofClamp(127 - distance * 10, 0, 255), ofClamp(255 - distance * 20, 0, 255)));
			for (const auto& vertex : {a - side, a + side, b + side, a - side, b + side, b - side}) {
				ribbons.addVertex(vertex);
				ribbons.addColor(color);
			}
		}
	}
	ribbons.draw();
	ofPopMatrix();
	camera.end();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);
	playback.drawStatus();
	runtime.finishFrame(playback.ready, "motion-visualization", ribbons.getNumVertices());
}

void ofApp::keyPressed(int key) {
	if (key == 'f') ofToggleFullscreen();
}
