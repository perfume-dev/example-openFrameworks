#include "ofApp.h"

void ofApp::RibbonTrail::update(const glm::vec3& noiseOffset) {
	if (!motion->isFrameNew()) return;
	for (size_t age = 0; age < history.size(); ++age) {
		const float t = float(age) / float(history.size());
		for (auto& position : history[age]) {
			glm::vec3 force{0};
			force.y = -2.5f * (1.0f - std::sin(t * t * PI)) + ofNoise(position.y * 0.0001f + noiseOffset.y) * 1.4f;
			force.x = ofSignedNoise(position.x * 0.0001f + noiseOffset.x) * 3.0f;
			force.z = ofSignedNoise(position.z * 0.0001f + noiseOffset.z) * 3.0f;
			if (position.y < 0) force *= glm::vec3(5.0f, 0.02f, 5.0f);
			position += force;
		}
	}
	Frame frame;
	for (int i = 0; i < motion->getNumJoints(); ++i) {
		const auto* joint = motion->getJoint(i);
		for (const auto* child : joint->getChildren()) {
			frame.emplace_back(joint->getPosition());
			frame.emplace_back(child->getPosition());
		}
	}
	history.push_front(std::move(frame));
	if (history.size() > 200) history.pop_back();
	surfaces.clear();
	outlines.clear();
	for (size_t bone = 0; bone + 1 < history.front().size(); bone += 2) {
		ofVboMesh strip;
		strip.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
		ofPolyline left;
		ofPolyline right;
		glm::vec3 normal{0, 1, 0};
		for (size_t age = 0; age + 1 < history.size(); ++age) {
			const float t = ofMap(float(age), 0, float(history.size()), 0.1f, 1.0f);
			const auto& a = history[age][bone];
			const auto& b = history[age][bone + 1];
			const glm::vec3 direction = a - b;
			const auto side = example::safeNormalize(glm::cross(direction, glm::vec3(0, 1, 0)));
			normal = example::safeNormalize(glm::mix(normal, example::safeNormalize(glm::cross(side, direction)), 0.3f));
			const auto tip = glm::mix(b, a, t);
			strip.addVertex(a);
			strip.addNormal(normal);
			strip.addVertex(tip);
			strip.addNormal(normal);
			left.addVertex(a);
			right.addVertex(tip);
		}
		surfaces.push_back(std::move(strip));
		outlines.push_back(std::move(left));
		outlines.push_back(std::move(right));
	}
	skeleton.clear();
	skeleton.setMode(OF_PRIMITIVE_LINES);
	skeleton.addVertices(history.front());
}

void ofApp::RibbonTrail::draw() {
	// Polygon offset remains part of core OpenGL; geometry lives in vertex buffers.
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1, 1);
	ofSetColor(255);
	for (auto& surface : surfaces) surface.draw();
	glDisable(GL_POLYGON_OFFSET_FILL);
	ofSetColor(0);
	for (const auto& outline : outlines) outline.draw();
	skeleton.draw();
}

void ofApp::setup() {
	runtime.setup();
	ofBackground(255);
	playback.load("example-pen-graphics", false, runtime.isSmokeTest());
	for (size_t i = 0; i < trails.size(); ++i) trails[i].motion = &playback.motions[i];
	noiseOffset = {ofRandom(1), ofRandom(1), ofRandom(1)};
	noiseVelocity = {ofRandom(0.001f), ofRandom(0.005f), ofRandom(0.001f)};
	ground.setMode(OF_PRIMITIVE_LINES);
	for (int x = -10; x < 10; ++x) {
		for (int z = -10; z < 10; ++z) {
			const glm::vec3 position(x * 500, 0, z * 500);
			ground.addVertex(position + glm::vec3(-10, 0, 0));
			ground.addVertex(position + glm::vec3(10, 0, 0));
			ground.addVertex(position + glm::vec3(0, 0, -10));
			ground.addVertex(position + glm::vec3(0, 0, 10));
		}
	}
}

void ofApp::update() {
	playback.update(runtime.deltaSeconds());
	if (!playback.ready) return;
	glm::vec3 targetCenter{0};
	for (const auto& motion : playback.motions) targetCenter += glm::vec3(motion.getJoint(0)->getPosition());
	center = glm::mix(center, targetCenter / 3.0f, 1.0f - std::exp(-0.6f * runtime.deltaSeconds()));
	for (auto& trail : trails) trail.update(noiseOffset);
	noiseOffset += noiseVelocity * runtime.deltaSeconds() * 60.0f;
	cameraPosition = glm::mix(cameraPosition, cameraTarget, 1.0f - std::exp(-0.6f * runtime.deltaSeconds()));
	camera.setPosition(cameraPosition);
	camera.lookAt(glm::vec3(0));
}

void ofApp::draw() {
	ofEnableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	camera.begin();
	ofPushMatrix();
	ofRotateYDeg(runtime.elapsedSeconds() * 20.0f);
	ofTranslate(-center.x, -100, -center.z);
	ofSetColor(200);
	ground.draw();
	for (auto& trail : trails) trail.draw();
	ofPopMatrix();
	camera.end();
	ofDisableDepthTest();
	ofSetColor(20);
	playback.drawStatus();
	runtime.finishFrame(playback.ready, "example-pen-graphics", trails.front().skeleton.getNumVertices());
}

void ofApp::keyPressed(int) {
	cameraTarget = {ofRandom(-600, 600), ofRandom(-100, 200), ofRandom(-600, 600)};
}
