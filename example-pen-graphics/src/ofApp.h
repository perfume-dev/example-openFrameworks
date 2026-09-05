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
	struct RibbonTrail {
		using Frame = std::vector<glm::vec3>;
		ofxBvh* motion = nullptr;
		std::deque<Frame> history;
		std::vector<ofVboMesh> surfaces;
		std::vector<ofPolyline> outlines;
		ofVboMesh skeleton;
		void update(const glm::vec3& noiseOffset);
		void draw();
	};
	example::Runtime runtime;
	example::MotionPlayback playback;
	std::array<RibbonTrail, 3> trails;
	ofCamera camera;
	ofVboMesh ground;
	glm::vec3 center{0};
	glm::vec3 cameraPosition{0, 60, -420};
	glm::vec3 cameraTarget{0, 60, -420};
	glm::vec3 noiseOffset{0};
	glm::vec3 noiseVelocity{0};
};
