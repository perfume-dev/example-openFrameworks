#pragma once

#include "ofMain.h"

#include "ofxBvh.h"
#include "ofxMarchingCubes.h"
#include "MetaBall.h"

#include <array>

class testApp : public ofBaseApp {

  public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;

	void keyPressed(int key) override;
	void keyReleased(int key) override;
	void mouseMoved(int x, int y) override;
	void mouseDragged(int x, int y, int button) override;
	void mousePressed(int x, int y, int button) override;
	void mouseReleased(int x, int y, int button) override;
	void windowResized(int w, int h) override;
	void dragEvent(ofDragInfo dragInfo) override;
	void gotMessage(ofMessage msg) override;
		
	ofSoundPlayer track;
	std::array<ofxBvh, 3> bvh;
	
	float rotate = 0.0f;
	float play_rate = 1.0f;
	float play_rate_t = 1.0f;
	
	ofEasyCam camera;
	ofImage background;
	
	ofxMarchingCubes marchingCubes;
	vector<MetaBall> metaBalls;

	float threshold = 0.17f;
	ofLight light;
	bool assetsReady = false;
	bool audioReady = false;
};
