#include "testApp.h"

#include <array>
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
			ofLogWarning("particle-motion-example")
				<< "Using bundled motion fallback for " << kPerfumeMotionFiles[i];
			loaded = motions[i].load(kBundledMotionFiles[i]) && loaded;
		}
	}
	return loaded;
}

} // namespace

class Particle
{
public:
	
	ofVec3f pos = {0, 0, 0};
	ofVec3f vel = {0, 0, 0};
	ofVec3f force = {0, 0, 0};
};

class Tracker
{
public:
	
	const ofxBvhJoint *joint = nullptr;
	deque<ofVec3f> samples;
	
	void setup(const ofxBvhJoint *o)
	{
		joint = o;
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
			
			for (auto& particle : particles)
			{
				ofVec3f dist = particle.pos - p;
				float r = dist.lengthSquared();
				
				if (r > 0 && r < 30*30)
				{
					r = sqrt(r);
					dist /= r;
					
					particle.force += ((A / pow(r, n)) - (B / pow(r, m))) * dist * 2;
				}
			}
		}
	}
	
	float length()
	{
		if (samples.empty()) return 0;
		
		float v = 0;
		for (size_t i = 0; i + 1 < samples.size(); ++i)
			v += samples[i].distance(samples[i + 1]);
		
		return v;
	}
	
	float dot()
	{
		if (samples.size() < 3) return 0;
		
		float v = 0;
		
		for (size_t i = 1; i + 1 < samples.size(); ++i)
		{
			const ofVec3f &v0 = samples[i - 1];
			const ofVec3f &v1 = samples[i];
			const ofVec3f &v2 = samples[i + 1];
			
			if (v0.squareDistance(v1) == 0) continue;
			if (v1.squareDistance(v2) == 0) continue;
			
			const ofVec3f d0 = (v0 - v1).getNormalized();
			const ofVec3f d1 = (v1 - v2).getNormalized();
			
			v += (d0).dot(d1);
		}
		
		return v / ((float)samples.size() - 2);
	}
	
	void draw()
	{
		if (samples.size() < 2) return;

		float len = length();
		len = ofMap(len, 30, 40, 0, 1, true);
		
		float d = dot();
		d = ofMap(d, 1, 0, 255, 0, true);
		
		glBegin(GL_LINE_STRIP);
		for (size_t i = 0; i < samples.size(); ++i)
		{
			float a = ofMap(static_cast<float>(i), 0.0f,
				static_cast<float>(samples.size() - 1), 1.0f, 0.0f, true);
			ofSetColor(d * len, 140 * a);
			glVertex3fv(samples[i].getPtr());
		}
		glEnd();
	}
};

class ParticleShape
{
public:
	
	ofxBvh *bvh = nullptr;
	
	vector<std::unique_ptr<Tracker>> tracker;
	
	vector<Particle> particles;
	size_t particle_index = 0;
	
	void setup(ofxBvh &o)
	{
		bvh = &o;
		
		for (int i = 1; i < o.getNumJoints(); ++i)
		{
			if (bvh->getJoint(i)->getName().find("Chest") == string::npos)
			{
				auto item = std::make_unique<Tracker>();
				item->setup(bvh->getJoint(i));
				tracker.push_back(std::move(item));
			}
		}
		
		particles.resize(15000);
	}
	
	void update()
	{
		bvh->update();
		
		for (auto& particle : particles)
		{
			particle.force.set(0, 0, 0);
		}
		
		if (bvh->isFrameNew())
		{
			for (auto& item : tracker)
			{
				// update force
				item->update(particles);
				
				const ofVec3f &p = item->joint->getPosition();
				
				// emit 10 particle every frame
				for (int i = 0; i < 10; ++i)
				{
					particles[particle_index].pos.set(p);
					
					particle_index++;
					if (particle_index >= particles.size())
						particle_index = 0;
				}
			}
		}
		
		// update particle position
		for (auto& p : particles)
		{
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

		for (auto& item : tracker)
		{
			item->draw();
		}
		
		ofSetColor(255, 15);
		glBegin(GL_POINTS);
		for (auto& p : particles)
		{
			glVertex3fv(p.pos.getPtr());
		}
		glEnd();
	}
};

constexpr size_t NUM_ACTOR = 3;
std::array<ParticleShape, NUM_ACTOR> particle_shapes;
std::array<ofxBvh, NUM_ACTOR> bvh;

ofSoundPlayer player;

ofVec3f center;

//--------------------------------------------------------------
void testApp::setup()
{
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	
	ofBackground(0);
	
	assetsReady = loadMotionFiles(bvh);
	
	for (size_t i = 0; i < NUM_ACTOR; ++i)
	{
		bvh[i].setFrame(1);
		if (assetsReady) particle_shapes[i].setup(bvh[i]);
	}
	
	audioReady = ofFile::doesFileExist("Perfume_globalsite_sound.wav")
		&& player.load("Perfume_globalsite_sound.wav");
	if (audioReady) {
		player.setLoop(true);
		player.play();
	} else {
		ofLogNotice("particle-motion-example")
			<< "Audio file not found; using a silent internal clock.";
	}
}

//--------------------------------------------------------------
void testApp::update()
{
	if (!assetsReady) return;
	const float motionDuration = bvh[0].getDuration();
	const float motionSeconds = audioReady
		? player.getPosition() * player.getDuration()
		: std::fmod(ofGetElapsedTimef(), motionDuration);
	const float t = ofClamp(motionSeconds / motionDuration, 0.0f, 1.0f);
	
	ofVec3f avg;
	
	for (size_t i = 0; i < NUM_ACTOR; ++i)
	{
		ofxBvh *o = particle_shapes[i].bvh;
		
		o->setPosition(t);
		particle_shapes[i].update();
		
		avg += o->getJoint(0)->getPosition();
	}
	
	avg /= 3;
	
	center += (avg - center) * 0.1;
}

//--------------------------------------------------------------
void testApp::draw()
{
	ofDisableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	
	// smooth particle
	glEnable(GL_POINT_SMOOTH);
	glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
	
	static GLfloat distance[] = {0.0, 0.0, 1.0};
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, distance);
	glPointSize(1500);
	
	glLineWidth(2);
	
	cam.begin();
	ofRotateYDeg(ofGetElapsedTimef() * 10);
	ofTranslate(-center);
	
	for (size_t i = 0; i < NUM_ACTOR; ++i)
	{
		particle_shapes[i].draw();
	}
	
	cam.end();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);

	if (!assetsReady)
		ofDrawBitmapString("Motion data is missing. See README.md.", 10, 20);
	else if (!audioReady)
		ofDrawBitmapString("Audio data is missing; running with a silent clock.", 10, 20);
}

//--------------------------------------------------------------
void testApp::keyPressed(int key)
{
	if (!audioReady) return;
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
