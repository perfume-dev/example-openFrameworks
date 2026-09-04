#include "ofApp.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr std::size_t trailLength = 180;
constexpr std::array<const char*, 5> trackedJoints{
    "LeftWrist", "RightWrist", "LeftAnkle", "RightAnkle", "Head"};
const std::array<ofFloatColor, 3> palette{
    ofFloatColor(0.30f, 0.92f, 0.94f), ofFloatColor(1.0f, 0.37f, 0.30f),
    ofFloatColor(0.93f, 0.91f, 0.79f)};
}

void ofApp::setup() {
    ofSetWindowTitle("Motion Ribbons | perfume-dev");
    ofSetFrameRate(options.automated() ? 0 : 60);
    ofSetVerticalSync(!options.automated());
    ofBackground(7, 10, 16);
    ofFbo::Settings target;
    target.width = 1440;
    target.height = 900;
    target.internalformat = GL_RGBA;
    target.useDepth = true;
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
            rootOffsets[dancer] = glm::vec3(motions[dancer].getJoint(0)->getPosition());
            for (const auto* name : trackedJoints) loaded = motions[dancer].getJoint(name) && loaded;
        }
    }
    // GLSL 150 is the portable desktop GL 3.2 geometry-shader baseline.
    loaded = ribbonShader.setupShaderFromFile(GL_VERTEX_SHADER, "shaders/ribbon.vert") && loaded;
    loaded = ribbonShader.setupShaderFromFile(GL_GEOMETRY_SHADER, "shaders/ribbon.geom") && loaded;
    loaded = ribbonShader.setupShaderFromFile(GL_FRAGMENT_SHADER, "shaders/ribbon.frag") && loaded;
    loaded = ribbonShader.bindDefaults() && loaded;
    loaded = ribbonShader.linkProgram() && loaded;
    // OF 0.12.1's isLoaded/linkProgram result alone does not guarantee successful linkage.
    GLint linked = GL_FALSE;
    glGetProgramiv(ribbonShader.getProgram(), GL_LINK_STATUS, &linked);
    ready = loaded && linked == GL_TRUE && canvas.isAllocated();
    if (!ready) {
        ofLogError("Motion Ribbons") << "Required BVH, shader, or framebuffer could not be loaded.";
        ofExit(1);
        return;
    }
    ribbons.setMode(OF_PRIMITIVE_LINES);
    skeleton.setMode(OF_PRIMITIVE_LINES);
    camera.setNearClip(1.0f);
    camera.setFarClip(4000.0f);
    camera.setFov(36.0f);
    camera.setPosition(0.0f, 190.0f, 850.0f);
    camera.lookAt(glm::vec3(0.0f, 0.0f, -55.0f));
    if (options.automated()) camera.disableMouseInput();
    reset();
}

glm::vec3 ofApp::position(int dancer, const ofxBvhJoint* joint) const {
    // Remove horizontal stage travel so the three studies stay in their own columns.
    const glm::vec3 root(motions[dancer].getJoint(0)->getPosition());
    const glm::vec3 origin(root.x, rootOffsets[dancer].y, root.z);
    return glm::vec3(joint->getPosition()) - origin +
           glm::vec3((dancer - 1) * 230.0f, 0.0f, 0.0f);
}

void ofApp::reset() {
    timeline = 12.0f;
    for (auto& dancer : trails) for (auto& trail : dancer) trail.clear();
    for (auto& motion : motions) {
        motion.setPosition(timeline / motion.getDuration());
        motion.update(0.0f);
    }
    previousJoint = position(0, motions[0].getJoint("LeftWrist"));
    traveled = 0.0f;
    rebuildMeshes(); // Reset also takes effect immediately while playback is paused.
}

void ofApp::update() {
    if (!ready || paused) return;
    const float delta = options.automated() ? 1.0f / 60.0f : static_cast<float>(std::min(ofGetLastFrameTime(), 0.05));
    timeline += delta;
    for (int dancer = 0; dancer < 3; ++dancer) {
        auto& motion = motions[dancer];
        const float seconds = std::fmod(timeline, motion.getDuration());
        // A wrap is a discontinuity; never join the last pose to the first with a ribbon.
        if (seconds < delta) for (auto& trail : trails[dancer]) trail.clear();
        motion.setPosition(seconds / motion.getDuration());
        motion.update(0.0f);
        for (std::size_t joint = 0; joint < trackedJoints.size(); ++joint) {
            auto& trail = trails[dancer][joint];
            trail.push_back(position(dancer, motion.getJoint(trackedJoints[joint])));
            if (trail.size() > trailLength) trail.pop_front();
        }
    }
    const auto current = position(0, motions[0].getJoint("LeftWrist"));
    traveled += glm::distance(previousJoint, current);
    previousJoint = current;
    rebuildMeshes();
}

void ofApp::rebuildMeshes() {
    ribbons.clear();
    skeleton.clear();
    for (int dancer = 0; dancer < 3; ++dancer) {
        for (const auto& trail : trails[dancer]) {
            for (std::size_t i = 1; i < trail.size(); ++i) {
                for (const auto sample : {i - 1, i}) {
                    auto color = palette[dancer];
                    color.a = std::pow(static_cast<float>(sample + 1) / trail.size(), 0.8f) * 0.90f;
                    ribbons.addVertex(trail[sample]);
                    ribbons.addColor(color);
                }
            }
        }
        const auto& motion = motions[dancer];
        for (int j = 0; j < motion.getNumJoints(); ++j) {
            const auto* joint = motion.getJoint(j);
            if (!joint->getParent()) continue;
            skeleton.addVertex(position(dancer, joint));
            skeleton.addVertex(position(dancer, joint->getParent()));
            const auto tint = palette[dancer].getLerped(ofFloatColor(1.0f), 0.6f);
            skeleton.addColor(ofFloatColor(tint.r, tint.g, tint.b, 0.35f));
            skeleton.addColor(ofFloatColor(tint.r, tint.g, tint.b, 0.35f));
        }
    }
}

void ofApp::draw() {
    if (!ready) return;
    canvas.begin();
    ofClear(7, 10, 16, 255);
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(26, 33, 43);
    ofDrawLine(68, 104, 1372, 104);
    ofDrawLine(68, 808, 1372, 808);
    camera.begin(ofRectangle(0, 0, canvas.getWidth(), canvas.getHeight()));
    // Pixel widths remain stable when orbiting or resizing. Soft alpha avoids a heavy neon bloom.
    ribbonShader.begin();
    ribbonShader.setUniform2f("uViewport", canvas.getWidth(), canvas.getHeight());
    ribbonShader.setUniform1f("uWidth", 10.5f);
    ribbons.draw();
    ribbonShader.setUniform1f("uWidth", 1.6f);
    skeleton.draw();
    ribbonShader.end();
    camera.end();

    ofSetColor(225, 229, 230);
    ofDrawBitmapString("M O T I O N   R I B B O N S", 68, 68);
    ofSetColor(126, 140, 156);
    ofDrawBitmapString("PERFUME / MOTION STUDY 01", 1110, 68);
    ofDrawBitmapString("A   /   CYAN", 245, 775);
    ofDrawBitmapString("B   /   CORAL", 672, 775);
    ofDrawBitmapString("C   /   IVORY", 1101, 775);
    if (showHelp) {
        ofDrawBitmapString("SPACE pause  /  R reset  /  drag orbit  /  scroll zoom  /  H help", 68, 849);
        ofDrawBitmapString("VERTEX > GEOMETRY > FRAGMENT   /   " + ofToString(timeline, 2) + " s" +
                           (paused ? "   PAUSED" : ""), 941, 849);
    }
    canvas.end();
    ofSetColor(255);
    // Preserve composition in tall/wide windows with centered letterboxing.
    const float scale = std::min(ofGetWidth() / canvas.getWidth(), ofGetHeight() / canvas.getHeight());
    const ofRectangle viewport((ofGetWidth() - canvas.getWidth() * scale) * 0.5f,
                               (ofGetHeight() - canvas.getHeight() * scale) * 0.5f,
                               canvas.getWidth() * scale, canvas.getHeight() * scale);
    canvas.draw(viewport);
    camera.setControlArea(viewport);
    finishAutomatedFrame();
}

void ofApp::finishAutomatedFrame() {
    if (!options.automated() || ++renderedFrames < options.captureFrame) return;
    ofPixels pixels;
    canvas.readToPixels(pixels);
    if (!pixels.isAllocated() || pixels.getWidth() != 1440 || pixels.getHeight() != 900 ||
        pixels.getNumChannels() != 4) {
        ofLogError("Motion Ribbons") << "Framebuffer readback failed.";
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
    if (glGetError() != GL_NO_ERROR || brightPixels < 1000 || traveled < 1.0f) {
        ofLogError("Motion Ribbons") << "Render validation failed: pixels=" << brightPixels << ", motion=" << traveled;
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
        ofLogError("Motion Ribbons") << "Could not save capture.";
        ofExit(1);
        return;
    }
    std::cout << "SMOKE_TEST_OK frames=" << renderedFrames << " brightPixels=" << brightPixels
              << " motionDistance=" << validatedMotion << " shaderStages=3"
              << " controlsChecked=" << options.smokeTest << std::endl;
    ofExit(0);
}

void ofApp::keyPressed(int key) {
    if (key == ' ') paused = !paused;
    if (key == 'r' || key == 'R') reset();
    if (key == 'h' || key == 'H') showHelp = !showHelp;
}
