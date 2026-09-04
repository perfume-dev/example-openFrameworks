#pragma once

#include "../../shared/ExampleRuntime.h"
#include <deque>

class ofApp : public ofBaseApp {
public:
	explicit ofApp(const example::RunOptions& options) : runtime(options) {}
	void setup() override;
	void update() override;
	void draw() override;
	void keyPressed(int key) override;
	void keyReleased(int key) override;

private:
	struct Trail {
		const ofxBvhJoint* joint = nullptr;
		std::deque<glm::vec3> points;
		ofVboMesh mesh;
		void update();
	};
	example::Runtime runtime;
	example::MotionPlayback playback;
	std::vector<Trail> trails;
	float rotation = 0.0f;
	float playbackSpeed = 1.0f;
	float targetSpeed = 1.0f;
};
