#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr const char* validationLabel = "KINEMATIC REFERENCE \xE2\x80\x94 PHYSICS NOT VALIDATED";
constexpr std::size_t trailSamples = 36; // 0.6 seconds at the 60 Hz display clock.
constexpr float exportedPositionTolerance = 1e-4f; // 0.1 mm; input positions are rounded to 0.01 mm.
const ofFloatColor ink(0.09f, 0.12f, 0.13f);
const ofFloatColor cyan(0.02f, 0.59f, 0.65f);

void require(bool valid, const std::string& message) {
    if (!valid) throw std::runtime_error(message);
}

float number(const ofJson& value) {
    require(value.is_number(), "Expected a numeric value.");
    const float result = value.get<float>();
    require(std::isfinite(result), "Non-finite number in G1 data.");
    return result;
}

int integer(const ofJson& value, int minimum, int maximum, const std::string& label) {
    require(value.is_number_integer(), label + " must be an integer, not a coerced scalar.");
    // Check before get<int> so large JSON integers cannot wrap into valid indices.
    const double result = value.get<double>();
    require(result >= minimum && result <= maximum, label + " is out of range.");
    return static_cast<int>(result);
}

std::vector<int> parents(const ofJson& values, std::size_t count, const std::string& label) {
    require(values.is_array() && values.size() == count, label + " parent count is inconsistent.");
    std::vector<int> result;
    result.reserve(count);
    for (const auto& value : values) result.push_back(integer(value, -1, static_cast<int>(count) - 1, label + " parent"));
    return result;
}

glm::vec3 vector3(const ofJson& values, std::size_t index = 0) {
    return {number(values.at(index)), number(values.at(index + 1)), number(values.at(index + 2))};
}

glm::quat quaternion(const ofJson& values, std::size_t index = 0) {
    // Both exported quaternion arrays are wxyz; GLM's constructor is also wxyz.
    const glm::quat result(number(values.at(index)), number(values.at(index + 1)),
                           number(values.at(index + 2)), number(values.at(index + 3)));
    const float length = glm::length(result);
    require(std::abs(length - 1.0f) < 0.02f, "Quaternion is not unit length.");
    return result / length;
}

void validateParents(const std::vector<int>& parents, std::size_t count, const std::string& label) {
    require(parents.size() == count, label + " parent count is inconsistent.");
    int roots = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (parents[i] == -1) ++roots;
        require(parents[i] >= -1 && parents[i] < static_cast<int>(count) && parents[i] != static_cast<int>(i),
                label + " parent index is invalid.");
        int ancestor = static_cast<int>(i);
        for (std::size_t depth = 0; ancestor != -1; ++depth) {
            require(depth < count, label + " hierarchy contains a cycle.");
            ancestor = parents.at(ancestor);
        }
    }
    require(roots == 1, label + " requires exactly one root.");
}

ofJson loadJson(const std::string& filename) {
    require(ofFile::doesFileExist(filename), "Missing bin/data/" + filename + ". Export real G1 data first; no placeholder is used.");
    auto json = ofLoadJson(filename);
    require(json.is_object(), filename + " is not a JSON object.");
    return json;
}
}

void ofApp::setup() {
    runtime.setup();
    ofBackground(242, 243, 237);
    selectedClip = options.selectedClip;
    showSource = options.sourceOverlay;
    try {
        loadData();
        loadSurfaceShader();
        grid.setMode(OF_PRIMITIVE_LINES);
        for (int line = -16; line <= 16; ++line) {
            const float coordinate = line * 0.25f;
            const ofFloatColor color(line % 4 == 0 ? 0.76f : 0.85f, 0.79f, 0.77f, 0.50f);
            for (const auto vertex : {glm::vec3(-4, coordinate, 0), glm::vec3(4, coordinate, 0),
                                      glm::vec3(coordinate, -4, 0), glm::vec3(coordinate, 4, 0)}) {
                grid.addVertex(vertex);
                grid.addColor(color);
            }
        }
        sourceLines.setMode(OF_PRIMITIVE_LINES);
        handTrails.setMode(OF_PRIMITIVE_LINES);
        camera.setNearClip(0.025f);
        camera.setFarClip(100.0f);
        camera.setFov(38.0f);
        resetCamera();
        reset();
        ready = true;
        if (runtime.isSmokeTest()) {
            validateInterpolation();
            validateControls();
        }
    } catch (const std::exception& error) {
        ofLogError("G1 Motion Lab") << "SMOKE_TEST_FAILED " << error.what();
        ofExit(1);
    }
}

void ofApp::loadData() {
    const auto motion = loadJson("g1-motion.json");
    require(integer(motion.at("schema_version"), 1, 1, "schema_version") == 1, "Unsupported G1 motion schema.");
    require(motion.at("validation").at("label") == validationLabel,
            "The export must explicitly identify itself as unvalidated kinematic reference.");
    // A diagnostic export may render successfully without passing the pose gate.
    poseQaPassed = motion.at("validation").value("pose_status", std::string("unavailable")) == "pass";
    fps = number(motion.at("fps"));
    require(fps > 0.0f && fps <= 240.0f, "Reference FPS is invalid.");
    bodyNames = motion.at("body_names").get<std::vector<std::string>>();
    sourceNames = motion.at("source_names").get<std::vector<std::string>>();
    require(!bodyNames.empty() && bodyNames.size() <= 256 && !sourceNames.empty() && sourceNames.size() <= 256,
            "Body/source names are missing or excessive.");
    bodyParents = parents(motion.at("body_parents"), bodyNames.size(), "Robot");
    sourceParents = parents(motion.at("source_parents"), sourceNames.size(), "Source");
    validateParents(bodyParents, bodyNames.size(), "Robot");
    validateParents(sourceParents, sourceNames.size(), "Source");
    require(bodyParents.front() == -1, "Robot root must be body index 0.");
    rootBody = 0;
    for (std::size_t body = 1; body < bodyParents.size(); ++body) {
        require(bodyParents[body] >= 0 && bodyParents[body] < static_cast<int>(body),
                "Robot hierarchy must be parent-first.");
    }
    require(motion.contains("body_offsets"), "Missing body_offsets from the model export.");
    const auto& offsets = motion.at("body_offsets");
    require(offsets.is_array() && offsets.size() == bodyNames.size(), "Body offset count does not match body names.");
    bodyOffsets.reserve(offsets.size());
    for (const auto& offset : offsets) {
        require(offset.is_array() && offset.size() == 3, "Body offset must contain exactly three numbers.");
        bodyOffsets.push_back(vector3(offset));
    }
    // Prefer the most distal hand link; keep a wrist fallback for different official G1 assets.
    for (int side = 0; side < 2; ++side) {
        const std::string prefix = side == 0 ? "left" : "right";
        for (const auto suffix : {"rubber_hand", "hand", "wrist_yaw", "wrist_pitch", "wrist_roll", "elbow"}) {
            const auto target = prefix + "_" + suffix;
            for (std::size_t i = 0; i < bodyNames.size(); ++i) {
                if (bodyNames[i].find(target) != std::string::npos) { handBodies[side] = i; break; }
            }
            if (handBodies[side] != -1) break;
        }
        require(handBodies[side] != -1, "Could not identify both G1 hand/wrist bodies for trails.");
    }
    require(motion.at("clips").is_array() && motion.at("clips").size() == clips.size(), "Exactly A, B, C clips are required.");
    for (const auto& input : motion.at("clips")) {
        const auto name = input.at("name").get<std::string>();
        require(name == "A" || name == "B" || name == "C", "Clip must be named A, B, or C.");
        auto& clip = clips[name[0] - 'A'];
        require(clip.frames.empty(), "Duplicate clip name.");
        clip.name = name;
        const auto& frames = input.at("frames");
        require(frames.is_array() && frames.size() >= 2 && frames.size() <= 100000, "Invalid frame count.");
        clip.frames.reserve(frames.size());
        for (const auto& inputFrame : frames) {
            const auto& positions = inputFrame.at("positions");
            const auto& rotations = inputFrame.at("rotations");
            const auto& source = inputFrame.at("source_positions");
            require(positions.is_array() && positions.size() == bodyNames.size() * 3 &&
                    rotations.is_array() && rotations.size() == bodyNames.size() * 4 &&
                    source.is_array() && source.size() == sourceNames.size() * 3, "Frame array sizes do not match the names.");
            Frame frame;
            frame.positions.reserve(bodyNames.size());
            frame.rotations.reserve(bodyNames.size());
            frame.sourcePositions.reserve(sourceNames.size());
            for (std::size_t body = 0; body < bodyNames.size(); ++body) {
                frame.positions.push_back(vector3(positions, body * 3));
                frame.rotations.push_back(quaternion(rotations, body * 4));
            }
            frame.localRotations.resize(bodyNames.size());
            frame.localRotations[rootBody] = frame.rotations[rootBody];
            for (std::size_t body = 1; body < bodyNames.size(); ++body) {
                const auto parent = bodyParents[body];
                frame.localRotations[body] = glm::normalize(glm::inverse(frame.rotations[parent]) * frame.rotations[body]);
                const auto anchor = frame.positions[parent] + frame.rotations[parent] * bodyOffsets[body];
                require(glm::distance(anchor, frame.positions[body]) <= exportedPositionTolerance,
                        "Body offset disagrees with exported world poses.");
            }
            for (std::size_t joint = 0; joint < sourceNames.size(); ++joint) frame.sourcePositions.push_back(vector3(source, joint * 3));
            clip.frames.push_back(std::move(frame));
        }
    }

    const auto model = loadJson("g1-model.json");
    require(model.at("meshes").is_array() && !model.at("meshes").empty(), "The real G1 model has no meshes.");
    std::size_t discardedTriangles = 0;
    for (const auto& input : model.at("meshes")) {
        BodyMesh mesh;
        mesh.body = integer(input.at("body"), 0, static_cast<int>(bodyNames.size()) - 1, "Mesh body index");
        const auto& vertices = input.at("vertices");
        const auto& indices = input.at("indices");
        require(vertices.is_array() && vertices.size() >= 9 && vertices.size() <= 30000000 && vertices.size() % 3 == 0 &&
                indices.is_array() && indices.size() >= 3 && indices.size() % 3 == 0, "Mesh must contain indexed triangles.");
        mesh.geometry.setMode(OF_PRIMITIVE_TRIANGLES);
        for (std::size_t vertex = 0; vertex < vertices.size(); vertex += 3) mesh.geometry.addVertex(vector3(vertices, vertex));
        for (const auto& item : indices) {
            const int index = integer(item, 0, static_cast<int>(vertices.size() / 3) - 1, "Mesh triangle index");
            mesh.geometry.addIndex(index);
        }
        // The exported hulls have outward counter-clockwise winding. Compute the
        // matching normals explicitly: OF 0.12.1 flatNormals() uses the opposite
        // cross-product order, making these otherwise-correct hulls look dark.
        ofVboMesh triangles;
        triangles.setMode(OF_PRIMITIVE_TRIANGLES);
        const auto& meshVertices = mesh.geometry.getVertices();
        const auto& meshIndices = mesh.geometry.getIndices();
        for (std::size_t face = 0; face < meshIndices.size(); face += 3) {
            const auto a = meshVertices[meshIndices[face]];
            const auto b = meshVertices[meshIndices[face + 1]];
            const auto c = meshVertices[meshIndices[face + 2]];
            const auto cross = glm::cross(b - a, c - a);
            const float magnitude = glm::length(cross);
            require(std::isfinite(magnitude), "Model contains a non-finite triangle normal.");
            // Avoid unstable normals on numerically negligible facets (area <=
            // 5e-13 square metres), including facets collapsed by export rounding.
            if (magnitude <= 1e-12f) { ++discardedTriangles; continue; }
            const auto normal = cross / magnitude;
            for (const auto vertex : {a, b, c}) {
                triangles.addVertex(vertex);
                triangles.addNormal(normal);
            }
        }
        require(triangles.getNumVertices() >= 3, "A model mesh contains no drawable triangles.");
        mesh.geometry = std::move(triangles);
        require(input.at("position").size() == 3 && input.at("quaternion").size() == 4 && input.at("color").size() == 4,
                "Mesh transform or color has an invalid size.");
        mesh.localTransform = glm::translate(glm::mat4(1), vector3(input.at("position"))) * glm::mat4_cast(quaternion(input.at("quaternion")));
        const auto& color = input.at("color");
        for (const auto& channel : color) require(number(channel) >= 0 && number(channel) <= 1, "Mesh color is outside 0..1.");
        mesh.color = ofFloatColor(number(color[0]), number(color[1]), number(color[2]), number(color[3]));
        geometryVertices += mesh.geometry.getNumVertices();
        bodyMeshes.push_back(std::move(mesh));
    }
    if (discardedTriangles > 0) ofLogNotice("G1 Motion Lab") << "Omitted " << discardedTriangles
        << " numerically negligible facets (area <= 5e-13 m^2 each). No poses were changed.";
    ofLogNotice("G1 Motion Lab") << "Loaded real reference: " << bodyNames.size() << " bodies, "
        << bodyMeshes.size() << " hull meshes; A/B/C at " << fps << " Hz. KINEMATIC ONLY. poseQA="
        << (poseQaPassed ? "pass" : "FAILED / DIAGNOSTIC ONLY");
}

void ofApp::loadSurfaceShader() {
    bool loaded = surfaceShader.setupShaderFromFile(GL_VERTEX_SHADER, "shaders/surface.vert");
    loaded = surfaceShader.setupShaderFromFile(GL_FRAGMENT_SHADER, "shaders/surface.frag") && loaded;
    loaded = surfaceShader.bindDefaults() && loaded;
    loaded = surfaceShader.linkProgram() && loaded;
    GLint linked = GL_FALSE;
    glGetProgramiv(surfaceShader.getProgram(), GL_LINK_STATUS, &linked);
    require(loaded && linked == GL_TRUE, "G1 surface shaders failed to compile/link.");
}

bool ofApp::isVisible(std::size_t index) const { return selectedClip == -1 || selectedClip == static_cast<int>(index); }

glm::vec3 ofApp::displayOffset(std::size_t index) const {
    const auto root = clips[index].pose.positions[rootBody];
    // Presentation-only horizontal recentering, applied equally to source + robot.
    // Keep the exported vertical height; do not floor-clamp or hide penetration.
    return {-root.x, -root.y + (selectedClip == -1 ? (1.0f - static_cast<float>(index)) * 1.85f : 0.0f), 0.0f};
}

void ofApp::interpolateFrame(const Frame& a, const Frame& b, float fraction, Frame& pose) const {
    pose.positions.resize(bodyNames.size());
    pose.rotations.resize(bodyNames.size());
    pose.sourcePositions.resize(sourceNames.size());
    // Only the floating root has independent world translation. The model's
    // root rest offset is metadata, not an extra translation added at playback.
    pose.positions[rootBody] = glm::mix(a.positions[rootBody], b.positions[rootBody], fraction);
    pose.rotations[rootBody] = glm::normalize(glm::slerp(a.rotations[rootBody], b.rotations[rootBody], fraction));
    for (std::size_t body = 1; body < bodyNames.size(); ++body) {
        const auto parent = bodyParents[body];
        const auto local = glm::normalize(glm::slerp(a.localRotations[body], b.localRotations[body], fraction));
        pose.positions[body] = pose.positions[parent] + pose.rotations[parent] * bodyOffsets[body];
        pose.rotations[body] = glm::normalize(pose.rotations[parent] * local);
    }
    // The source skeleton is a comparison overlay, not the robot's rigid model.
    for (std::size_t joint = 0; joint < sourceNames.size(); ++joint) {
        pose.sourcePositions[joint] = glm::mix(a.sourcePositions[joint], b.sourcePositions[joint], fraction);
    }
}

void ofApp::validateInterpolation() const {
    float maximumKeyError = 0.0f, maximumAnchorGap = 0.0f;
    std::size_t intervals = 0;
    Frame pose;
    for (const auto& clip : clips) {
        for (std::size_t index = 0; index < clip.frames.size(); ++index) {
            const auto& frame = clip.frames[index];
            interpolateFrame(frame, frame, 0.0f, pose);
            for (std::size_t body = 0; body < bodyNames.size(); ++body) {
                maximumKeyError = std::max(maximumKeyError, glm::distance(pose.positions[body], frame.positions[body]));
                require(std::abs(glm::dot(pose.rotations[body], frame.rotations[body])) >= 1.0f - 1e-5f,
                        "Hierarchy interpolation changed a keyframe orientation.");
            }
            if (index + 1 == clip.frames.size()) continue;
            // Midpoints expose the old world-LERP link shrinkage; thirds also
            // cover the fractions encountered by a 60 Hz viewer of 40 Hz data.
            for (const auto fraction : {1.0f / 3.0f, 0.5f, 2.0f / 3.0f}) {
                interpolateFrame(frame, clip.frames[index + 1], fraction, pose);
                for (std::size_t body = 1; body < bodyNames.size(); ++body) {
                    const auto parent = bodyParents[body];
                    const auto relative = glm::inverse(pose.rotations[parent]) * (pose.positions[body] - pose.positions[parent]);
                    maximumAnchorGap = std::max(maximumAnchorGap, glm::distance(relative, bodyOffsets[body]));
                }
            }
            ++intervals;
        }
    }
    require(maximumKeyError <= exportedPositionTolerance, "Hierarchy interpolation changed keyframe positions beyond export precision.");
    require(maximumAnchorGap <= 1e-6f, "Hierarchy interpolation failed to preserve rigid body anchors.");
    ofLogNotice("G1 Motion Lab") << "Hierarchy interpolation checks passed: intervals=" << intervals
        << " keyPoseMaxErrorM=" << maximumKeyError << " midAnchorMaxGapM=" << maximumAnchorGap
        << ". Rendering geometry only, not pose or physics acceptance.";
}

void ofApp::samplePoses(bool appendTrails) {
    for (std::size_t index = 0; index < clips.size(); ++index) {
        auto& clip = clips[index];
        // No interpolation from the final pose to the initial pose: hold the last
        // sample for one source frame, then begin a new loop with cleared trails.
        const double framePosition = std::fmod(timeline * fps, static_cast<double>(clip.frames.size()));
        const auto first = static_cast<std::size_t>(framePosition);
        const auto second = std::min(first + 1, clip.frames.size() - 1);
        const float fraction = static_cast<float>(framePosition - first);
        const auto& a = clip.frames[first];
        const auto& b = clip.frames[second];
        interpolateFrame(a, b, fraction, clip.pose);
        if (framePosition < clip.previousSample) for (auto& trail : clip.trails) trail.clear();
        clip.previousSample = framePosition;
        if (appendTrails) {
            const auto offset = displayOffset(index);
            for (std::size_t hand = 0; hand < handBodies.size(); ++hand) {
                auto& trail = clip.trails[hand];
                trail.push_back(clip.pose.positions[handBodies[hand]] + offset);
                if (trail.size() > trailSamples) trail.pop_front();
            }
        }
    }
}

void ofApp::reset() {
    timeline = 0.0;
    for (auto& clip : clips) {
        clip.previousSample = -1.0;
        for (auto& trail : clip.trails) trail.clear();
    }
    samplePoses(false); // Reset is immediately visible even while paused.
}

void ofApp::resetCamera() {
    // The bundled dance faces approximately -X; start at a useful front three-quarter view.
    orbitYaw = 2.73f;
    orbitPitch = 0.30f;
    orbitDistance = selectedClip == -1 ? 5.1f : 3.6f;
    updateCamera();
}

void ofApp::updateCamera() {
    const float aspect = ofGetWidth() / static_cast<float>(std::max(1, ofGetHeight() - 208));
    const float distance = orbitDistance * std::max(1.0f, 1.75f / aspect);
    const glm::vec3 direction(std::cos(orbitYaw) * std::cos(orbitPitch),
                              std::sin(orbitYaw) * std::cos(orbitPitch), std::sin(orbitPitch));
    camera.setPosition(glm::vec3(0, 0, 0.72f) + direction * distance);
    camera.lookAt(glm::vec3(0, 0, 0.72f), glm::vec3(0, 0, 1));
}

void ofApp::validateControls() {
    keyPressed(' ');
    update();
    require(paused && timeline == 0.0, "Pause control failed.");
    timeline = 1.0;
    keyPressed('r');
    Frame initial;
    interpolateFrame(clips[0].frames[0], clips[0].frames[0], 0.0f, initial);
    require(paused && timeline == 0.0 && clips[0].pose.positions == initial.positions, "Paused reset failed.");
    keyPressed(' ');
    keyPressed('b');
    require(!paused && selectedClip == 1, "Clip selection failed.");
    keyPressed('0');
    require(selectedClip == -1, "Three-robot selection failed.");
    const bool sourceBefore = showSource;
    keyPressed('s');
    require(showSource != sourceBefore, "Source overlay control failed.");
    keyPressed('s');
    keyPressed('h');
    require(!showHelp, "Help control failed.");
    keyPressed('h');
    selectedClip = options.selectedClip;
    resetCamera();
    reset();
    ofLogNotice("G1 Motion Lab") << "Playback/selection/overlay control checks passed.";
}

void ofApp::update() {
    if (!ready || paused) return;
    timeline += runtime.isSmokeTest() ? 1.0 / 60.0 : std::min(static_cast<double>(runtime.deltaSeconds()), 0.05);
    samplePoses(true);
}

void ofApp::drawRobot(std::size_t index) {
    const auto& clip = clips[index];
    const auto offset = displayOffset(index);
    surfaceShader.begin();
    for (const auto& part : bodyMeshes) {
        const glm::mat4 bodyTransform = glm::translate(glm::mat4(1), clip.pose.positions[part.body] + offset)
            * glm::mat4_cast(clip.pose.rotations[part.body]);
        ofPushMatrix();
        ofMultMatrix(bodyTransform * part.localTransform);
        // Keep official model material contrast, with only a restrained cool tint.
        const auto color = part.color.getLerped(ofFloatColor(0.72f, 0.80f, 0.79f), 0.08f);
        surfaceShader.setUniform3f("uBaseColor", color.r, color.g, color.b);
        part.geometry.draw();
        ofPopMatrix();
    }
    surfaceShader.end();

    handTrails.clear();
    for (const auto& trail : clip.trails) {
        for (std::size_t sample = 1; sample < trail.size(); ++sample) {
            for (const auto point : {sample - 1, sample}) {
                handTrails.addVertex(trail[point]);
                auto color = cyan;
                color.a = static_cast<float>(point + 1) / trail.size() * 0.8f;
                handTrails.addColor(color);
            }
        }
    }
    ofSetColor(255);
    handTrails.draw();
    if (showSource) {
        sourceLines.clear();
        for (std::size_t joint = 0; joint < sourceParents.size(); ++joint) {
            if (sourceParents[joint] == -1) continue;
            sourceLines.addVertex(clip.pose.sourcePositions[joint] + offset);
            sourceLines.addVertex(clip.pose.sourcePositions[sourceParents[joint]] + offset);
        }
        // Depth remains enabled: source bones behind the robot are genuinely occluded.
        ofSetColor(6, 151, 164, 200);
        sourceLines.draw();
    }
}

void ofApp::draw() {
    if (!ready) return;
    ofBackground(242, 243, 237);
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofEnableDepthTest();
    camera.begin(ofRectangle(0, 108, ofGetWidth(), std::max(1, ofGetHeight() - 208)));
    ofSetColor(255);
    grid.draw();
    for (std::size_t index = 0; index < clips.size(); ++index) if (isVisible(index)) drawRobot(index);
    camera.end();
    ofDisableDepthTest();
    if (selectedClip == -1) {
        ofSetColor(80, 97, 98);
        const ofRectangle viewport(0, 108, ofGetWidth(), std::max(1, ofGetHeight() - 208));
        for (std::size_t index = 0; index < clips.size(); ++index) {
            const auto anchor = camera.worldToScreen(glm::vec3(0, (1.0f - index) * 1.85f, 0), viewport);
            ofDrawBitmapString(clips[index].name, anchor.x - 4, anchor.y + 24);
        }
    }
    drawHud();
    const auto visibleVertices = geometryVertices * (selectedClip == -1 ? clips.size() : 1);
    runtime.finishFrame(ready, "example-g1-motion-lab", visibleVertices);
}

void ofApp::drawHud() {
    const float width = ofGetWidth(), height = ofGetHeight();
    const float margin = std::max(20.0f, width * 0.045f);
    ofSetColor(ink);
    ofDrawBitmapString("G 1   /   M O T I O N   L A B", margin, 43);
    ofSetColor(90, 105, 106);
    ofDrawBitmapString("PERFUME  /  A B C  /  REFERENCE STUDY", margin, 68);
    if (!poseQaPassed) {
        ofSetColor(160, 66, 35);
        const std::string warning = "POSE QA FAILED - DIAGNOSTIC ONLY";
        ofDrawBitmapString(warning, width >= 1050 ? width - margin - warning.size() * 8 : margin, width >= 1050 ? 68 : 143);
    }
    ofSetColor(195, 206, 201);
    ofDrawLine(margin, 90, width - margin, 90);

    // This status never disappears when help is hidden. Bitmap text uses ASCII
    // so the essential warning remains legible without an external font asset.
    ofSetColor(ink);
    ofDrawBitmapString("KINEMATIC REFERENCE - PHYSICS NOT VALIDATED", margin, height - std::min(71.0f, height * 0.07f));
    ofSetColor(91, 106, 106);
    const auto& clip = clips[selectedClip < 0 ? 0 : selectedClip];
    const double seconds = std::fmod(timeline, clip.frames.size() / static_cast<double>(fps));
    const std::string mode = selectedClip == -1 ? "A + B + C" : clip.name;
    const auto status = mode + "  /  " + ofToString(seconds, 2) + " s  /  " + ofToString(fps, 0)
        + " Hz source" + (paused ? "  /  PAUSED" : "") + (showSource ? "  /  SOURCE OVERLAY" : "");
    if (width >= 1050) ofDrawBitmapString(status, width - margin - status.size() * 8, 43);
    else ofDrawBitmapString(status, margin, 113);
    if (showHelp) {
        ofDrawBitmapString("A B C single  /  0 all  /  SPACE pause  /  R reset  /  S source  /  H help", margin, height - 44);
        ofDrawBitmapString("drag orbit  /  scroll zoom  /  V reset camera  /  ROOT XY RECENTERED", margin, height - 23);
    } else {
        ofDrawBitmapString("ROOT XY RECENTERED  /  H controls", margin, height - 37);
    }
    // Timing uses exported sample count / FPS; this viewer adds no solver status.
    ofSetColor(204, 212, 207);
    // Keep the changing progress bar below the shared smoke-test content region.
    const float progressY = height * 0.905f;
    ofDrawLine(margin, progressY, width - margin, progressY);
    ofSetColor(cyan);
    const float progress = static_cast<float>(seconds / (clip.frames.size() / static_cast<double>(fps)));
    ofDrawLine(margin, progressY, margin + (width - 2 * margin) * progress, progressY);
}

void ofApp::keyPressed(int key) {
    if (!ready) return;
    if (key == ' ') paused = !paused;
    else if (key == 'r' || key == 'R') reset();
    else if (key == 'h' || key == 'H') showHelp = !showHelp;
    else if (key == 's' || key == 'S') showSource = !showSource;
    else if (key == 'v' || key == 'V') resetCamera();
    else {
        int next = selectedClip;
        if (key == '0' || key == '4') next = -1;
        else if (key >= 'a' && key <= 'c') next = key - 'a';
        else if (key >= 'A' && key <= 'C') next = key - 'A';
        else if (key >= '1' && key <= '3') next = key - '1';
        if (next != selectedClip) {
            selectedClip = next;
            for (auto& clip : clips) for (auto& trail : clip.trails) trail.clear();
            resetCamera();
        }
    }
}

void ofApp::windowResized(int width, int height) {
    updateCamera();
}

void ofApp::mousePressed(int x, int y, int button) {
    dragging = !runtime.isSmokeTest() && button == OF_MOUSE_BUTTON_LEFT && y >= 108 && y < ofGetHeight() - 100;
    previousMouse = glm::vec2(x, y);
}

void ofApp::mouseDragged(int x, int y, int button) {
    if (!dragging) return;
    const glm::vec2 position(x, y);
    const auto delta = position - previousMouse;
    orbitYaw -= delta.x * 0.005f;
    orbitPitch = ofClamp(orbitPitch + delta.y * 0.005f, -0.20f, 1.40f);
    previousMouse = position;
    updateCamera();
}

void ofApp::mouseReleased(int x, int y, int button) { dragging = false; }

void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY) {
    if (runtime.isSmokeTest() || y < 108 || y >= ofGetHeight() - 100) return;
    orbitDistance = ofClamp(orbitDistance * std::exp(-scrollY * 0.06f), 1.0f, 25.0f);
    updateCamera();
}
