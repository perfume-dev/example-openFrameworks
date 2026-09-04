#pragma once

#include "ofMain.h"
#include "ofxBvh.h"

#include <array>

class testApp : public ofBaseApp {

  public:
	void setup() override;
	void update() override;
	void draw() override;

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
	
	ofCamera cam;
	ofLight light;
	bool assetsReady = false;
	bool audioReady = false;
};
