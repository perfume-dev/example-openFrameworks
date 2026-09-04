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
			ofLogWarning("example-pen-graphics")
				<< "Using bundled motion fallback for " << kPerfumeMotionFiles[i];
			loaded = motions[i].load(kBundledMotionFiles[i]) && loaded;
		}
	}
	return loaded;
}

} // namespace

ofVec3f center, center_t;
ofVec3f campos, campos_t;
ofVec3f offset, offset_v;

class Tracker
{
public:
	
	ofxBvh *bvh = nullptr;
	
	typedef vector<ofVec3f> Frame;
	deque<Frame> track;
	
	struct Buffer
	{
		ofVec3f v1, v2;
		ofVec3f norm;
	};
	
	typedef vector<Buffer> BufferArray;
	vector<BufferArray> buffer;
	
	void setup(ofxBvh *o)
	{
		bvh = o;
	}
	
	void update()
	{
		if (bvh->isFrameNew())
		{
			// update vertexes flow
			
			for (size_t i = 0; i < track.size(); ++i)
			{
				float delta = ofMap(static_cast<float>(i), 0.0f,
					static_cast<float>(track.size()), 0.0f, 1.0f);
				Frame &f = track[i];
				
				for (auto& v : f)
				{
					ofVec3f force{};
					
					// gravity
					force.y -= 2.5 * (1 - sin(pow(delta, 2) * PI));
					force.y += ofNoise(v.y * 0.0001 + offset.y) * 1.4;
					
					force.x += ofSignedNoise(v.x * 0.0001 + offset.x) * 3;
					force.z += ofSignedNoise(v.z * 0.0001 + offset.z) * 3;
					
					if (v.y < 0)
					{
						force.y *= 0.02;
						force.x *= 5;
						force.z *= 5;
					}
					
					v += force;
				}
			}
			
			Frame f;
			for (int i = 0; i < bvh->getNumJoints(); ++i)
			{
				const ofxBvhJoint *j = bvh->getJoint(i);
				for (size_t n = 0; n < j->getChildren().size(); ++n)
				{
					f.push_back(j->getPosition());
					f.push_back(j->getChildren().at(n)->getPosition());
				}
			}
			
			track.push_front(f);
			
			if (track.size() > 200)
				track.pop_back();
			
			
			// cache vertexes
			
			buffer.clear();
			for (size_t n = 0; n + 1 < track.front().size(); n += 2)
			{
				ofVec3f norm;
				
				BufferArray arr;
				
				for (size_t i = 0; i + 1 < track.size(); ++i)
				{
					float delta = ofMap(static_cast<float>(i), 0.0f,
						static_cast<float>(track.size()), 0.1f, 1.0f);
					Frame &f1 = track[i];
					
					const ofVec3f &v1 = f1[n];
					const ofVec3f &v2 = f1[n + 1];
					const ofVec3f d = v1 - v2;
					
					const ofVec3f c1 = d.getCrossed(ofVec3f(0, 1, 0)).getNormalized();
					const ofVec3f c = c1.getCrossed(d).getNormalized();
					// if (c.y < 0) c *= -1;
					
					if (i == 0) norm.set(c);
					
					ofVec3f m = v1 * delta + v2 * (1 - delta);
					norm += (c - norm) * 0.3;
					
					Buffer buf;
					
					buf.norm = norm;
					buf.v1 = v1;
					buf.v2 = m;
					
					arr.push_back(buf);
				}
				
				buffer.push_back(arr);
			}

		}
	}
	
	void draw()
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1, 1);

		// draw polygons
		
		ofSetColor(255);
		
		for (size_t i = 0; i < buffer.size(); ++i)
		{
			const BufferArray &arr = buffer[i];
			
			glBegin(GL_TRIANGLE_STRIP);
			for (const auto& b : arr)
			{
				glNormal3fv(b.norm.getPtr());
				glVertex3fv(b.v1.getPtr());
				glVertex3fv(b.v2.getPtr());
			}
			glEnd();
		}

		// draw outline
		
		ofSetColor(0);
		
		for (size_t i = 0; i < buffer.size(); ++i)
		{
			const BufferArray &arr = buffer[i];
			
			glBegin(GL_LINE_STRIP);
			for (const auto& b : arr)
			{
				glNormal3fv(b.norm.getPtr());
				glVertex3fv(b.v1.getPtr());
			}
			glEnd();
		}
		
		for (size_t i = 0; i < buffer.size(); ++i)
		{
			const BufferArray &arr = buffer[i];
			
			glBegin(GL_LINE_STRIP);
			for (const auto& b : arr)
			{
				glNormal3fv(b.norm.getPtr());
				glVertex3fv(b.v2.getPtr());
			}
			glEnd();
		}

		if (!track.empty())
		{
			Frame &f = track[0];
			
			glBegin(GL_LINES);
			for (size_t n = 0; n + 1 < f.size(); n += 2)
			{
				ofVec3f &v1 = f[n];
				ofVec3f &v2 = f[n + 1];
				
				glVertex3fv(v1.getPtr());
				glVertex3fv(v2.getPtr());
			}
			glEnd();
		}

		glDisable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(0, 0);
	}
	
};

vector<std::unique_ptr<Tracker>> trackers;

//--------------------------------------------------------------
void testApp::setup()
{
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofSetSmoothLighting(true);
	ofSetGlobalAmbientColor(ofColor(220));

	ofBackground(255);
	
	assetsReady = loadMotionFiles(bvh);
	
	for (auto& motion : bvh)
	{
		motion.setFrame(4);
	}
	
	audioReady = ofFile::doesFileExist("Perfume_globalsite_sound.wav")
		&& track.load("Perfume_globalsite_sound.wav");
	if (audioReady) {
		track.setLoop(true);
		track.play();
	} else {
		ofLogNotice("example-pen-graphics")
			<< "Audio file not found; using a silent internal clock.";
	}
	
	// setup tracker
	if (assetsReady)
	{
		for (auto& motion : bvh)
		{
			auto tracker = std::make_unique<Tracker>();
			tracker->setup(&motion);
			trackers.push_back(std::move(tracker));
		}
	}
	
	offset.x = ofRandom(1);
	offset.y = ofRandom(1);
	offset.z = ofRandom(1);
	offset_v.x = ofRandom(0.001);
	offset_v.y = ofRandom(0.005);
	offset_v.z = ofRandom(0.001);
	
	campos_t.set(0, 0, -300);
}

//--------------------------------------------------------------
void testApp::update()
{
	if (!assetsReady) return;
	const float motionDuration = bvh[0].getDuration();
	const float motionSeconds = audioReady
		? track.getPosition() * track.getDuration()
		: std::fmod(ofGetElapsedTimef(), motionDuration);
	const float t = ofClamp(motionSeconds / motionDuration, 0.0f, 1.0f);
	
	center_t.set(0, 0, 0);
	
	for (auto& motion : bvh)
	{
		motion.setPosition(t);
		motion.update();
		
		center_t += motion.getJoint(0)->getPosition();
	}
	
	center_t /= 3;
	center += (center_t - center) * 0.01;
	
	for (auto& tracker : trackers)
	{
		tracker->update();
	}
	
	offset += offset_v;
	
	cam.setPosition(campos.x, campos.y, campos.z);
	cam.lookAt(ofVec3f(0, 0, 0));
	campos += (campos_t - campos) * 0.01;
}

//--------------------------------------------------------------
void testApp::draw(){
	glEnable(GL_DEPTH_TEST);
	glShadeModel(GL_SMOOTH);
	
	ofEnableSmoothing();
	
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glLineWidth(1);
	
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	
	light.enable();
	light.setPosition(0, -500, 0);
	
	cam.begin();
	
	ofPushMatrix();
	{
		glRotatef(ofGetElapsedTimef() * 20, 0, 1, 0);
		glTranslatef(-center.x, -100, -center.z);
		
		ofSetColor(200);
		
		for (int x = -10; x < 10; x++)
		{
			for (int y = -10; y < 10; y++)
			{
				ofPushMatrix();
				glTranslatef(x * 500, 0, y * 500);
				ofDrawLine(10, 0, 0, -10, 0, 0);
				ofDrawLine(0, 0, 10, 0, 0, -10);
				ofPopMatrix();
			}
		}
		
		ofSetColor(ofColor::white, 80);
		for (auto& tracker : trackers)
		{
			tracker->draw();
		}
	}
	ofPopMatrix();
	
	cam.end();
	
	light.disable();
	ofDisableDepthTest();
	ofSetColor(20);

	if (!assetsReady)
		ofDrawBitmapString("Motion data is missing. See README.md.", 10, 20);
	else if (!audioReady)
		ofDrawBitmapString("Audio data is missing; running with a silent clock.", 10, 20);
	
}

//--------------------------------------------------------------
void testApp::keyPressed(int key){
	campos_t.x = ofRandom(-600, 600);
	campos_t.z = ofRandom(-600, 600);
	campos_t.y = ofRandom(-100, 200);
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
