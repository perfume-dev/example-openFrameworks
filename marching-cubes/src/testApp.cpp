#include "testApp.h"

float startTime = 0.02;

//--------------------------------------------------------------
void testApp::setup() {
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	rotate = 0;
	
	bvh[0].load("bvhfiles/kashiyuka.bvh");
	bvh[1].load("bvhfiles/nocchi.bvh");
	bvh[2].load("bvhfiles/aachan.bvh");
	
	for (int i = 0; i < 3; i++)	{
		bvh[i].play();
	}
	
	track.loadSound("Perfume_globalsite_sound.wav");
	track.play();
	track.setLoop(true);
	
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
	for (int i = 0; i < 3; i++){
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
	float t = (track.getPosition() * 64.28);
	t = t / bvh[0].getDuration();
	
	for (int i = 0; i < 3; i++)	{
		bvh[i].setPosition(t);
		bvh[i].update();
	}
	
	marchingCubes.resetIsoValues();
	
	int n = 0;
	for (int i = 0; i < 3; i++){
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
	
	marchingCubes.update(0.17, true);
}

//--------------------------------------------------------------
void testApp::draw(){
	ofBackgroundHex(0x222222);
	
	camera.begin();
	ofPushMatrix();
	{
		ofTranslate(0, -80);
		ofRotate(5, 1, 0, 0);
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
