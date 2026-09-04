/*
 * Copyright (c) 2009, Rui Madeira
 * Modified 2026-09-05 by Daito Manabe for openFrameworks 0.12.1
 * compatibility and safety; see ../README.md.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * http://creativecommons.org/licenses/LGPL/2.1/
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "ofMain.h"
#include "Tables.h"
#include "ofxMCTypes.h"
#ifdef OFX_MARCHING_CUBES_ENABLE_STL
#include "ofxSTL.h"
#endif

//marching cubes algorithm implementation based on the explanations and source in this page: http://local.wasp.uwa.edu.au/~pbourke/geometry/polygonise/

class ofxMarchingCubes{
public:
	ofxMarchingCubes();
	virtual ~ofxMarchingCubes();
	void init(const ofPoint& _iniGridPos, const ofPoint& gridSize, unsigned int _gridResX,unsigned int _gridResY,unsigned int _gridResZ);
	void clear();
	void update(float _threshold, bool bCalcNormals = false);
	virtual void draw(){debugDraw();}
	void debugDraw();
	void drawWireFrame();
	void drawFilled();
	void drawCube();
	void drawGrid();
	void addMetaBall(const ofPoint& pos, float force);
	void setIsoValue(unsigned int gridX, unsigned int gridY, unsigned int gridZ, float value);
	float getIsoValue(unsigned int gridX, unsigned int gridY, unsigned int gridZ);
	void resetIsoValues(); //set all values to 0
	float getMaxIsoValue();
	float getMinIsoValue();
	float getAverageIsoValue();
	void scaleIsoValues(float amount); //multiplies
	void shiftIsoValues(float amount); //adds
	void normalizeIsoValues();
	void rescaleIsoValues(float min, float max);
	void absoluteValues();
	int getNumTriangles();
	float getThreshold();
	vector<ofPoint>& getVertices();
	vector<ofPoint>& getNormals();
	ofxMCGridValues& getIsoValues();
	ofxMCGridPoints& getGrid();
	void setGridPos(const ofPoint& _gridPos);
	void setGridSize(const ofPoint& _gridSize);
	void setGridRes(unsigned int gridResX, unsigned int gridResY, unsigned int gridResZ);
	ofPoint getGridPos();
	ofPoint getGridSize();
	ofPoint getGridRes();

	#ifdef OFX_MARCHING_CUBES_ENABLE_STL
	void saveModel(string fileName, bool bUseASCII_mode = false);
	ofxSTLExporter& getSTLExporter();
	#endif

protected:
	#ifdef OFX_MARCHING_CUBES_ENABLE_STL
	ofxSTLExporter stlExporter;
	#endif
	void setupGrid();
	bool isGridReady() const;
	int gridResX, gridResY, gridResZ;
	ofPoint iniGridPos;
	ofPoint gridSize;
	int numTriangles;
	float threshold;
	vector<ofPoint>vertices;
	vector<ofPoint>normals;
	ofPoint vertList[12];

	ofxMCGridValues isoValues;
	ofxMCGridPoints gridPoints;

	void vertexInterp(float isoLevel,const ofPoint& p1, const ofPoint& p2, float valp1, float valp2, ofPoint& theVertice);
	void polygonise(uint i, uint j, uint k, bool bCalcNormals);
};
