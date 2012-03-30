#include "ofxBvh.h"

static inline void billboard();

ofxBvh::~ofxBvh()
{
	unload();
}

void ofxBvh::load(string path)
{
	path = ofToDataPath(path);
	
	string data = ofBufferFromFile(path).getText();
	
	const size_t HIERARCHY_BEGIN = data.find("HIERARCHY", 0);
	const size_t MOTION_BEGIN = data.find("MOTION", 0);
	
	if (HIERARCHY_BEGIN == string::npos
		|| MOTION_BEGIN == string::npos)
	{
		ofLogError("ofxBvh", "invalid bvh format");
		return;
	}
	
	parseHierarchy(data.substr(HIERARCHY_BEGIN, MOTION_BEGIN));
	parseMotion(data.substr(MOTION_BEGIN));
	
	currentFrame = frames[0];
	
	int index = 0;
	updateJoint(index, currentFrame, root);
	
	frame_new = false;
}

void ofxBvh::unload()
{
	for (int i = 0; i < joints.size(); i++)
		delete joints[i];
	
	joints.clear();
	
	root = NULL;
	
	frames.clear();
	currentFrame.clear();
	
	num_frames = 0;
	frame_time = 0;
	
	rate = 1;
	play_head = 0;
	playing = false;
	loop = false;
	
	need_update = false;
}

void ofxBvh::play()
{
	playing = true;
}

void ofxBvh::stop()
{
	playing = false;
}

bool ofxBvh::isPlaying()
{
	return playing;
}

void ofxBvh::setLoop(bool yn)
{
	loop = yn;
}

bool ofxBvh::isLoop() { return loop; }

void ofxBvh::setRate(float rate)
{
	this->rate = rate;
}

void ofxBvh::updateJoint(int& index, const FrameData& frame_data, ofxBvhJoint *joint)
{
	ofVec3f translate;
	ofQuaternion rotate;
	
	for (int i = 0; i < joint->channel_type.size(); i++)
	{
		float v = frame_data[index++];
		ofxBvhJoint::CHANNEL t = joint->channel_type[i];
		
		if (t == ofxBvhJoint::X_POSITION)
			translate.x = v;
		else if (t == ofxBvhJoint::Y_POSITION)
			translate.y = v;
		else if (t == ofxBvhJoint::Z_POSITION)
			translate.z = v;
		else if (t == ofxBvhJoint::X_ROTATION)
			rotate = ofQuaternion(v, ofVec3f(1, 0, 0)) * rotate;
		else if (t == ofxBvhJoint::Y_ROTATION)
			rotate = ofQuaternion(v, ofVec3f(0, 1, 0)) * rotate;
		else if (t == ofxBvhJoint::Z_ROTATION)
			rotate = ofQuaternion(v, ofVec3f(0, 0, 1)) * rotate;
	}
	
	translate += joint->initial_offset;
	
	joint->matrix.makeIdentityMatrix();
	joint->matrix.glTranslate(translate);
	joint->matrix.glRotate(rotate);
	
	joint->global_matrix = joint->matrix;
	joint->offset = translate;
	
	if (joint->parent)
	{
		joint->global_matrix.postMult(joint->parent->global_matrix);
	}
	
	for (int i = 0; i < joint->children.size(); i++)
	{
		updateJoint(index, frame_data, joint->children[i]);
	}
}

void ofxBvh::update()
{
	frame_new = false;
	
	if (playing && ofGetFrameNum() > 1)
	{
		int last_index = getFrame();
		
		play_head += ofGetLastFrameTime() * rate;
		int index = getFrame();
		
		if (index != last_index)
		{
			need_update = true;
			
			currentFrame = frames[index];
			
			if (index >= frames.size())
			{
				if (loop)
					play_head = 0;
				else
					playing = false;
			}
			
			if (play_head < 0)
				play_head = 0;
		}
	}
	
	if (need_update)
	{
		need_update = false;
		frame_new = true;
		
		int index = 0;
		updateJoint(index, currentFrame, root);
	}
}

void ofxBvh::draw()
{
	ofPushStyle();
	ofFill();
	
	for (int i = 0; i < joints.size(); i++)
	{
		ofxBvhJoint *o = joints[i];
		glPushMatrix();
		glMultMatrixf(o->getGlobalMatrix().getPtr());
		
		if (o->isSite())
		{
			ofSetColor(ofColor::yellow);
			billboard();
			ofCircle(0, 0, 6);
		}
		else if (o->getChildren().size() == 1)
		{
			ofSetColor(ofColor::white);		
			billboard();
			ofCircle(0, 0, 2);
		}
		else if (o->getChildren().size() > 1)
		{
			if (o->isRoot())
				ofSetColor(ofColor::cyan);
			else
				ofSetColor(ofColor::green);
			
			billboard();
			ofCircle(0, 0, 4);
		}
		
		glPopMatrix();
	}
	
	ofPopStyle();
}

bool ofxBvh::isFrameNew()
{
	return frame_new;
}

void ofxBvh::setFrame(int index)
{
	if (ofInRange(index, 0, frames.size()) && getFrame() != index)
	{
		currentFrame = frames[index];
		play_head = (float)index * frame_time;
		
		need_update = true;
	}
}

int ofxBvh::getFrame()
{
	return floor(play_head / frame_time);
}

void ofxBvh::setPosition(float pos)
{
	setFrame((float)frames.size() * pos);
}

float ofxBvh::getPosition()
{
	return play_head / (float)frames.size();
}

float ofxBvh::getDuration()
{
	return (float)frames.size() * frame_time;
}

void ofxBvh::parseHierarchy(const string& data)
{
	vector<string> tokens;
	string token;
	
	total_channels = 0;
	num_frames = 0;
	frame_time = 0;
	
	for (int i = 0; i < data.size(); i++)
	{
		char c = data[i];
		
		if (isspace(c))
		{
			if (!token.empty()) tokens.push_back(token);
			token.clear();
		}
		else
		{
			token.push_back(c);
		}
	}
	
	int index = 0;
	while (index < tokens.size())
	{
		if (tokens[index++] == "ROOT")
		{
			root = parseJoint(index, tokens, NULL);
		}
	}
}

ofxBvhJoint* ofxBvh::parseJoint(int& index, vector<string> &tokens, ofxBvhJoint *parent)
{
	string name = tokens[index++];
	ofxBvhJoint *joint = new ofxBvhJoint(name, parent);
	if (parent) parent->children.push_back(joint);
	
	joint->bvh = this;
	
	joints.push_back(joint);
	jointMap[name] = joint;
	
	while (index < tokens.size())
	{
		string token = tokens[index++];
		
		if (token == "OFFSET")
		{
			joint->initial_offset.x = ofToFloat(tokens[index++]);
			joint->initial_offset.y = ofToFloat(tokens[index++]);
			joint->initial_offset.z = ofToFloat(tokens[index++]);
			
			joint->offset = joint->initial_offset;
		}
		else if (token == "CHANNELS")
		{
			int num = ofToInt(tokens[index++]);
			
			joint->channel_type.resize(num);
			total_channels += num;
			
			for (int i = 0; i < num; i++)
			{
				string ch = tokens[index++];
				
				char axis = tolower(ch[0]);
				char elem = tolower(ch[1]);
				
				if (elem == 'p')
				{
					if (axis == 'x')
						joint->channel_type[i] = ofxBvhJoint::X_POSITION;
					else if (axis == 'y')
						joint->channel_type[i] = ofxBvhJoint::Y_POSITION;
					else if (axis == 'z')
						joint->channel_type[i] = ofxBvhJoint::Z_POSITION;
					else
					{
						ofLogError("ofxBvh", "invalid bvh format");
						return NULL;
					}
				}
				else if (elem == 'r')
				{
					if (axis == 'x')
						joint->channel_type[i] = ofxBvhJoint::X_ROTATION;
					else if (axis == 'y')
						joint->channel_type[i] = ofxBvhJoint::Y_ROTATION;
					else if (axis == 'z')
						joint->channel_type[i] = ofxBvhJoint::Z_ROTATION;
					else
					{
						ofLogError("ofxBvh", "invalid bvh format");
						return NULL;
					}
				}
				else
				{
					ofLogError("ofxBvh", "invalid bvh format");
					return NULL;
				}
			}
		}
		else if (token == "JOINT"
				 || token == "End")
		{
			parseJoint(index, tokens, joint);
		}
		else if (token == "}")
		{
			break;
		}
	}
	
	return joint;
}

void ofxBvh::parseMotion(const string& data)
{
	vector<string> lines = ofSplitString(data, "\n", true, true);
	
	int index = 0;
	
	while (index < lines.size())
	{
		string line = lines[index];
		
		if (line.empty())
		{
			index++;
			continue;
		}
		
		if (line.find("MOTION") != string::npos) {}
		else if (line.find("Frames:") != string::npos)
		{
			num_frames = ofToInt(ofSplitString(line, ":")[1]);
		}
		else if (line.find("Frame Time:") != string::npos)
		{
			frame_time = ofToFloat(ofSplitString(line, ":")[1]);
		}
		else break;
		
		index++;
	}
	
	while (index < lines.size())
	{
		string line = lines[index];
		vector<string> channels = ofSplitString(line, " ");

		if (channels.size() != total_channels)
		{
			ofLogError("ofxBvh", "channel size mismatch");
			return;
		}
		
		char buf[64];
		FrameData data;
		for (int i = 0; i < channels.size(); i++)
		{
			float v;
			sscanf(channels[i].c_str(), "%f", &v);
			data.push_back(v);
		}
		
		frames.push_back(data);
		
		index++;
	}
	
	if (num_frames != frames.size())
		ofLogWarning("ofxBvh", "frame size mismatch");
}

const ofxBvhJoint* ofxBvh::getJoint(int index)
{
	return joints.at(index);
}

const ofxBvhJoint* ofxBvh::getJoint(string name)
{
	return jointMap[name];
}

static inline void billboard()
{
	GLfloat m[16];
	glGetFloatv(GL_MODELVIEW_MATRIX, m);
	
	float inv_len;
	
	m[8] = -m[12];
	m[9] = -m[13];
	m[10] = -m[14];
	inv_len = 1. / sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
	m[8] *= inv_len;
	m[9] *= inv_len;
	m[10] *= inv_len;
	
	m[0] = -m[14];
	m[1] = 0.0;
	m[2] = m[12];
	inv_len = 1. / sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	m[0] *= inv_len;
	m[1] *= inv_len;
	m[2] *= inv_len;
	
	m[4] = m[9] * m[2] - m[10] * m[1];
	m[5] = m[10] * m[0] - m[8] * m[2];
	m[6] = m[8] * m[1] - m[9] * m[0];
	
	glLoadMatrixf(m);
}