#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in  vec2 position;
layout(location = 1) in  vec4 color;
layout(push_constant) uniform transform {
	vec4 scale;
	vec4 translate;
} tf;

layout(location = 0) out vec4 fragment;

void main() {
	// Intel Gen8 выполняет умножение и сложение одной командой.
	gl_Position = tf.scale * vec4(position, 0.0, 1.0) + tf.translate;
	fragment = color;
}
