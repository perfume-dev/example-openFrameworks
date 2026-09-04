#pragma once

#include "../../shared/ExampleRuntime.h"
#include "ofxMarchingCubes.h"
#include "MetaBall.h"

class ofApp : public ofBaseApp {
public:
	explicit ofApp(const example::RunOptions& options) : runtime(options) {}
	void setup() override;
	void update() override;
	void draw() override;
	void keyPressed(int key) override;

private:
	struct TrackedBall {
		const ofxBvhJoint* joint = nullptr;
		MetaBall ball;
	};
	example::Runtime runtime;
	example::MotionPlayback playback;
	ofEasyCam camera;
	ofxMarchingCubes marchingCubes;
	std::vector<TrackedBall> balls;
	ofVboMesh surface;
	ofLight light;
	ofMaterial material;
};
