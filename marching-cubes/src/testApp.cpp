#include "testApp.h"

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
			ofLogWarning("marching-cubes")
				<< "Using bundled motion fallback for " << kPerfumeMotionFiles[i];
			loaded = motions[i].load(kBundledMotionFiles[i]) && loaded;
		}
	}
	return loaded;
}

} // namespace

float startTime = 0.02;

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
		ofLogNotice("marching-cubes")
			<< "Audio file not found; using a silent internal clock.";
	}
	
	camera.setFov(30);
	camera.setDistance(700);
	
	// MarchingCube init
	ofPoint iniPos(0,0,0);
	ofPoint gridSize(550, 550, 550);
	int gridResX = 60;
	int gridResY = 60;
	int gridResZ = 60;
	marchingCubes.init(iniPos, gridSize, gridResX, gridResY, gridResZ);	
	
	// Metaball init
	int metaballNum = bvh[0].getNumJoints() + bvh[1].getNumJoints() + bvh[2].getNumJoints();
	metaBalls.resize(metaballNum);
	
	int n = 0;
	for (size_t i = 0; i < bvh.size(); ++i){
		for (int j = 0; j < bvh[i].getNumJoints(); j++) {
			const ofxBvhJoint *o = bvh[i].getJoint(j);
			if (o->isSite()) {
				metaBalls[n].init(o->getPosition());
				metaBalls[n].size = 1.4;
			}
			n++;
		}
	}
	
	light.enable();
	light.setAmbientColor(ofFloatColor(0.1, 0.3, 0.8, 1.0));
	light.setDiffuseColor(ofFloatColor(0.7, 0.7, 0.7));
	light.setSpecularColor(ofFloatColor(1.0, 0.5, 0.0));
}

//--------------------------------------------------------------
void testApp::update(){
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

	marchingCubes.resetIsoValues();
	
	int n = 0;
	for (size_t i = 0; i < bvh.size(); ++i){
		for (int j = 0; j < bvh[i].getNumJoints(); j++) {
			const ofxBvhJoint *o = bvh[i].getJoint(j);
			if (o->isSite()) {
				if (t > startTime) {
					metaBalls[n].goTo(o->getPosition(), 0.3, 0.94);
					marchingCubes.addMetaBall(metaBalls[n], metaBalls[n].size);
				} else {
					metaBalls[n].goTo(o->getPosition(), 1.0, 0.1);
				}
			}
			n++;
		}
	}
	
	marchingCubes.update(threshold, true);
}

//--------------------------------------------------------------
void testApp::draw(){
	ofBackgroundHex(0x222222);
	
	camera.begin();
	ofPushMatrix();
	{
		ofTranslate(0, -80);
		ofRotateDeg(5, 1, 0, 0);
		ofScale(1, 1, 1);
		
		// draw MarchingCubes
		vector<ofPoint>& vertices = marchingCubes.getVertices();
		vector<ofPoint>& normals = marchingCubes.getNormals();
		int numVertices = vertices.size();
		
		glEnable(GL_DEPTH_TEST);
		glColor3f(1.0f, 1.0f, 1.0f);
		glBegin(GL_TRIANGLES);
		
		for(int i=0; i<numVertices; i++){
			glNormal3f(normals[i].x, normals[i].y, normals[i].z);
			glVertex3f(vertices[i].x, vertices[i].y, vertices[i].z);
		}
		
		glEnd();
		glDisable(GL_DEPTH_TEST);
		
	}
	
	ofPopMatrix();
	camera.end();
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
