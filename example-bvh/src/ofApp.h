#pragma once

#include "../../shared/ExampleRuntime.h"

class ofApp : public ofBaseApp {
public:
	explicit ofApp(const example::RunOptions& options) : runtime(options) {}
	void setup() override;
	void update() override;
	void draw() override;

private:
	example::Runtime runtime;
	example::MotionPlayback playback;
	ofEasyCam camera;
};
