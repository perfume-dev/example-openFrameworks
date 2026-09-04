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
			ofLogWarning("example-sync-sound")
				<< "Using bundled motion fallback for " << kPerfumeMotionFiles[i];
			loaded = motions[i].load(kBundledMotionFiles[i]) && loaded;
		}
	}
	return loaded;
}

} // namespace

class Tracker
{
public:
	
	const ofxBvhJoint *joint = nullptr;
	deque<ofVec3f> points;
	
	void setup(const ofxBvhJoint *o)
	{
		joint = o;
	}
	
	void update()
	{
		if (joint->getBvh()->isFrameNew())
		{
			points.push_front(joint->getPosition());
			
			if (points.size() > 15)
				points.pop_back();
		}
	}
	
	void draw()
	{
		if (points.empty()) return;
		
		glBegin(GL_LINE_STRIP);
		for (size_t i = 0; i + 1 < points.size(); ++i)
		{
			float a = ofMap(static_cast<float>(i), 0.0f,
				static_cast<float>(points.size() - 1), 1.0f, 0.0f, true);
			
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

vector<std::unique_ptr<Tracker>> trackers;

//--------------------------------------------------------------
void testApp::setup()
{
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofBackground(0);
	
	play_rate = play_rate_t = 1.0f;
	rotate = 0.0f;
	
	assetsReady = loadMotionFiles(bvh);
	
	for (auto& motion : bvh)
	{
		motion.setFrame(1);
	}
	
	audioReady = ofFile::doesFileExist("Perfume_globalsite_sound.wav")
		&& track.load("Perfume_globalsite_sound.wav");
	if (audioReady) {
		track.setLoop(true);
		track.play();
	} else {
		ofLogNotice("example-sync-sound")
			<< "Audio file not found; using a silent internal clock.";
	}
	
	// setup tracker
	if (assetsReady)
	{
		for (auto& motion : bvh)
		{
			for (int n = 0; n < motion.getNumJoints(); ++n)
			{
				auto tracker = std::make_unique<Tracker>();
				tracker->setup(motion.getJoint(n));
				trackers.push_back(std::move(tracker));
			}
		}
	}
}

//--------------------------------------------------------------
void testApp::update()
{
	rotate += 0.1;
	
	play_rate += (play_rate_t - play_rate) * 0.3;
	if (audioReady) track.setSpeed(play_rate);
	if (!assetsReady) return;

	const float motionDuration = bvh[0].getDuration();
	float motionSeconds = 0.0f;
	if (audioReady) {
		motionSeconds = track.getPosition() * track.getDuration();
	} else {
		fallbackPlayhead += ofGetLastFrameTime() * play_rate;
		fallbackPlayhead = std::fmod(fallbackPlayhead, motionDuration);
		if (fallbackPlayhead < 0.0f) fallbackPlayhead += motionDuration;
		motionSeconds = fallbackPlayhead;
	}
	const float t = ofClamp(motionSeconds / motionDuration, 0.0f, 1.0f);
	
	for (auto& motion : bvh)
	{
		motion.setPosition(t);
		motion.update();
	}
	
	for (auto& tracker : trackers)
	{
		tracker->update();
	}
}

//--------------------------------------------------------------
void testApp::draw(){
	ofEnableDepthTest();
	
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	
	ofPushMatrix();
	{
		ofTranslate(ofGetWidth()/2, ofGetHeight()/2);
		ofTranslate(0, 150);
		
		ofRotateDeg(-15, 1, 0, 0);
		ofRotateDeg(rotate, 0, 1, 0);
		
		ofScale(1, -1, 1);
		
		ofSetColor(ofColor::white);
		
		ofFill();
		
		// draw ground
		ofPushMatrix();
		ofRotateDeg(90, 1, 0, 0);
		ofDrawLine(100, 0, -100, 0);
		ofDrawLine(0, 100, 0, -100);
		ofPopMatrix();
		
		// draw actor
		for (auto& motion : bvh)
		{
			motion.draw();
		}

		// draw tracker
		ofDisableDepthTest();
		ofEnableBlendMode(OF_BLENDMODE_ADD);
		
		ofSetColor(ofColor::white, 80);
		for (auto& tracker : trackers)
		{
			tracker->draw();
		}
	}
	ofPopMatrix();
	
	ofSetColor(255);
	ofDrawBitmapString("press any key to scratch\nplay_rate: " + ofToString(play_rate, 1), 10, 20);
	if (!assetsReady)
		ofDrawBitmapString("Motion data is missing. See README.md.", 10, 65);
	else if (!audioReady)
		ofDrawBitmapString("Audio data is missing; running with a silent clock.", 10, 65);
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
