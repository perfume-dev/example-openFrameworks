#include "ofApp.h"

void ofApp::setup() {
	runtime.setup();
	playback.load("marching-cubes", false, runtime.isSmokeTest());
	camera.setFov(30);
	camera.setDistance(700);
	// The vendored addon retains ofPoint in its public interface.
	marchingCubes.init(ofPoint(0, 0, 0), ofPoint(550, 550, 550), 60, 60, 60);
	if (playback.ready) {
		for (auto& motion : playback.motions) {
			for (int i = 0; i < motion.getNumJoints(); ++i) {
				const auto* joint = motion.getJoint(i);
				if (joint->isSite()) balls.push_back({joint, {glm::vec3(joint->getPosition())}});
			}
		}
	}
	surface.setMode(OF_PRIMITIVE_TRIANGLES);
	light.setPosition(200, 350, 500);
	light.setAmbientColor(ofFloatColor(0.1f, 0.3f, 0.8f));
	light.setDiffuseColor(ofFloatColor(0.7f));
	light.setSpecularColor(ofFloatColor(1.0f, 0.5f, 0.0f));
	material.setDiffuseColor(ofFloatColor(0.8f));
	material.setSpecularColor(ofFloatColor(1.0f));
	material.setShininess(60);
}

void ofApp::update() {
	playback.update(runtime.deltaSeconds());
	if (!playback.ready) return;
	marchingCubes.resetIsoValues();
	for (auto& tracked : balls) {
		tracked.ball.follow(glm::vec3(tracked.joint->getPosition()), runtime.deltaSeconds());
		const auto& position = tracked.ball.position;
		marchingCubes.addMetaBall(ofPoint(position.x, position.y, position.z), tracked.ball.size);
	}
	marchingCubes.update(0.17f, true);
	surface.clear();
	const auto& vertices = marchingCubes.getVertices();
	const auto& normals = marchingCubes.getNormals();
	for (size_t i = 0; i < vertices.size(); ++i) {
		surface.addVertex(glm::vec3(vertices[i]));
		surface.addNormal(glm::vec3(normals[i]));
	}
}

void ofApp::draw() {
	ofBackground(34);
	ofEnableDepthTest();
	camera.begin();
	light.enable();
	material.begin();
	ofPushMatrix();
	ofTranslate(0, -80);
	ofRotateXDeg(5);
	ofSetColor(255);
	surface.draw();
	ofPopMatrix();
	material.end();
	light.disable();
	camera.end();
	ofDisableDepthTest();
	ofSetColor(255);
	playback.drawStatus();
	runtime.finishFrame(playback.ready, "marching-cubes", surface.getNumVertices());
}

void ofApp::keyPressed(int key) {
	if (key == 'f') ofToggleFullscreen();
}
