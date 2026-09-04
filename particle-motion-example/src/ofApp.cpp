#include "ofApp.h"

void ofApp::Trail::update() {
	samples.emplace_front(joint->getPosition());
	if (samples.size() > 10) samples.pop_back();
	float distance = 0.0f;
	float alignment = 0.0f;
	for (size_t i = 0; i + 1 < samples.size(); ++i) distance += glm::distance(samples[i], samples[i + 1]);
	for (size_t i = 1; i + 1 < samples.size(); ++i) {
		const auto a = samples[i - 1] - samples[i];
		const auto b = samples[i] - samples[i + 1];
		if (glm::dot(a, a) > 1e-12f && glm::dot(b, b) > 1e-12f) alignment += glm::dot(glm::normalize(a), glm::normalize(b));
	}
	if (samples.size() > 2) alignment /= float(samples.size() - 2);
	const float brightness = ofMap(distance, 30, 40, 0, 1, true) * ofClamp(alignment, 0.0f, 1.0f);
	mesh.clear();
	mesh.setMode(OF_PRIMITIVE_LINE_STRIP);
	for (size_t i = 0; i < samples.size(); ++i) {
		const float age = samples.size() > 1 ? float(i) / float(samples.size() - 1) : 0.0f;
		mesh.addVertex(samples[i]);
		mesh.addColor(ofFloatColor(brightness, brightness, brightness, 0.55f * (1.0f - age)));
	}
}

void ofApp::ParticleShape::setup(ofxBvh& source) {
	motion = &source;
	for (int i = 1; i < source.getNumJoints(); ++i) {
		const auto* joint = source.getJoint(i);
		if (joint->getName().find("Chest") == std::string::npos) {
			Trail trail;
			trail.joint = joint;
			trails.push_back(std::move(trail));
		}
	}
	particles.resize(15000);
	points.setMode(OF_PRIMITIVE_POINTS);
	points.getVertices().resize(particles.size());
}

void ofApp::ParticleShape::update(float deltaSeconds) {
	const float steps = std::min(deltaSeconds * 60.0f, 4.0f);
	for (auto& particle : particles) particle.force = glm::vec3(0);
	if (motion->isFrameNew()) {
		for (auto& trail : trails) {
			trail.update();
			const glm::vec3 position(trail.joint->getPosition());
			for (auto& particle : particles) {
				auto direction = particle.position - position;
				const float squaredDistance = glm::dot(direction, direction);
				if (squaredDistance > 1e-4f && squaredDistance < 900.0f) {
					const float distance = std::sqrt(squaredDistance);
					particle.force += (0.4f / squaredDistance - 1.6f / std::pow(distance, 1.1f)) * direction / distance * 2.0f;
				}
			}
			for (int i = 0; i < 10; ++i) {
				particles[nextParticle] = {position, glm::vec3(0), glm::vec3(0)};
				nextParticle = (nextParticle + 1) % particles.size();
			}
		}
	}
	for (size_t i = 0; i < particles.size(); ++i) {
		auto& particle = particles[i];
		particle.force.y -= 0.1f;
		particle.velocity *= std::pow(0.98f, steps);
		particle.velocity += particle.force * 0.9f * steps;
		particle.position += particle.velocity * 0.9f * steps;
		if (particle.position.y < 0) {
			particle.position.y = 0;
			particle.velocity *= std::pow(0.95f, steps);
		}
		points.setVertex(i, particle.position);
	}
}

void ofApp::setup() {
	runtime.setup();
	ofBackground(0);
	playback.load("particle-motion-example", false, runtime.isSmokeTest());
	if (playback.ready) {
		for (size_t i = 0; i < shapes.size(); ++i) shapes[i].setup(playback.motions[i]);
	}
	camera.setDistance(450);
	// gl_PointCoord supplies a soft circular sprite without a texture dependency.
	shaderReady = pointShader.setupShaderFromSource(GL_VERTEX_SHADER, R"GLSL(#version 150
uniform mat4 modelViewProjectionMatrix;
uniform mat4 modelViewMatrix;
in vec4 position;
void main() {
    vec4 viewPosition = modelViewMatrix * position;
    gl_Position = modelViewProjectionMatrix * position;
    gl_PointSize = clamp(1500.0 / max(1.0, -viewPosition.z), 1.0, 12.0);
}
)GLSL");
	shaderReady = pointShader.setupShaderFromSource(GL_FRAGMENT_SHADER, R"GLSL(#version 150
out vec4 fragColor;
void main() {
    float radius = length(gl_PointCoord * 2.0 - 1.0);
    float alpha = 0.12 * (1.0 - smoothstep(0.0, 1.0, radius));
    fragColor = vec4(vec3(1.0), alpha);
}
)GLSL") && shaderReady;
	pointShader.bindDefaults();
	shaderReady = pointShader.linkProgram() && shaderReady;
	GLint linked = GL_FALSE;
	glGetProgramiv(pointShader.getProgram(), GL_LINK_STATUS, &linked);
	shaderReady = shaderReady && linked == GL_TRUE;
}

void ofApp::update() {
	const float dt = paused ? 0.0f : runtime.deltaSeconds();
	playback.update(dt, paused ? 0.0f : 1.0f);
	if (!playback.ready) return;
	glm::vec3 average{0};
	for (auto& shape : shapes) {
		if (!paused) shape.update(dt);
		average += glm::vec3(shape.motion->getJoint(0)->getPosition());
	}
	center = glm::mix(center, average / 3.0f, 1.0f - std::exp(-6.3f * dt));
}

void ofApp::draw() {
	ofDisableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	camera.begin();
	ofPushMatrix();
	ofRotateYDeg(runtime.elapsedSeconds() * 10.0f);
	ofTranslate(-center);
	for (auto& shape : shapes) for (auto& trail : shape.trails) trail.mesh.draw();
	if (shaderReady) {
		glEnable(GL_PROGRAM_POINT_SIZE);
		pointShader.begin();
		for (auto& shape : shapes) shape.points.draw();
		pointShader.end();
		glDisable(GL_PROGRAM_POINT_SIZE);
	}
	ofPopMatrix();
	camera.end();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);
	playback.drawStatus();
	runtime.finishFrame(playback.ready && shaderReady, "particle-motion-example", shapes.front().points.getNumVertices());
}

void ofApp::keyPressed(int) { paused = !paused; }
