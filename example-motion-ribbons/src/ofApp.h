#pragma once
#include "ofMain.h"
#include "ofxBvh.h"
#include <array>
#include <deque>

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
    using Pose = std::array<glm::vec3, 5>;
    AppOptions options;
    std::array<ofxBvh, 3> motions;
    std::array<std::array<std::deque<glm::vec3>, 5>, 3> trails;
    std::array<glm::vec3, 3> rootOffsets{};
    ofEasyCam camera;
    ofShader ribbonShader;
    ofVboMesh ribbons, skeleton;
    ofFbo canvas;
    bool paused = false;
    bool showHelp = true;
    bool ready = false;
    float timeline = 12.0f;
    float traveled = 0.0f;
    int renderedFrames = 0;
    glm::vec3 previousJoint{};
    void reset();
    void rebuildMeshes();
    glm::vec3 position(int dancer, const ofxBvhJoint* joint) const;
    void finishAutomatedFrame();
};
