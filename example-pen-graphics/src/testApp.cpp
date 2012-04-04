#include "testApp.h"

const float trackDuration = 64.28;
ofVec3f center, center_t;
ofVec3f campos, campos_t;
ofVec3f offset, offset_v;

class Tracker
{
public:
	
	ofxBvh *bvh;
	
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
			
			for (int i = 0; i < track.size(); i++)
			{
				float delta = ofMap(i, 0, track.size(), 0, 1);
				Frame &f = track[i];
				
				for (int n = 0; n < f.size(); n++)
				{
					ofVec3f &v = f[n];
					ofVec3f f = 0;
					
					// gravity
					f.y -= 2.5 * (1 - sin(pow(delta, 2) * PI));
					f.y += ofNoise(v.y * 0.0001 + offset.y) * 1.4;
					
					f.x += ofSignedNoise(v.x * 0.0001 + offset.x) * 3;
					f.z += ofSignedNoise(v.z * 0.0001 + offset.z) * 3;
					
					if (v.y < 0)
					{
						f.y *= 0.02;
						f.x *= 5;
						f.y *= 5;
					}
					
					v += f;
				}
			}
			
			Frame f;
			for (int i = 0; i < bvh->getNumJoints(); i++)
			{
				const ofxBvhJoint *j = bvh->getJoint(i);
				for (int n = 0; n < j->getChildren().size(); n++)
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
			for (int n = 0; n < 52; n += 2)
			{
				ofVec3f norm;
				
				BufferArray arr;
				
				for (int i = 0; i < track.size() - 1; i++)
				{
					float delta = ofMap(i, 0, track.size(), 0.1, 1);
					Frame &f1 = track[i];
					
					const ofVec3f &v1 = f1[n];
					const ofVec3f &v2 = f1[n + 1];
					const ofVec3f d = v1 - v2;
					
					const ofVec3f c1 = d.crossed(ofVec3f(0, 1, 0)).normalized();
					const ofVec3f c = c1.crossed(d).normalized();
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
		
		for (int i = 0; i < buffer.size(); i++)
		{
			const BufferArray &arr = buffer[i];
			
			glBegin(GL_TRIANGLE_STRIP);
			for (int n = 0; n < arr.size(); n++)
			{
				const Buffer &b = arr[n];
				glNormal3fv(b.norm.getPtr());
				glVertex3fv(b.v1.getPtr());
				glVertex3fv(b.v2.getPtr());
			}
			glEnd();
		}

		// draw outline
		
		ofSetColor(0);
		
		for (int i = 0; i < buffer.size(); i++)
		{
			const BufferArray &arr = buffer[i];
			
			glBegin(GL_LINE_STRIP);
			for (int n = 0; n < arr.size(); n++)
			{
				const Buffer &b = arr[n];
				glNormal3fv(b.norm.getPtr());
				glVertex3fv(b.v1.getPtr());
			}
			glEnd();
		}
		
		for (int i = 0; i < buffer.size(); i++)
		{
			const BufferArray &arr = buffer[i];
			
			glBegin(GL_LINE_STRIP);
			for (int n = 0; n < arr.size(); n++)
			{
				const Buffer &b = arr[n];
				glNormal3fv(b.norm.getPtr());
				glVertex3fv(b.v2.getPtr());
			}
			glEnd();
		}

		if (!track.empty())
		{
			Frame &f = track[0];
			
			glBegin(GL_LINES);
			for (int n = 0; n < f.size(); n += 2)
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

vector<Tracker*> trackers;

//--------------------------------------------------------------
void testApp::setup()
{
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofSetSmoothLighting(true);
	ofSetGlobalAmbientColor(ofColor(220));

	ofBackground(255);
	
	bvh.resize(3);
	
	// You have to get motion and sound data from http://www.perfume-global.com
	
	// setup bvh
	bvh[0].load("bvhfiles/aachan.bvh");
	bvh[1].load("bvhfiles/kashiyuka.bvh");
	bvh[2].load("bvhfiles/nocchi.bvh");
	
	for (int i = 0; i < bvh.size(); i++)
	{
		bvh[i].setFrame(4);
	}
	
	track.loadSound("Perfume_globalsite_sound.wav");
	track.setLoop(true);
	track.play();
	
	// setup tracker
	for (int i = 0; i < bvh.size(); i++)
	{
		ofxBvh &b = bvh[i];

		Tracker *t = new Tracker;
		t->setup(&b);
		trackers.push_back(t);
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
	float t = (track.getPosition() * trackDuration);
	t = t / bvh[0].getDuration();
	
	center_t.set(0, 0, 0);
	
	for (int i = 0; i < bvh.size(); i++)
	{
		bvh[i].setPosition(t);
		bvh[i].update();
		
		center_t += bvh[i].getJoint(0)->getPosition();
	}
	
	center_t /= 3;
	center += (center_t - center) * 0.01;
	
	for (int i = 0; i < trackers.size(); i++)
	{
		trackers[i]->update();
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
				ofLine(10, 0, 0, -10, 0, 0);
				ofLine(0, 0, 10, 0, 0, -10);
				ofPopMatrix();
			}
		}
		
		ofSetColor(ofColor::white, 80);
		for (int i = 0; i < trackers.size(); i++)
		{
			trackers[i]->draw();
		}
	}
	ofPopMatrix();
	
	cam.end();
	
	light.disable();
	
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
