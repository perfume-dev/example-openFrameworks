#include "ofxBvh.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

static inline void billboard();

namespace {

bool parseFloatToken(const string& token, float& value)
{
	if (token.empty()) return false;

	char* end = nullptr;
	errno = 0;
	value = std::strtof(token.c_str(), &end);
	return errno != ERANGE && end == token.c_str() + token.size() && std::isfinite(value);
}

bool parseIntToken(const string& token, int& value)
{
	if (token.empty()) return false;

	char* end = nullptr;
	errno = 0;
	const long parsed = std::strtol(token.c_str(), &end, 10);
	if (errno == ERANGE || end != token.c_str() + token.size()
		|| parsed < std::numeric_limits<int>::min()
		|| parsed > std::numeric_limits<int>::max())
	{
		return false;
	}

	value = static_cast<int>(parsed);
	return true;
}

bool valueForHeader(const string& line, const string& expectedKey, string& value)
{
	const size_t colon = line.find(':');
	if (colon == string::npos || ofTrim(line.substr(0, colon)) != expectedKey) return false;
	value = ofTrim(line.substr(colon + 1));
	return !value.empty();
}

size_t findSectionLine(const string& data, const string& section, size_t offset = 0)
{
	size_t lineStart = offset;
	while (lineStart < data.size())
	{
		const size_t lineEnd = data.find('\n', lineStart);
		const size_t length = lineEnd == string::npos
			? data.size() - lineStart
			: lineEnd - lineStart;
		if (ofTrim(data.substr(lineStart, length)) == section) return lineStart;
		if (lineEnd == string::npos) break;
		lineStart = lineEnd + 1;
	}
	return string::npos;
}

} // namespace

ofxBvh::~ofxBvh()
{
	unload();
}

bool ofxBvh::load(const of::filesystem::path& path)
{
	unload();
	const auto dataPath = ofToDataPathFS(path, true);
	const string data = ofBufferFromFile(dataPath).getText();
	
	if (data.empty())
	{
		ofLogError("ofxBvh") << "could not read " << dataPath;
		return false;
	}
	
	const size_t HIERARCHY_BEGIN = findSectionLine(data, "HIERARCHY");
	const size_t MOTION_BEGIN = findSectionLine(data, "MOTION",
		HIERARCHY_BEGIN == string::npos ? 0 : HIERARCHY_BEGIN);
	
	if (HIERARCHY_BEGIN == string::npos
		|| MOTION_BEGIN == string::npos
		|| HIERARCHY_BEGIN >= MOTION_BEGIN)
	{
		ofLogError("ofxBvh") << "invalid BVH format in " << dataPath;
		return false;
	}
	
	if (!parseHierarchy(data.substr(HIERARCHY_BEGIN, MOTION_BEGIN - HIERARCHY_BEGIN))
		|| !parseMotion(data.substr(MOTION_BEGIN)))
	{
		ofLogError("ofxBvh") << "could not parse " << dataPath;
		unload();
		return false;
	}
	
	if (!root || frames.empty() || frame_time <= 0.0f)
	{
		ofLogError("ofxBvh") << "incomplete BVH data in " << dataPath;
		unload();
		return false;
	}

	currentFrame = frames.front();
	
	size_t index = 0;
	if (!updateJoint(index, currentFrame, root) || index != currentFrame.size())
	{
		ofLogError("ofxBvh") << "invalid channel data in " << dataPath;
		unload();
		return false;
	}
	
	frame_new = false;
	return true;
}

void ofxBvh::unload()
{
	for (ofxBvhJoint* joint : joints)
		delete joint;
	
	joints.clear();
	jointMap.clear();
	
	root = nullptr;
	
	frames.clear();
	currentFrame.clear();
	
	num_frames = 0;
	frame_time = 0;
	
	rate = 1;
	play_head = 0;
	playing = false;
	loop = false;
	
	need_update = false;
	frame_new = false;
}

void ofxBvh::play()
{
	playing = true;
}

void ofxBvh::stop()
{
	playing = false;
}

bool ofxBvh::isPlaying() const
{
	return playing;
}

void ofxBvh::setLoop(bool yn)
{
	loop = yn;
}

bool ofxBvh::isLoop() const { return loop; }

void ofxBvh::setRate(float rate)
{
	this->rate = std::isfinite(rate) ? rate : 0.0f;
}

bool ofxBvh::updateJoint(size_t& index, const FrameData& frame_data, ofxBvhJoint *joint)
{
	if (!joint) return false;

	ofVec3f translate;
	ofQuaternion rotate;
	
	for (const ofxBvhJoint::CHANNEL type : joint->channel_type)
	{
		if (index >= frame_data.size()) return false;
		const float value = frame_data[index++];
		
		if (type == ofxBvhJoint::X_POSITION)
			translate.x = value;
		else if (type == ofxBvhJoint::Y_POSITION)
			translate.y = value;
		else if (type == ofxBvhJoint::Z_POSITION)
			translate.z = value;
		else if (type == ofxBvhJoint::X_ROTATION)
			rotate = ofQuaternion(value, ofVec3f(1, 0, 0)) * rotate;
		else if (type == ofxBvhJoint::Y_ROTATION)
			rotate = ofQuaternion(value, ofVec3f(0, 1, 0)) * rotate;
		else if (type == ofxBvhJoint::Z_ROTATION)
			rotate = ofQuaternion(value, ofVec3f(0, 0, 1)) * rotate;
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
	
	for (ofxBvhJoint* child : joint->children)
	{
		if (!updateJoint(index, frame_data, child)) return false;
	}

	return true;
}

void ofxBvh::update()
{
	update(ofGetLastFrameTime());
}

void ofxBvh::update(float deltaSeconds)
{
	frame_new = false;
	if (!isLoaded()) return;
	
	if (playing && std::isfinite(deltaSeconds) && deltaSeconds > 0.0f)
	{
		const int last_index = getFrame();
		const double duration = static_cast<double>(getDuration());
		const double nextPlayHead = static_cast<double>(play_head)
			+ static_cast<double>(deltaSeconds) * static_cast<double>(rate);
		if (loop)
		{
			play_head = static_cast<float>(std::fmod(nextPlayHead, duration));
			if (play_head < 0.0f) play_head += duration;
		}
		else if (nextPlayHead >= duration)
		{
			play_head = static_cast<float>(duration) - frame_time;
			playing = false;
		}
		else if (nextPlayHead < 0.0)
		{
			play_head = 0.0f;
			playing = false;
		}
		else
		{
			play_head = static_cast<float>(nextPlayHead);
		}

		const int index = getFrame();
		
		if (index != last_index)
		{
			need_update = true;
			currentFrame = frames[static_cast<size_t>(index)];
		}
	}
	
	if (need_update)
	{
		need_update = false;
		frame_new = true;
		
		size_t index = 0;
		if (!updateJoint(index, currentFrame, root) || index != currentFrame.size())
		{
			ofLogError("ofxBvh") << "invalid channel data while updating";
			unload();
		}
	}
}

void ofxBvh::draw()
{
	if (!isLoaded()) return;

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
			ofDrawCircle(0, 0, 6);
		}
		else if (o->getChildren().size() == 1)
		{
			ofSetColor(ofColor::white);		
			billboard();
			ofDrawCircle(0, 0, 2);
		}
		else if (o->getChildren().size() > 1)
		{
			if (o->isRoot())
				ofSetColor(ofColor::cyan);
			else
				ofSetColor(ofColor::green);
			
			billboard();
			ofDrawCircle(0, 0, 4);
		}
		
		glPopMatrix();
	}
	
	ofPopStyle();
}

bool ofxBvh::isFrameNew() const
{
	return frame_new;
}

bool ofxBvh::isLoaded() const
{
	return root && !frames.empty() && frame_time > 0.0f;
}

void ofxBvh::setFrame(int index)
{
	if (index >= 0 && index < static_cast<int>(frames.size()) && getFrame() != index)
	{
		currentFrame = frames[static_cast<size_t>(index)];
		play_head = static_cast<float>(index) * frame_time;
		
		need_update = true;
	}
}

int ofxBvh::getFrame() const
{
	if (!isLoaded() || !std::isfinite(play_head)) return 0;
	return ofClamp(static_cast<int>(std::floor(play_head / frame_time + 1e-4f)),
		0, static_cast<int>(frames.size()) - 1);
}

void ofxBvh::setPosition(float pos)
{
	if (!isLoaded() || !std::isfinite(pos)) return;
	const float clamped = ofClamp(pos, 0.0f, 1.0f);
	const int frame = clamped >= 1.0f
		? static_cast<int>(frames.size()) - 1
		: static_cast<int>(clamped * static_cast<float>(frames.size()));
	setFrame(frame);
}

float ofxBvh::getPosition() const
{
	return isLoaded() ? ofClamp(play_head / getDuration(), 0.0f, 1.0f) : 0.0f;
}

float ofxBvh::getDuration() const
{
	return static_cast<float>(frames.size()) * frame_time;
}

bool ofxBvh::parseHierarchy(const string& data)
{
	vector<string> tokens;
	string token;
	
	total_channels = 0;
	num_frames = 0;
	frame_time = 0;
	
	for (const char c : data)
	{
		if (std::isspace(static_cast<unsigned char>(c)))
		{
			if (!token.empty()) tokens.push_back(token);
			token.clear();
		}
		else
		{
			token.push_back(c);
		}
	}
	if (!token.empty()) tokens.push_back(token);
	
	if (tokens.size() < 3 || tokens[0] != "HIERARCHY" || tokens[1] != "ROOT")
	{
		ofLogError("ofxBvh") << "hierarchy must begin with HIERARCHY ROOT";
		return false;
	}

	size_t index = 2;
	root = parseJoint(index, tokens, nullptr);
	if (!root) return false;
	if (index != tokens.size())
	{
		ofLogError("ofxBvh") << "unexpected data after ROOT joint";
		return false;
	}
	return total_channels > 0;
}

ofxBvhJoint* ofxBvh::parseJoint(size_t& index, const vector<string>& tokens, ofxBvhJoint *parent)
{
	if (index >= tokens.size())
	{
		ofLogError("ofxBvh") << "joint name is missing";
		return nullptr;
	}

	const string name = tokens[index++];
	if (index >= tokens.size() || tokens[index++] != "{")
	{
		ofLogError("ofxBvh") << "joint " << name << " has no opening brace";
		return nullptr;
	}

	ofxBvhJoint *joint = new ofxBvhJoint(name, parent);
	if (parent) parent->children.push_back(joint);
	
	joint->bvh = this;
	
	joints.push_back(joint);
	jointMap[name] = joint;
	
	bool closed = false;
	while (index < tokens.size())
	{
		const string token = tokens[index++];
		
		if (token == "OFFSET")
		{
			if (tokens.size() - index < 3
				|| !parseFloatToken(tokens[index], joint->initial_offset.x)
				|| !parseFloatToken(tokens[index + 1], joint->initial_offset.y)
				|| !parseFloatToken(tokens[index + 2], joint->initial_offset.z))
			{
				ofLogError("ofxBvh") << "invalid OFFSET for joint " << name;
				return nullptr;
			}
			index += 3;
			
			joint->offset = joint->initial_offset;
		}
		else if (token == "CHANNELS")
		{
			int num = 0;
			if (index >= tokens.size() || !parseIntToken(tokens[index++], num)
				|| num < 0 || static_cast<size_t>(num) > tokens.size() - index
				|| num > std::numeric_limits<int>::max() - total_channels)
			{
				ofLogError("ofxBvh") << "invalid CHANNELS count for joint " << name;
				return nullptr;
			}
			
			joint->channel_type.resize(static_cast<size_t>(num));
			total_channels += num;
			
			for (int i = 0; i < num; i++)
			{
				const string channel = ofToLower(tokens[index++]);
				if (channel == "xposition")
					joint->channel_type[i] = ofxBvhJoint::X_POSITION;
				else if (channel == "yposition")
					joint->channel_type[i] = ofxBvhJoint::Y_POSITION;
				else if (channel == "zposition")
					joint->channel_type[i] = ofxBvhJoint::Z_POSITION;
				else if (channel == "xrotation")
					joint->channel_type[i] = ofxBvhJoint::X_ROTATION;
				else if (channel == "yrotation")
					joint->channel_type[i] = ofxBvhJoint::Y_ROTATION;
				else if (channel == "zrotation")
					joint->channel_type[i] = ofxBvhJoint::Z_ROTATION;
				else
				{
					ofLogError("ofxBvh") << "invalid channel name for joint " << name;
					return nullptr;
				}
			}
		}
		else if (token == "JOINT"
				 || token == "End")
		{
			if (token == "End" && (index >= tokens.size() || tokens[index] != "Site"))
			{
				ofLogError("ofxBvh") << "invalid End Site declaration";
				return nullptr;
			}
			if (!parseJoint(index, tokens, joint)) return nullptr;
		}
		else if (token == "}")
		{
			closed = true;
			break;
		}
		else
		{
			ofLogError("ofxBvh") << "unexpected hierarchy token: " << token;
			return nullptr;
		}
	}
	
	if (!closed)
	{
		ofLogError("ofxBvh") << "joint " << name << " has no closing brace";
		return nullptr;
	}

	return joint;
}

bool ofxBvh::parseMotion(const string& data)
{
	vector<string> lines = ofSplitString(data, "\n", true, true);
	size_t index = 0;
	if (index >= lines.size() || ofTrim(lines[index++]) != "MOTION")
	{
		ofLogError("ofxBvh") << "motion section must begin with MOTION";
		return false;
	}

	string value;
	if (index >= lines.size()
		|| !valueForHeader(ofTrim(lines[index++]), "Frames", value)
		|| !parseIntToken(value, num_frames)
		|| num_frames <= 0)
	{
		ofLogError("ofxBvh") << "invalid Frames header";
		return false;
	}
	if (index >= lines.size()
		|| !valueForHeader(ofTrim(lines[index++]), "Frame Time", value)
		|| !parseFloatToken(value, frame_time)
		|| frame_time <= 0.0f
		|| total_channels <= 0)
	{
		ofLogError("ofxBvh") << "invalid Frame Time header";
		return false;
	}

	while (index < lines.size())
	{
		const string line = ofTrim(lines[index++]);
		if (line.empty()) continue;

		std::istringstream stream(line);
		string channel;
		FrameData frame;
		while (stream >> channel)
		{
			float value = 0.0f;
			if (!parseFloatToken(channel, value))
			{
				ofLogError("ofxBvh") << "invalid motion channel value";
				return false;
			}
			frame.push_back(value);
		}

		if (frame.size() != static_cast<size_t>(total_channels))
		{
			ofLogError("ofxBvh") << "channel size mismatch";
			return false;
		}

		frames.push_back(std::move(frame));
	}
	
	if (static_cast<size_t>(num_frames) != frames.size())
	{
		ofLogError("ofxBvh") << "frame size mismatch";
		return false;
	}
	const double duration = static_cast<double>(num_frames)
		* static_cast<double>(frame_time);
	if (!std::isfinite(duration)
		|| duration > static_cast<double>(std::numeric_limits<float>::max()))
	{
		ofLogError("ofxBvh") << "motion duration is out of range";
		return false;
	}

	return true;
}

const ofxBvhJoint* ofxBvh::getJoint(int index) const
{
	if (index < 0 || index >= static_cast<int>(joints.size())) return nullptr;
	return joints[static_cast<size_t>(index)];
}

const ofxBvhJoint* ofxBvh::getJoint(const string& name) const
{
	const auto it = jointMap.find(name);
	return it == jointMap.end() ? nullptr : it->second;
}

static inline void billboard()
{
	GLfloat m[16];
	glGetFloatv(GL_MODELVIEW_MATRIX, m);
	
	constexpr float epsilon = 1e-12f;

	m[8] = -m[12];
	m[9] = -m[13];
	m[10] = -m[14];
	float lengthSquared = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
	if (lengthSquared > epsilon)
	{
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		m[8] *= inverseLength;
		m[9] *= inverseLength;
		m[10] *= inverseLength;
	}
	else
	{
		m[8] = 0.0f;
		m[9] = 0.0f;
		m[10] = 1.0f;
	}
	
	m[0] = -m[14];
	m[1] = 0.0;
	m[2] = m[12];
	lengthSquared = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
	if (lengthSquared > epsilon)
	{
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		m[0] *= inverseLength;
		m[1] *= inverseLength;
		m[2] *= inverseLength;
	}
	else
	{
		m[0] = 1.0f;
		m[1] = 0.0f;
		m[2] = 0.0f;
	}
	
	m[4] = m[9] * m[2] - m[10] * m[1];
	m[5] = m[10] * m[0] - m[8] * m[2];
	m[6] = m[8] * m[1] - m[9] * m[0];
	
	glLoadMatrixf(m);
}
