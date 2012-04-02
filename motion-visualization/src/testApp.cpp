#include "testApp.h"

int trackerLength = 200;
float startTime = 0.035;

class Tracker
{
public:
	
	const ofxBvhJoint *joint;
	deque<ofVec3f> points;
	float trackerLength;
	
	void setup(const ofxBvhJoint *o){
		joint = o;
	}
	
	void update() {
		const ofVec3f &p = joint->getPosition();
		
		if (p.distance(points.front()) > 1)
			points.push_front(joint->getPosition());
		
		if (points.size() > trackerLength){
			points.pop_back();
		}
	}
	
	void draw()	{
		if (points.empty()) return;
		
		for (int i = 0; i < points.size() - 1; i++){
			float a = ofMap(i, 0, points.size() - 1, 1, 0);
				
			ofVec3f &p0 = points[i];
			ofVec3f &p1 = points[i + 1];
			
			float dist = p0.distance(p1);
			
			if (dist < 40) {
				ofSetLineWidth(ofMap(dist, 0, 30, 0, 14));
				ofSetColor(dist*20, 127-dist*10, 255-dist*20);
				ofLine(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
			}
		}		
	}
	 
	void clear() {
		points.clear();
	}
	
	void setTrackerLength(float _trackerLength) {
		trackerLength = _trackerLength;
	}
};

vector<Tracker*> trackers;

//--------------------------------------------------------------
void testApp::setup() {
	ofSetFrameRate(60);
	ofSetVerticalSync(true);	
	
	rotate = 0;
	
	// setup bvh
	bvh[0].load("bvhfiles/kashiyuka.bvh");
	bvh[1].load("bvhfiles/nocchi.bvh");
	bvh[2].load("bvhfiles/aachan.bvh");
	
	for (int i = 0; i < 3; i++)	{
		bvh[i].play();
	}
	
	track.loadSound("Perfume_globalsite_sound.wav");
	track.play();
	track.setLoop(true);
	
	// setup tracker
	for (int i = 0; i < 3; i++)
	{
		ofxBvh &b = bvh[i];
		
		for (int n = 0; n < b.getNumJoints(); n++) {
			const ofxBvhJoint *o = b.getJoint(n);
			Tracker *t = new Tracker;
			t->setup(o);
			t->setTrackerLength(trackerLength);
			trackers.push_back(t);
		}
	}
	
	camera.setFov(45);
	camera.setDistance(360);
	camera.disableMouseInput();
	
	background.loadImage("background.png");
	
}

//--------------------------------------------------------------
void testApp::update()
{
	rotate += 0.04;
	
	float t = (track.getPosition() * 64.28);
	t = t / bvh[0].getDuration();
	
	for (int i = 0; i < 3; i++)	{
		bvh[i].setPosition(t);
		bvh[i].update();
	}
	
	for (int i = 0; i < trackers.size(); i++) {
		if (t > startTime) {
			trackers[i]->setTrackerLength(trackerLength);
			trackers[i]->update();
		}
	}
}

//--------------------------------------------------------------
void testApp::draw(){
	ofBackgroundHex(0x000000);
	ofSetHexColor(0xffffff);
	background.draw(0,0,ofGetWidth(),ofGetHeight());
	
	glEnable(GL_DEPTH_TEST);
	
	camera.begin();
	ofPushMatrix();
	{
		ofTranslate(0, -80);
		ofRotate(rotate, 0, 1, 0);
		ofScale(1, 1, 1);

		// draw tracker
		glDisable(GL_DEPTH_TEST);
		ofEnableBlendMode(OF_BLENDMODE_ADD);
		
		//ofSetColor(ofColor::white, 80);
		for (int i = 0; i < trackers.size(); i++){
			trackers[i]->draw();
		}

	}
	ofPopMatrix();
	camera.end();

}

void testApp::exit(){

}

//--------------------------------------------------------------
void testApp::keyPressed(int key){
	if (key == 'f') {
		ofToggleFullscreen();
	}
}

//--------------------------------------------------------------
void testApp::keyReleased(int key){

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
