#pragma once
#include "ofMain.h"
#include "ofxBvh.h"
#include <array>

struct AppOptions {
    bool smokeTest = false;
    std::string capturePath;
    int captureFrame = 180;
    bool automated() const { return smokeTest || !capturePath.empty(); }
};

class ofApp : public ofBaseApp {
public:
    explicit ofApp(AppOptions options) : options(std::move(options)) {}
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    static constexpr int jointsPerDancer = 13;
    AppOptions options;
    std::array<ofxBvh, 3> motions;
    std::array<glm::vec3, 3> origins{};
    std::array<glm::vec2, jointsPerDancer * 3> jointPositions{};
    ofShader fieldShader;
    ofFbo canvas;
    bool ready = false, paused = false, showHelp = true;
    float timeline = 12.0f, contourDensity = 8.0f, traveled = 0.0f;
    int renderedFrames = 0;
    glm::vec2 previousJoint{};
    void updatePose();
    void finishAutomatedFrame();
};
