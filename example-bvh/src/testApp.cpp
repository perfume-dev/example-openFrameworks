#include "testApp.h"

//--------------------------------------------------------------
void testApp::setup(){
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofBackground(0);
	
	// setup bvh
	assetsReady = bvh[0].load("A_test.bvh");
	assetsReady = bvh[1].load("B_test.bvh") && assetsReady;
	assetsReady = bvh[2].load("C_test.bvh") && assetsReady;
	
	for (auto& motion : bvh)
	{
		motion.play();
		motion.setLoop(true);
	}
}

//--------------------------------------------------------------
void testApp::update()
{
	for (auto& motion : bvh)
	{
		motion.update();
	}
}

//--------------------------------------------------------------
void testApp::draw(){
	ofEnableDepthTest();
	
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);

	cam.begin();
	
	for (auto& motion : bvh)
	{
		motion.draw();
	}
	
	cam.end();

	ofDisableDepthTest();
	if (!assetsReady)
	{
		ofSetColor(255);
		ofDrawBitmapString("Unable to load the bundled BVH files.", 20, 30);
	}

}

//--------------------------------------------------------------
void testApp::keyPressed(int key){
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
