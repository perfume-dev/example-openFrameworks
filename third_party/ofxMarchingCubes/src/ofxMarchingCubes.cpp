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


#include "ofxMarchingCubes.h"

ofxMarchingCubes::ofxMarchingCubes()
	: gridResX(0)
	, gridResY(0)
	, gridResZ(0)
	, numTriangles(0)
	, threshold(0.0f) {}

ofxMarchingCubes::~ofxMarchingCubes(){
	clear();
}

void ofxMarchingCubes::init(const ofPoint& _iniGridPos,const ofPoint& _gridSize, unsigned int _gridResX, unsigned int _gridResY,unsigned int _gridResZ){
	clear();
	iniGridPos = _iniGridPos;
	gridResX = MAX(2, _gridResX);
	gridResY = MAX(2, _gridResY);
	gridResZ = MAX(2, _gridResZ);
	gridSize = _gridSize;
	setupGrid();
}

void ofxMarchingCubes::clear(){
	isoValues.clear();
	gridPoints.clear();
	vertices.clear();
	normals.clear();
	gridResX = 0;
	gridResY = 0;
	gridResZ = 0;
	numTriangles = 0;
}

void ofxMarchingCubes::update(float _threshold, bool bCalcNormals){
	threshold = _threshold;
	numTriangles = 0;
	vertices.clear();
	normals.clear();
	if (!isGridReady()) return;
	const int numPointsX = gridResX - 1;
	const int numPointsY = gridResY - 1;
	const int numPointsZ = gridResZ - 1;
	for(int i=0; i<numPointsX; ++i){
		for(int j=0; j<numPointsY; ++j){
			for(int k=0; k<numPointsZ; ++k){
				polygonise(i, j, k, bCalcNormals);
			}
		}
	}
}

void ofxMarchingCubes::debugDraw(){
	if (!isGridReady()) return;
	glColor3f(1.0f, 1.0f, 1.0f);
	drawFilled();
	glLineWidth(2.0f);
	glColor3f(0.0f, 0.0f, 0.0f);
	drawWireFrame();
	drawCube();
	//drawGrid();
}

void ofxMarchingCubes::drawWireFrame(){
	int numVertices = vertices.size();
	normals.resize(numVertices);
	for(int i=0; i<numVertices; i+=3){
		glBegin(GL_LINE_STRIP);
		glNormal3f(normals[i].x, normals[i].y, normals[i].z);
		glVertex3f(vertices[i].x, vertices[i].y, vertices[i].z);
		glVertex3f(vertices[i+1].x, vertices[i+1].y, vertices[i+1].z);
		glVertex3f(vertices[i+2].x, vertices[i+2].y, vertices[i+2].z);
		glVertex3f(vertices[i].x, vertices[i].y, vertices[i].z);
		glEnd();
	}
}

void ofxMarchingCubes::drawFilled(){
	int numVertices = vertices.size();
	normals.resize(numVertices);
	glBegin(GL_TRIANGLES);
	for(int i=0; i<numVertices; i+=3){
		glNormal3f(normals[i].x, normals[i].y, normals[i].z);
		glVertex3f(vertices[i].x, vertices[i].y, vertices[i].z);
		glVertex3f(vertices[i+1].x, vertices[i+1].y, vertices[i+1].z);
		glVertex3f(vertices[i+2].x, vertices[i+2].y, vertices[i+2].z);
	}
	glEnd();
}

void ofxMarchingCubes::drawCube(){
	glPushMatrix();
	glTranslatef(iniGridPos.x, iniGridPos.y, iniGridPos.z);
	glScalef(gridSize.x, gridSize.y, gridSize.z);
    ofPushStyle();
    ofNoFill();
	    ofDrawBox(1.0f);
    ofPopStyle();
	glPopMatrix();
}

void ofxMarchingCubes::drawGrid(){
	if (!isGridReady()) return;
	for(int i=0; i<gridResX; i++){
		for(int j=0; j<gridResY; j++){
			glBegin(GL_LINE_STRIP);
			for(int k=0; k<gridResZ; k++){
				ofPoint& p = gridPoints[i][j][k];
				glVertex3f(p.x, p.y, p.z);
			}
			glEnd();
		}
	}
	for(int i=0; i<gridResZ; i++){
		for(int j=0; j<gridResY; j++){
			glBegin(GL_LINE_STRIP);
			for(int k=0; k<gridResX; k++){
				ofPoint& p = gridPoints[k][j][i];
				glVertex3f(p.x, p.y, p.z);
			}
			glEnd();
		}
	}
	for(int i=0; i<gridResZ; i++){
		for(int j=0; j<gridResX; j++){
			glBegin(GL_LINE_STRIP);
			for(int k=0; k<gridResY; k++){
				ofPoint& p = gridPoints[j][k][i];
				glVertex3f(p.x, p.y, p.z);
			}
			glEnd();
		}
	}
}

void ofxMarchingCubes::addMetaBall(const ofPoint& pos, float force){
	if (!isGridReady()) return;
	float distSQ;
	ofPoint diff;
	for(int i=0; i<gridResX; i++){
		for(int j=0; j<gridResY; j++){
			for(int k=0; k<gridResZ; k++){
				diff = gridPoints[i][j][k] - pos;
				distSQ = diff.x*diff.x + diff.y*diff.y + diff.z*diff.z;
				isoValues[i][j][k] += force / sqrtf(MAX(distSQ, 1e-12f));
			}
		}
	}
}

void ofxMarchingCubes::setIsoValue(unsigned int gridX, unsigned int gridY, unsigned int gridZ, float value){
	if(!isGridReady() || gridX >= gridResX or gridY >= gridResY or gridZ >= gridResZ){
		ofLogWarning("ofxMarchingCubes") << "setIsoValue: grid index out of bounds";
		return;
	}
	isoValues[gridX][gridY][gridZ] = value;
}

float ofxMarchingCubes::getIsoValue(unsigned int gridX, unsigned int gridY, unsigned int gridZ){
	if(!isGridReady() || gridX >= gridResX or gridY >= gridResY or gridZ >= gridResZ){
		ofLogWarning("ofxMarchingCubes")
			<< "getIsoValue: grid index out of bounds; returning zero";
		return 0;
	}
	return isoValues[gridX][gridY][gridZ];
}

void ofxMarchingCubes::resetIsoValues(){
	if (!isGridReady()) return;
	for(int i=0; i<gridResX; i++){
		for(int j=0; j<gridResY; j++){
			for(int k=0; k<gridResZ; k++){
				isoValues[i][j][k] = 0.0f;
			}
		}
	}
}

float ofxMarchingCubes::getMaxIsoValue(){
	if(!isGridReady()) return 0.0f;
	float max = isoValues[0][0][0];
	float val;
	for(int i=0; i<gridResX; ++i){
		for(int j=0; j<gridResY; ++j){
			for(int k=0; k<gridResZ; ++k){
				val = isoValues[i][j][k];
				if(val > max){
					max = val;
				}
			}
		}
	}
	return max;
}

float ofxMarchingCubes::getMinIsoValue(){
	if(!isGridReady()) return 0.0f;
	float min = isoValues[0][0][0];
	float val;
	for(int i=0; i<gridResX; ++i){
		for(int j=0; j<gridResY; ++j){
			for(int k=0; k<gridResZ; ++k){
				val = isoValues[i][j][k];
				if(val < min){
					min = val;
				}
			}
		}
	}
	return min;
}

float ofxMarchingCubes::getAverageIsoValue(){
	if(!isGridReady()) return 0.0f;
	float avg = 0.0f;
	for(int i=0; i<gridResX; ++i){
		for(int j=0; j<gridResY; ++j){
			for(int k=0; k<gridResZ; ++k){
				avg += isoValues[i][j][k];
			}
		}
	}
	avg /= gridResX * gridResY * gridResZ;
	return avg;
}

void ofxMarchingCubes::scaleIsoValues(float amount){
	if(!isGridReady()) return;
	for(int i=0; i<gridResX; ++i){
		for(int j=0; j<gridResY; ++j){
			for(int k=0; k<gridResZ; ++k){
				isoValues[i][j][k] *= amount;
			}
		}
	}
}

void ofxMarchingCubes::shiftIsoValues(float amount){
	if(!isGridReady()) return;
	for(int i=0; i<gridResX; ++i){
		for(int j=0; j<gridResY; ++j){
			for(int k=0; k<gridResZ; ++k){
				isoValues[i][j][k] += amount;
			}
		}
	}
}

void ofxMarchingCubes::normalizeIsoValues(){
	if(!isGridReady()) return;
	float min = getMinIsoValue();
	if(min < 0.0f){
		min = -min;
		shiftIsoValues(min);
	}
	float scale = getMaxIsoValue();
	if (ABS(scale) < 1e-12f) return;
	scale = 1.0f / scale;
	scaleIsoValues(scale);
}

void ofxMarchingCubes::rescaleIsoValues(float min, float max){
	if(!isGridReady()) return;
	const float lower = MIN(min, max);
	const float upper = MAX(min, max);
	float diff = upper - lower;
	normalizeIsoValues();
	scaleIsoValues(diff);
	shiftIsoValues(lower);
}

void ofxMarchingCubes::absoluteValues(){
	if(!isGridReady()) return;
	for(int i=0; i<gridResX; ++i){
		for(int j=0; j<gridResY; ++j){
			for(int k=0; k<gridResZ; ++k){
				isoValues[i][j][k] = ABS(isoValues[i][j][k]);
			}
		}
	}
}

int ofxMarchingCubes::getNumTriangles(){
	return numTriangles;
}

float ofxMarchingCubes::getThreshold(){
	return threshold;
}

vector<ofPoint>& ofxMarchingCubes::getVertices(){
	return vertices;
}

vector<ofPoint>& ofxMarchingCubes::getNormals(){
	return normals;
}

ofxMCGridValues& ofxMarchingCubes::getIsoValues(){
	return isoValues;
}

ofxMCGridPoints& ofxMarchingCubes::getGrid(){
	return gridPoints;
}

void ofxMarchingCubes::setGridPos(const ofPoint& _gridPos){
	if(_gridPos.x == iniGridPos.x and _gridPos.y == iniGridPos.y and _gridPos.z == iniGridPos.z) return;
	const int previousGridResX = gridResX;
	const int previousGridResY = gridResY;
	const int previousGridResZ = gridResZ;
	clear();
	iniGridPos = _gridPos;
	if (previousGridResX >= 2 && previousGridResY >= 2 && previousGridResZ >= 2) {
		gridResX = previousGridResX;
		gridResY = previousGridResY;
		gridResZ = previousGridResZ;
		setupGrid();
	}
}

void ofxMarchingCubes::setGridSize(const ofPoint& _gridSize){
	if(_gridSize.x == gridSize.x and _gridSize.y == gridSize.y and _gridSize.z == gridSize.z)return;
	const int previousGridResX = gridResX;
	const int previousGridResY = gridResY;
	const int previousGridResZ = gridResZ;
	clear();
	gridSize = _gridSize;
	if (previousGridResX >= 2 && previousGridResY >= 2 && previousGridResZ >= 2) {
		gridResX = previousGridResX;
		gridResY = previousGridResY;
		gridResZ = previousGridResZ;
		setupGrid();
	}
}

void ofxMarchingCubes::setGridRes(unsigned int _gridResX, unsigned int _gridResY, unsigned int _gridResZ){
	if(_gridResX == gridResX and _gridResY == gridResY and _gridResZ == gridResZ)return;
	clear();
	gridResX = MAX(2, _gridResX);
	gridResY = MAX(2, _gridResY);
	gridResZ = MAX(2, _gridResZ);
	setupGrid();
}

ofPoint ofxMarchingCubes::getGridPos(){
	return iniGridPos;
}

ofPoint ofxMarchingCubes::getGridSize(){
	return gridSize;
}

ofPoint ofxMarchingCubes::getGridRes(){
	return ofPoint(gridResX, gridResY, gridResZ);
}

#ifdef OFX_MARCHING_CUBES_ENABLE_STL
void ofxMarchingCubes::saveModel(string fileName, bool bUseASCII_mode){
	stlExporter.useASCIIFormat(bUseASCII_mode);

	int numVertices = vertices.size();
	for(int i=0; i<numVertices; i+=3){
		stlExporter.addTriangle(vertices[i], vertices[i+1], vertices[i+2], normals[i]);
	}
	stlExporter.saveModel(fileName);
}

ofxSTLExporter& ofxMarchingCubes::getSTLExporter(){
	return stlExporter;
}
#endif

void ofxMarchingCubes::setupGrid(){
	if (gridResX < 2 || gridResY < 2 || gridResZ < 2) return;
	ofPoint gap(gridSize.x / (gridResX-1), gridSize.y / (gridResY-1), gridSize.z / (gridResZ-1));
	ofPoint gridShift = gridSize * -0.5f;
	gridPoints.resize(gridResX);
	for(int i=0; i<gridResX; ++i){
		gridPoints[i].resize(gridResY);
		for(int j=0; j<gridResY; ++j){
			gridPoints[i][j].resize(gridResZ);
			for(int k=0; k<gridResZ; ++k){
				gridPoints[i][j][k].set(i*gap.x, j*gap.y, k*gap.z);
				gridPoints[i][j][k] += gridShift;
				gridPoints[i][j][k] += iniGridPos;
			}
		}
	}

	isoValues.resize(gridResX);
	for(int i=0; i<gridResX; i++){
		isoValues[i].resize(gridResY);
		for(int j=0; j<gridResY; j++){
			isoValues[i][j].resize(gridResZ);
			for(int k=0; k<gridResZ; k++){
				isoValues[i][j][k] = 0;
			}
		}
	}
	numTriangles = 0;
}

bool ofxMarchingCubes::isGridReady() const {
	if (gridResX < 2 || gridResY < 2 || gridResZ < 2
		|| isoValues.size() != static_cast<size_t>(gridResX)
		|| gridPoints.size() != static_cast<size_t>(gridResX)) {
		return false;
	}
	for (int i = 0; i < gridResX; ++i) {
		if (isoValues[i].size() != static_cast<size_t>(gridResY)
			|| gridPoints[i].size() != static_cast<size_t>(gridResY)) {
			return false;
		}
		for (int j = 0; j < gridResY; ++j) {
			if (isoValues[i][j].size() != static_cast<size_t>(gridResZ)
				|| gridPoints[i][j].size() != static_cast<size_t>(gridResZ)) {
				return false;
			}
		}
	}
	return true;
}

void ofxMarchingCubes::vertexInterp(float threshold,const ofPoint& p1,const ofPoint& p2, float valp1, float valp2, ofPoint& theVertice){
	float mu;
	if (ABS(threshold-valp1) < 0.00001){
		theVertice.set(p1.x, p1.y, p1.z);
		return;
	}
	if (ABS(threshold-valp2) < 0.00001){
		theVertice.set(p2.x, p2.y, p2.z);
		return;
	}
	if (ABS(valp1-valp2) < 0.00001){
		theVertice.set(p1.x, p1.y, p1.z);
		return;
	}
	mu = (threshold - valp1) / (valp2 - valp1);
	theVertice.x = p1.x + mu * (p2.x - p1.x);
	theVertice.y = p1.y + mu * (p2.y - p1.y);
	theVertice.z = p1.z + mu * (p2.z - p1.z);
}

void ofxMarchingCubes::polygonise(unsigned int i, unsigned int j, unsigned int k, bool bCalcNormals){
	int cubeindex;

	cubeindex = 0;
	if (isoValues[i][j][k] < threshold) cubeindex |= 1;
	if (isoValues[i+1][j][k] < threshold) cubeindex |= 2;
	if (isoValues[i+1][j+1][k] < threshold) cubeindex |= 4;
	if (isoValues[i][j+1][k] < threshold) cubeindex |= 8;
	if (isoValues[i][j][k+1] < threshold) cubeindex |= 16;
	if (isoValues[i+1][j][k+1] < threshold) cubeindex |= 32;
	if (isoValues[i+1][j+1][k+1] < threshold) cubeindex |= 64;
	if (isoValues[i][j+1][k+1] < threshold) cubeindex |= 128;
	if (edgeTable[cubeindex] == 0){
	return;
	}

	if (edgeTable[cubeindex] & 1){
		vertexInterp(threshold, gridPoints[i][j][k], gridPoints[i+1][j][k], isoValues[i][j][k] ,isoValues[i+1][j][k], vertList[0]);
	}
	if (edgeTable[cubeindex] & 2){
		vertexInterp(threshold, gridPoints[i+1][j][k], gridPoints[i+1][j+1][k], isoValues[i+1][j][k], isoValues[i+1][j+1][k], vertList[1]);
	}
	if (edgeTable[cubeindex] & 4){
		 vertexInterp(threshold, gridPoints[i+1][j+1][k], gridPoints[i][j+1][k], isoValues[i+1][j+1][k], isoValues[i][j+1][k], vertList[2]);
	}
	if (edgeTable[cubeindex] & 8){
		 vertexInterp(threshold, gridPoints[i][j+1][k], gridPoints[i][j][k], isoValues[i][j+1][k], isoValues[i][j][k], vertList[3]);
	}
	if (edgeTable[cubeindex] & 16){
		 vertexInterp(threshold, gridPoints[i][j][k+1], gridPoints[i+1][j][k+1], isoValues[i][j][k+1], isoValues[i+1][j][k+1], vertList[4]);
	}
	if (edgeTable[cubeindex] & 32){
		 vertexInterp(threshold, gridPoints[i+1][j][k+1], gridPoints[i+1][j+1][k+1], isoValues[i+1][j][k+1], isoValues[i+1][j+1][k+1], vertList[5]);
	}
	if (edgeTable[cubeindex] & 64){
		vertexInterp(threshold, gridPoints[i+1][j+1][k+1], gridPoints[i][j+1][k+1], isoValues[i+1][j+1][k+1], isoValues[i][j+1][k+1], vertList[6]);
	}
	if (edgeTable[cubeindex] & 128){
		vertexInterp(threshold, gridPoints[i][j+1][k+1], gridPoints[i][j][k+1], isoValues[i][j+1][k+1], isoValues[i][j][k+1], vertList[7]);
	}
	if (edgeTable[cubeindex] & 256){
		vertexInterp(threshold, gridPoints[i][j][k], gridPoints[i][j][k+1], isoValues[i][j][k], isoValues[i][j][k+1], vertList[8]);
	}
	if (edgeTable[cubeindex] & 512){
		vertexInterp(threshold, gridPoints[i+1][j][k], gridPoints[i+1][j][k+1], isoValues[i+1][j][k], isoValues[i+1][j][k+1], vertList[9]);
	}
	if (edgeTable[cubeindex] & 1024){
		vertexInterp(threshold, gridPoints[i+1][j+1][k], gridPoints[i+1][j+1][k+1], isoValues[i+1][j+1][k], isoValues[i+1][j+1][k+1], vertList[10]);
	}
	if (edgeTable[cubeindex] & 2048){
		vertexInterp(threshold,	gridPoints[i][j+1][k], gridPoints[i][j+1][k+1], isoValues[i][j+1][k], isoValues[i][j+1][k+1], vertList[11]);
	}
	for (i=0;triTable[cubeindex][i]!=-1;i+=3) {
		if(bCalcNormals){
			ofVec3f a = vertList[triTable[cubeindex][i+1]] - vertList[triTable[cubeindex][i]];
			ofVec3f b = vertList[triTable[cubeindex][i+2]] - vertList[triTable[cubeindex][i+1]];
			//cross product - a cross b
			ofVec3f normalVec = a.getCrossed(b);
			normalVec.normalize();
			normals.push_back(normalVec);
			normals.push_back(normalVec);
			normals.push_back(normalVec);
		}
		vertices.push_back(ofPoint(vertList[triTable[cubeindex][i]].x, vertList[triTable[cubeindex][i]].y, vertList[triTable[cubeindex][i]].z));
		vertices.push_back(ofPoint(vertList[triTable[cubeindex][i+1]].x, vertList[triTable[cubeindex][i+1]].y, vertList[triTable[cubeindex][i+1]].z));
		vertices.push_back(ofPoint(vertList[triTable[cubeindex][i+2]].x, vertList[triTable[cubeindex][i+2]].y, vertList[triTable[cubeindex][i+2]].z));
		numTriangles++;
	}
}
