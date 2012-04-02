#pragma once

#include "ofMain.h"
#include "ofxBvh.h"

class testApp : public ofBaseApp{

  public:
	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed  (int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y );
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);
		
	ofSoundPlayer track;
	ofxBvh bvh[3];
	
	float rotate;
	float play_rate, play_rate_t;
	
	ofEasyCam camera;
	ofImage background;
};
