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

private:
	struct Trail {
		const ofxBvhJoint* joint = nullptr;
		std::deque<glm::vec3> points;
	};
	example::Runtime runtime;
	example::MotionPlayback playback;
	std::vector<Trail> trails;
	ofEasyCam camera;
	ofImage background;
	ofVboMesh ribbons;
	float rotation = 0.0f;
};
