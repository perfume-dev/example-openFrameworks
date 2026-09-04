#include "testApp.h"

#include <memory>

namespace {

const std::array<const char*, 3> kPerfumeMotionFiles = {
	"bvhfiles/aachan.bvh",
	"bvhfiles/kashiyuka.bvh",
	"bvhfiles/nocchi.bvh"
};

const std::array<const char*, 3> kBundledMotionFiles = {
	"../../../example-bvh/bin/data/A_test.bvh",
	"../../../example-bvh/bin/data/B_test.bvh",
	"../../../example-bvh/bin/data/C_test.bvh"
};

bool loadMotionFiles(std::array<ofxBvh, 3>& motions) {
	bool loaded = true;
	for (size_t i = 0; i < motions.size(); ++i) {
		if (!motions[i].load(kPerfumeMotionFiles[i])) {
			ofLogWarning("motion-visualization")
				<< "Using bundled motion fallback for " << kPerfumeMotionFiles[i];
			loaded = motions[i].load(kBundledMotionFiles[i]) && loaded;
		}
	}
	return loaded;
}

} // namespace

int trackerLength = 200;
float startTime = 0.035;

class Tracker
{
public:
	
	const ofxBvhJoint *joint = nullptr;
	deque<ofVec3f> points;
	float trackerLength = 0.0f;
	
	void setup(const ofxBvhJoint *o){
		joint = o;
	}
	
	void update() {
		const ofVec3f &p = joint->getPosition();
		
		if (points.empty() || p.distance(points.front()) > 1)
			points.push_front(joint->getPosition());
		
		if (points.size() > trackerLength){
			points.pop_back();
		}
	}
	
	void draw()	{
		if (points.empty()) return;

		for (size_t i = 0; i + 1 < points.size(); ++i){
			ofVec3f &p0 = points[i];
			ofVec3f &p1 = points[i + 1];
			
			float dist = p0.distance(p1);
			
			if (dist < 40) {
				ofSetLineWidth(ofMap(dist, 0, 30, 0, 14));
				ofSetColor(dist*20, 127-dist*10, 255-dist*20);
				ofDrawLine(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z);
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

vector<std::unique_ptr<Tracker>> trackers;

//--------------------------------------------------------------
void testApp::setup() {
	ofSetFrameRate(60);
	ofSetVerticalSync(true);	
	
	rotate = 0;
	
	assetsReady = loadMotionFiles(bvh);
	
	audioReady = ofFile::doesFileExist("Perfume_globalsite_sound.wav")
		&& track.load("Perfume_globalsite_sound.wav");
	if (audioReady) {
		track.setLoop(true);
		track.play();
	} else {
		ofLogNotice("motion-visualization")
			<< "Audio file not found; using a silent internal clock.";
	}
	
	// setup tracker
	if (assetsReady)
	{
		for (auto& motion : bvh) {
			for (int n = 0; n < motion.getNumJoints(); ++n) {
				auto tracker = std::make_unique<Tracker>();
				tracker->setup(motion.getJoint(n));
				tracker->setTrackerLength(trackerLength);
				trackers.push_back(std::move(tracker));
			}
		}
	}
	
	camera.setFov(45);
	camera.setDistance(360);
	camera.disableMouseInput();
	
	background.load("background.png");
	
}

//--------------------------------------------------------------
void testApp::update()
{
	rotate += 0.04;
	
	if (!assetsReady) return;
	const float motionDuration = bvh[0].getDuration();
	const float motionSeconds = audioReady
		? track.getPosition() * track.getDuration()
		: std::fmod(ofGetElapsedTimef(), motionDuration);
	const float t = ofClamp(motionSeconds / motionDuration, 0.0f, 1.0f);
	
	for (auto& motion : bvh) {
		motion.setPosition(t);
		motion.update();
	}
	
	for (auto& tracker : trackers) {
		if (t > startTime) {
			tracker->setTrackerLength(trackerLength);
			tracker->update();
		}
	}
}

//--------------------------------------------------------------
void testApp::draw(){
	ofBackgroundHex(0x000000);
	ofSetHexColor(0xffffff);
	background.draw(0,0,ofGetWidth(),ofGetHeight());
	
	ofEnableDepthTest();
	
	camera.begin();
	ofPushMatrix();
	{
		ofTranslate(0, -80);
		ofRotateDeg(rotate, 0, 1, 0);
		ofScale(1, 1, 1);

		// draw tracker
		ofDisableDepthTest();
		ofEnableBlendMode(OF_BLENDMODE_ADD);
		
		//ofSetColor(ofColor::white, 80);
		for (auto& tracker : trackers){
			tracker->draw();
		}

	}
	ofPopMatrix();
	camera.end();
	ofDisableDepthTest();
	ofSetColor(255);

	if (!assetsReady)
		ofDrawBitmapString("Motion data is missing. See README.md.", 10, 20);
	else if (!audioReady)
		ofDrawBitmapString("Audio data is missing; running with a silent clock.", 10, 20);

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
