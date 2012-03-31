#include "testApp.h"

class Particle
{
public:
	
	ofVec3f pos;
	ofVec3f vel;
	ofVec3f force;
};

class Tracker
{
public:
	
	const ofxBvhJoint *joint, *root;
	deque<ofVec3f> samples;
	
	void setup(const ofxBvhJoint *o, const ofxBvhJoint *r)
	{
		joint = o;
		root = r;
	}
	
	void update(vector<Particle>& particles)
	{
		const ofVec3f &p = joint->getPosition();
		
		// update sample
		{
			samples.push_front(joint->getPosition());
			while (samples.size() > 10)
				samples.pop_back();
		}
		
		// update particle force
		{
			const float n = 2.0;
			const float A = 0.4;
			const float m = 1.1;
			const float B = 1.6;
			
			for (int i = 0; i < particles.size(); i++)
			{
				Particle &o = particles[i];
				ofVec3f dist = (o.pos - p);
				float r = dist.squareLength();
				
				if (r > 0 && r < 30*30)
				{
					r = sqrt(r);
					dist /= r;
					
					o.force += ((A / pow(r, n)) - (B / pow(r, m))) * dist * 2;
				}
			}
		}
	}
	
	float length()
	{
		if (samples.empty()) return 0;
		
		float v = 0;
		for (int i = 0; i < samples.size() - 1; i++)
			v += samples[i].distance(samples[i + 1]);
		
		return v;
	}
	
	float dot()
	{
		if (samples.empty()) return 0;
		
		float v = 0;
		
		for (int i = 1; i < samples.size() - 1; i++)
		{
			const ofVec3f &v0 = samples[i - 1];
			const ofVec3f &v1 = samples[i];
			const ofVec3f &v2 = samples[i + 1];
			
			if (v0.squareDistance(v1) == 0) continue;
			if (v1.squareDistance(v2) == 0) continue;
			
			const ofVec3f d0 = (v0 - v1).normalized();
			const ofVec3f d1 = (v1 - v2).normalized();
			
			v += (d0).dot(d1);
		}
		
		return v / ((float)samples.size() - 2);
	}
	
	void draw()
	{
		float len = length();
		len = ofMap(len, 30, 40, 0, 1, true);
		
		float d = dot();
		d = ofMap(d, 1, 0, 255, 0, true);
		
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < samples.size(); i++)
		{
			float a = ofMap(i, 0, samples.size() - 1, 1, 0, true);
			ofSetColor(d * len, 140 * a);
			glVertex3fv(samples[i].getPtr());
		}
		glEnd();
	}
};

class ParticleShape
{
public:
	
	ofxBvh *bvh;
	
	vector<Tracker*> tracker;
	
	vector<Particle> particles;
	int particle_index;
	
	void setup(ofxBvh &o)
	{
		bvh = &o;
		
		for (int i = 1; i < o.getNumJoints(); i++)
		{
			if (bvh->getJoint(i)->getName().find("Chest") == string::npos)
			{
				Tracker *t = new Tracker;
				t->setup(bvh->getJoint(i), bvh->getJoint(0));
				tracker.push_back(t);
			}
		}
		
		particle_index = 0;
		particles.resize(15000);
		for (int i = 0; i < particles.size(); i++)
		{
			Particle &p = particles[i];
			p.pos.set(0, 0, 0);
			p.vel.set(0, 0, 0);
		}
	}
	
	void update()
	{
		bvh->update();
		
		for (int i = 0; i < particles.size(); i++)
		{
			Particle &p = particles[i];
			p.force.set(0, 0, 0);
		}
		
		if (bvh->isFrameNew())
		{
			for (int i = 0; i < tracker.size(); i++)
			{
				// update force
				tracker[i]->update(particles);
				
				const ofVec3f &p = tracker[i]->joint->getPosition();
				
				// emit 10 particle every frame
				for (int i = 0; i < 10; i++)
				{
					particles[particle_index].pos.set(p);
					
					particle_index++;
					if (particle_index > particles.size())
						particle_index = 0;
				}
			}
		}
		
		// update particle position
		for (int i = 0; i < particles.size(); i++)
		{
			Particle &p = particles[i];
			
			p.force.y += -0.1;
			p.vel *= 0.98;
			
			p.vel += p.force * 0.9;
			p.pos += p.vel * 0.9;
			
			if (p.pos.y <= 0)
			{
				p.pos.y = 0;
				p.vel *= 0.95;
			}
		}
	}
	
	void draw()
	{
		// bvh->draw();

		for (int i = 0; i < tracker.size(); i++)
		{
			tracker[i]->draw();
		}
		
		ofSetColor(255, 15);
		glBegin(GL_POINTS);
		for (int i = 0; i < particles.size(); i++)
		{
			Particle &p = particles[i];
			glVertex3fv(p.pos.getPtr());
		}
		glEnd();
	}
};

const float trackDuration = 64.28;

const size_t NUM_ACTOR = 3;
vector<ParticleShape> particle_shapes;
vector<ofxBvh> bvh;

ofSoundPlayer player;

ofVec3f center;

//--------------------------------------------------------------
void testApp::setup()
{
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofBackground(0);
	
	bvh.resize(NUM_ACTOR);
	particle_shapes.resize(NUM_ACTOR);
	
	// You have to get motion and sound data from http://www.perfume-global.com
	
	bvh[0].load("bvhfiles/aachan.bvh");
	bvh[1].load("bvhfiles/kashiyuka.bvh");
	bvh[2].load("bvhfiles/nocchi.bvh");
	
	for (int i = 0; i < NUM_ACTOR; i++)
	{
		bvh[i].setFrame(1);
		particle_shapes[i].setup(bvh[i]);
	}
	
	player.loadSound("Perfume_globalsite_sound.wav");
	player.play();
}

//--------------------------------------------------------------
void testApp::update()
{
	float t = (player.getPosition() * trackDuration);
	
	ofVec3f avg;
	
	for (int i = 0; i < NUM_ACTOR; i++)
	{
		ofxBvh *o = particle_shapes[i].bvh;
		
		o->setPosition(t / o->getDuration());
		particle_shapes[i].update();
		
		avg += o->getJoint(0)->getPosition();
	}
	
	avg /= 3;
	
	center += (avg - center) * 0.1;
}

//--------------------------------------------------------------
void testApp::draw()
{
	glDisable(GL_DEPTH_TEST);
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	
	// smooth particle
	glEnable(GL_POINT_SMOOTH);
	glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
	
	static GLfloat distance[] = {0.0, 0.0, 1.0};
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, distance);
	glPointSize(1500);
	
	glLineWidth(2);
	
	cam.begin();
	ofRotateY(ofGetElapsedTimef() * 10);
	ofTranslate(-center);
	
	for (int i = 0; i < NUM_ACTOR; i++)
	{
		particle_shapes[i].draw();
	}
	
	cam.end();
}

//--------------------------------------------------------------
void testApp::keyPressed(int key)
{
	if (player.getSpeed() > 0)
		player.setSpeed(0);
	else
		player.setSpeed(1);
}

//--------------------------------------------------------------
void testApp::keyReleased(int key)
{

}

//--------------------------------------------------------------
void testApp::mouseMoved(int x, int y)
{

}

//--------------------------------------------------------------
void testApp::mouseDragged(int x, int y, int button)
{

}

//--------------------------------------------------------------
void testApp::mousePressed(int x, int y, int button)
{

}

//--------------------------------------------------------------
void testApp::mouseReleased(int x, int y, int button)
{

}

//--------------------------------------------------------------
void testApp::windowResized(int w, int h)
{

}

//--------------------------------------------------------------
void testApp::gotMessage(ofMessage msg)
{

}

//--------------------------------------------------------------
void testApp::dragEvent(ofDragInfo dragInfo)
{

}