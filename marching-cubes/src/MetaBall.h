#pragma once

#include "ofMain.h"

struct MetaBall {
	glm::vec3 position{0};
	glm::vec3 velocity{0};
	float size = 1.4f;

	void follow(const glm::vec3& target, float deltaSeconds) {
		const float steps = std::min(deltaSeconds * 60.0f, 4.0f);
		velocity += (target - position) * 0.3f * steps;
		velocity *= std::pow(0.94f, steps);
		position += velocity * steps;
	}
};
