#pragma once

#include "ofMain.h"
#include "../../shared/ExampleRuntime.h"

#include <array>
#include <deque>

struct G1Options {
    example::RunOptions run;
    int selectedClip = -1; // -1: all three; otherwise A / B / C.
    bool sourceOverlay = false;
};

// This viewer only displays exported reference poses. There is no physics engine,
// learned controller, live robot connection, or implied balance validation here.
class ofApp : public ofBaseApp {
public:
    explicit ofApp(G1Options options) : options(std::move(options)), runtime(this->options.run) {}
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
    void windowResized(int width, int height) override;

private:
    struct Frame {
        std::vector<glm::vec3> positions;
        std::vector<glm::quat> rotations;
        std::vector<glm::quat> localRotations;
        std::vector<glm::vec3> sourcePositions;
    };
    struct Clip {
        std::string name;
        std::vector<Frame> frames;
        Frame pose;
        std::array<std::deque<glm::vec3>, 2> trails;
        double previousSample = -1.0;
    };
    struct BodyMesh {
        int body = -1;
        ofVboMesh geometry;
        glm::mat4 localTransform{1.0f};
        ofFloatColor color;
    };

    G1Options options;
    example::Runtime runtime;
    std::vector<std::string> bodyNames, sourceNames;
    std::vector<int> bodyParents, sourceParents;
    std::vector<glm::vec3> bodyOffsets;
    std::vector<BodyMesh> bodyMeshes;
    std::array<Clip, 3> clips;
    std::array<int, 2> handBodies{{-1, -1}};
    ofCamera camera;
    ofShader surfaceShader;
    ofVboMesh grid, sourceLines, handTrails;
    bool ready = false;
    bool paused = false;
    bool showHelp = true;
    bool showSource = false;
    bool poseQaPassed = false;
    int selectedClip = -1;
    int rootBody = -1;
    float fps = 0.0f;
    double timeline = 0.0;
    std::size_t geometryVertices = 0;
    glm::vec2 previousMouse{};
    bool dragging = false;
    float orbitYaw = 2.73f, orbitPitch = 0.30f, orbitDistance = 5.1f;

    void loadData();
    void loadSurfaceShader();
    void samplePoses(bool appendTrails);
    void interpolateFrame(const Frame& a, const Frame& b, float fraction, Frame& pose) const;
    void validateInterpolation() const;
    void reset();
    void resetCamera();
    void updateCamera();
    void validateControls();
    void drawRobot(std::size_t index);
    void drawHud();
    bool isVisible(std::size_t index) const;
    glm::vec3 displayOffset(std::size_t index) const;
};
