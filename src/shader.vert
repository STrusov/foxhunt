#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in  vec2 position;
layout(location = 1) in  vec4 color;
layout(location = 0) out vec4 fragment;

void main() {
	gl_Position = vec4(position, 0.0, 1.0);
	fragment = color;
}
