#include "ofApp.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr std::array<const char*, 13> trackedJoints{
    "Hips", "Chest3", "Head", "LeftShoulder", "LeftElbow", "LeftWrist",
    "RightShoulder", "RightElbow", "RightWrist", "LeftKnee", "LeftAnkle",
    "RightKnee", "RightAnkle"};
}

void ofApp::setup() {
    ofSetWindowTitle("Motion Field | perfume-dev");
    ofSetFrameRate(options.automated() ? 0 : 60);
    ofSetVerticalSync(!options.automated());
    ofBackground(7, 10, 16);
    ofFbo::Settings target;
    target.width = 1440;
    target.height = 900;
    target.internalformat = GL_RGBA;
    target.textureTarget = GL_TEXTURE_2D;
    canvas.allocate(target);
    bool loaded = true;
    for (int dancer = 0; dancer < 3; ++dancer) {
        const auto filename = std::string(1, static_cast<char>('A' + dancer)) + "_test.bvh";
        loaded = motions[dancer].load("../../../example-bvh/bin/data/" + filename) && loaded;
        motions[dancer].setLoop(true);
        motions[dancer].play();
        if (motions[dancer].isLoaded()) {
            motions[dancer].setPosition(0.0f);
            motions[dancer].update(0.0f);
            origins[dancer] = glm::vec3(motions[dancer].getJoint(0)->getPosition());
            for (const auto* name : trackedJoints) loaded = motions[dancer].getJoint(name) && loaded;
        }
    }
    loaded = fieldShader.setupShaderFromFile(GL_VERTEX_SHADER, "shaders/field.vert") && loaded;
    loaded = fieldShader.setupShaderFromFile(GL_FRAGMENT_SHADER, "shaders/field.frag") && loaded;
    loaded = fieldShader.bindDefaults() && loaded;
    loaded = fieldShader.linkProgram() && loaded;
    // OF 0.12.1's isLoaded/linkProgram result alone does not guarantee successful linkage.
    GLint linked = GL_FALSE;
    glGetProgramiv(fieldShader.getProgram(), GL_LINK_STATUS, &linked);
    ready = loaded && linked == GL_TRUE && canvas.isAllocated();
    if (!ready) {
        ofLogError("Motion Field") << "Required BVH, shader, or framebuffer could not be loaded.";
        ofExit(1);
        return;
    }
    updatePose();
    previousJoint = jointPositions[5];
}

void ofApp::updatePose() {
    // CPU: project a sparse pose into a stable 2D coordinate system, measured in canvas heights.
    for (int dancer = 0; dancer < 3; ++dancer) {
        auto& motion = motions[dancer];
        motion.setPosition(std::fmod(timeline, motion.getDuration()) / motion.getDuration());
        motion.update(0.0f);
        const glm::vec3 root(motion.getJoint(0)->getPosition());
        const glm::vec3 origin(root.x, origins[dancer].y, root.z);
        for (int joint = 0; joint < jointsPerDancer; ++joint) {
            const auto p = glm::vec3(motion.getJoint(trackedJoints[joint])->getPosition()) - origin;
            jointPositions[dancer * jointsPerDancer + joint] =
                glm::vec2(p.x * 0.0020f + (dancer - 1) * 0.46f, p.y * 0.0020f);
        }
    }
}

void ofApp::update() {
    if (!ready || paused) return;
    timeline += options.automated() ? 1.0f / 60.0f : static_cast<float>(std::min(ofGetLastFrameTime(), 0.05));
    updatePose();
    traveled += glm::distance(previousJoint, jointPositions[5]);
    previousJoint = jointPositions[5];
}

void ofApp::draw() {
    if (!ready) return;
    canvas.begin();
    ofClear(7, 10, 16, 255);
    ofDisableDepthTest();
    ofSetColor(255);
    fieldShader.begin();
    fieldShader.setUniform2f("uResolution", canvas.getWidth(), canvas.getHeight());
    fieldShader.setUniform1f("uTime", timeline);
    fieldShader.setUniform1f("uContourDensity", contourDensity);
    fieldShader.setUniform2fv("uJoints", &jointPositions.front().x, jointPositions.size());
    // A single rectangle is the entire GPU geometry for this example.
    ofDrawRectangle(0, 0, canvas.getWidth(), canvas.getHeight());
    fieldShader.end();

    ofSetColor(49, 61, 75);
    ofDrawLine(68, 104, 1372, 104);
    ofDrawLine(68, 808, 1372, 808);
    ofSetColor(225, 229, 230);
    ofDrawBitmapString("M O T I O N   F I E L D", 68, 68);
    ofSetColor(126, 140, 156);
    ofDrawBitmapString("PERFUME / MOTION STUDY 02", 1110, 68);
    if (showHelp) {
        ofDrawBitmapString("SPACE pause  /  R reset  /  UP-DOWN contours  /  H help", 68, 849);
        ofDrawBitmapString("FRAGMENT FIELD   /   " + ofToString(contourDensity, 0) + " CONTOURS   /   " +
                           ofToString(timeline, 2) + " s" + (paused ? "   PAUSED" : ""), 974, 849);
    }
    canvas.end();
    ofSetColor(255);
    // Preserve composition in tall/wide windows with centered letterboxing.
    const float scale = std::min(ofGetWidth() / canvas.getWidth(), ofGetHeight() / canvas.getHeight());
    const ofRectangle viewport((ofGetWidth() - canvas.getWidth() * scale) * 0.5f,
                               (ofGetHeight() - canvas.getHeight() * scale) * 0.5f,
                               canvas.getWidth() * scale, canvas.getHeight() * scale);
    canvas.draw(viewport);
    finishAutomatedFrame();
}

void ofApp::finishAutomatedFrame() {
    if (!options.automated() || ++renderedFrames < options.captureFrame) return;
    ofPixels pixels;
    canvas.readToPixels(pixels);
    if (!pixels.isAllocated() || pixels.getWidth() != 1440 || pixels.getHeight() != 900 ||
        pixels.getNumChannels() != 4) {
        ofLogError("Motion Field") << "Framebuffer readback failed.";
        ofExit(1);
        return;
    }
    std::size_t brightPixels = 0;
    // Exclude every text/line overlay: only the central artwork can satisfy the check.
    for (std::size_t y = 125; y < 750; ++y) {
        for (std::size_t x = 68; x < 1372; ++x) {
            const auto i = (y * pixels.getWidth() + x) * pixels.getNumChannels();
            if (pixels[i] > 70 || pixels[i + 1] > 70 || pixels[i + 2] > 70) ++brightPixels;
        }
    }
    if (glGetError() != GL_NO_ERROR || brightPixels < 1000 || traveled < 0.001f) {
        ofLogError("Motion Field") << "Render validation failed: pixels=" << brightPixels << ", motion=" << traveled;
        ofExit(1);
        return;
    }
    const float validatedMotion = traveled;
    if (options.smokeTest) {
        const float frozenTime = timeline;
        keyPressed(' ');
        update();
        if (!paused || timeline != frozenTime) { ofExit(1); return; }
        keyPressed('r');
        if (timeline != 12.0f) { ofExit(1); return; }
        keyPressed(' ');
    }
    if (!options.capturePath.empty() && !ofSaveImage(pixels, options.capturePath)) {
        ofLogError("Motion Field") << "Could not save capture.";
        ofExit(1);
        return;
    }
    std::cout << "SMOKE_TEST_OK frames=" << renderedFrames << " brightPixels=" << brightPixels
              << " motionDistance=" << validatedMotion << " shaderStages=2"
              << " controlsChecked=" << options.smokeTest << std::endl;
    ofExit(0);
}

void ofApp::keyPressed(int key) {
    if (key == ' ') paused = !paused;
    if (key == 'r' || key == 'R') {
        timeline = 12.0f;
        updatePose();
        previousJoint = jointPositions[5];
    }
    if (key == 'h' || key == 'H') showHelp = !showHelp;
    if (key == OF_KEY_UP) contourDensity = std::min(contourDensity + 1.0f, 16.0f);
    if (key == OF_KEY_DOWN) contourDensity = std::max(contourDensity - 1.0f, 3.0f);
}
