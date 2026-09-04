#pragma once

#include "ofMain.h"

class ofxBvh;

class ofxBvhJoint
{
	friend class ofxBvh;
	
public:
	
	enum CHANNEL
	{
		X_ROTATION, Y_ROTATION, Z_ROTATION,
		X_POSITION, Y_POSITION, Z_POSITION
	};
	
	ofxBvhJoint(const string& name, ofxBvhJoint *parent)
		: name(name)
		, bvh(nullptr)
		, parent(parent) {}
	
	inline const string& getName() const { return name; }
	inline const ofVec3f& getOffset() const { return offset; }
	
	inline const ofMatrix4x4& getMatrix() const { return matrix; }
	inline const ofMatrix4x4& getGlobalMatrix() const { return global_matrix; }
	
	inline ofVec3f getPosition() const { return global_matrix.getTranslation(); }
	inline ofQuaternion getRotate() const { return global_matrix.getRotate(); }
	
	inline ofxBvhJoint* getParent() const { return parent; }
	inline const vector<ofxBvhJoint*>& getChildren() const { return children; }

	inline bool isSite() const { return children.empty(); }
	inline bool isRoot() const { return !parent; }
	
	inline ofxBvh* getBvh() const { return bvh; }
	
protected:

	string name;
	ofVec3f initial_offset;
	ofVec3f offset;
	
	ofMatrix4x4 matrix;
	ofMatrix4x4 global_matrix;
	
	ofxBvh* bvh;
	
	vector<ofxBvhJoint*> children;
	ofxBvhJoint* parent;
	
	vector<CHANNEL> channel_type;
};

class ofxBvh
{
public:
	
	ofxBvh()
		: total_channels(0)
		, root(nullptr)
		, num_frames(0)
		, frame_time(0.0f)
		, rate(1.0f)
		, playing(false)
		, play_head(0.0f)
		, loop(false)
		, need_update(false)
		, frame_new(false) {}
	
	virtual ~ofxBvh();
	ofxBvh(const ofxBvh&) = delete;
	ofxBvh& operator=(const ofxBvh&) = delete;
	
	bool load(const of::filesystem::path& path);
	void unload();

	void update();
	void update(float deltaSeconds);
	void draw();
	
	bool isFrameNew() const;
	bool isLoaded() const;
	
	void play();
	void stop();
	bool isPlaying() const;
	
	void setLoop(bool yn);
	bool isLoop() const;
	
	void setRate(float rate);

	void setFrame(int index);
	int getFrame() const;
	
	void setPosition(float pos);
	float getPosition() const;
	
	float getDuration() const;
	
	int getNumFrames() const { return static_cast<int>(frames.size()); }
	int getNumJoints() const { return static_cast<int>(joints.size()); }
	const ofxBvhJoint* getJoint(int index) const;
	const ofxBvhJoint* getJoint(const string& name) const;
	
protected:
	
	typedef vector<float> FrameData;
	
	int total_channels;
	
	ofxBvhJoint* root;
	vector<ofxBvhJoint*> joints;
	map<string, ofxBvhJoint*> jointMap;
	
	vector<FrameData> frames;
	FrameData currentFrame;
	
	int num_frames;
	float frame_time;
	
	float rate;
	
	bool playing;
	float play_head;
	
	bool loop;
	bool need_update;
	bool frame_new;
	
	bool parseHierarchy(const string& data);
	ofxBvhJoint* parseJoint(size_t& index, const vector<string>& tokens, ofxBvhJoint *parent);
	bool updateJoint(size_t& index, const FrameData& frame_data, ofxBvhJoint *joint);
	
	bool parseMotion(const string& data);
	
};
