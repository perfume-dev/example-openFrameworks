#include "testApp.h"

class Tracker
{
public:
	
	const ofxBvhJoint *joint;
	deque<ofVec3f> points;
	
	void setup(const ofxBvhJoint *o)
	{
		joint = o;
	}
	
	void update()
	{
		if (joint->getBvh()->isFrameNew())
		{
			const ofVec3f &p = joint->getPosition();
			
			points.push_front(joint->getPosition());
			
			if (points.size() > 15)
				points.pop_back();
		}
	}
	
	void draw()
	{
		if (points.empty()) return;
		
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < points.size() - 1; i++)
		{
			float a = ofMap(i, 0, points.size() - 1, 1, 0, true);
			
			ofVec3f &p0 = points[i];
			ofVec3f &p1 = points[i + 1];
			
			float d = p0.distance(p1);
			a *= ofMap(d, 3, 5, 0, 1, true);
			
			glColor4f(1, 1, 1, a);
			glVertex3fv(points[i].getPtr());
		}
		glEnd();
	}
};

vector<Tracker*> trackers;
const float trackDuration = 64.28;

//--------------------------------------------------------------
void testApp::setup()
{
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofBackground(0);
	
	play_rate = play_rate_t = 1;
	rotate = 0;

	bvh.resize(3);
	
	// You have to get motion and sound data from http://www.perfume-global.com
	
	// setup bvh
	bvh[0].load("bvhfiles/aachan.bvh");
	bvh[1].load("bvhfiles/kashiyuka.bvh");
	bvh[2].load("bvhfiles/nocchi.bvh");
	
	for (int i = 0; i < bvh.size(); i++)
	{
		bvh[i].setFrame(1);
	}
	
	track.loadSound("Perfume_globalsite_sound.wav");
	track.play();
	track.setLoop(true);
	
	// setup tracker
	for (int i = 0; i < bvh.size(); i++)
	{
		ofxBvh &b = bvh[i];
		
		for (int n = 0; n < b.getNumJoints(); n++)
		{
			const ofxBvhJoint *o = b.getJoint(n);
			Tracker *t = new Tracker;
			t->setup(o);
			trackers.push_back(t);
		}
	}
}

//--------------------------------------------------------------
void testApp::update()
{
	rotate += 0.1;
	
	play_rate += (play_rate_t - play_rate) * 0.3;
	track.setSpeed(play_rate);
	
	float t = (track.getPosition() * trackDuration);
	t = t / bvh[0].getDuration();
	
	for (int i = 0; i < bvh.size(); i++)
	{
		bvh[i].setPosition(t);
		bvh[i].update();
	}
	
	for (int i = 0; i < trackers.size(); i++)
	{
		trackers[i]->update();
	}
}

//--------------------------------------------------------------
void testApp::draw(){
	glEnable(GL_DEPTH_TEST);
	
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	
	ofPushMatrix();
	{
		ofTranslate(ofGetWidth()/2, ofGetHeight()/2);
		ofTranslate(0, 150);
		
		ofRotate(-15, 1, 0, 0);
		ofRotate(rotate, 0, 1, 0);
		
		ofScale(1, -1, 1);
		
		ofSetColor(ofColor::white);
		
		ofFill();
		
		// draw ground
		ofPushMatrix();
		ofRotate(90, 1, 0, 0);
		ofLine(100, 0, -100, 0);
		ofLine(0, 100, 0, -100);
		ofPopMatrix();
		
		// draw actor
		for (int i = 0; i < bvh.size(); i++)
		{
			bvh[i].draw();
		}

		// draw tracker
		glDisable(GL_DEPTH_TEST);
		ofEnableBlendMode(OF_BLENDMODE_ADD);
		
		ofSetColor(ofColor::white, 80);
		for (int i = 0; i < trackers.size(); i++)
		{
			trackers[i]->draw();
		}
	}
	ofPopMatrix();
	
	ofSetColor(255);
	ofDrawBitmapString("press any key to scratch\nplay_rate: " + ofToString(play_rate, 1), 10, 20);
}

//--------------------------------------------------------------
void testApp::keyPressed(int key){
	play_rate_t = -1;
}

//--------------------------------------------------------------
void testApp::keyReleased(int key){
	play_rate_t = 1;
}

//--------------------------------------------------------------
void testApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void testApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void testApp::mouseReleased(int x, int y, int button){
}

//--------------------------------------------------------------
void testApp::windowResized(int w, int h){
}

//--------------------------------------------------------------
void testApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void testApp::dragEvent(ofDragInfo dragInfo){ 
}
