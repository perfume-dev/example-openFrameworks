#pragma once

#include "ofMain.h"
#include "ofxMarchingCubes.h"

class MetaBall: public ofPoint{
public:
	ofPoint accel, vel;
	float size;
	MetaBall(){
		size = ofRandom(5, 10);
	}
	
	void init(const ofPoint& _pos){
		x = _pos.x;
		y = _pos.y;
	}
	
	void goTo(const ofPoint& target, float k = 0.1f, float damp = 0.9f){
		accel = (target - *this)*k;
		vel += accel;
		vel *= damp;
		*this += vel;
	}
	
	void update(const ofPoint& _force, float damp = 0.9f){
		vel += _force;
		vel *= damp;
		*this += vel;
	}
};

