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
	struct Particle {
		glm::vec3 position{0};
		glm::vec3 velocity{0};
		glm::vec3 force{0};
	};
	struct Trail {
		const ofxBvhJoint* joint = nullptr;
		std::deque<glm::vec3> samples;
		ofVboMesh mesh;
		void update();
	};
	struct ParticleShape {
		ofxBvh* motion = nullptr;
		std::vector<Trail> trails;
		std::vector<Particle> particles;
		ofVboMesh points;
		size_t nextParticle = 0;
		void setup(ofxBvh& motion);
		void update(float deltaSeconds);
	};
	example::Runtime runtime;
	example::MotionPlayback playback;
	std::array<ParticleShape, 3> shapes;
	ofEasyCam camera;
	ofShader pointShader;
	glm::vec3 center{0};
	bool paused = false;
	bool shaderReady = false;
};
