#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in  vec4 fragment;
layout(location = 0) out vec4 color;

void main() {
    color = fragment;
}
