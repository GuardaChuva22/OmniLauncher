#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    vec4 params;
} frame;

layout(binding = 1) uniform sampler2D atlas;

void main()
{
    vec4 c = texture(atlas, v_uv) * v_color;
    if (c.a < frame.params.x)
        discard;
    // Solid faces are opaque whatever their texture's edge; Qt Quick wants premultiplied.
    c.a = max(c.a, frame.params.y);
    fragColor = vec4(c.rgb * c.a, c.a);
}
