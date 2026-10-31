#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    vec4 params; // x: alpha below which a texel is cut out
} frame;

void main()
{
    v_uv = texcoord;
    v_color = color;
    gl_Position = frame.mvp * vec4(position, 1.0);
}
