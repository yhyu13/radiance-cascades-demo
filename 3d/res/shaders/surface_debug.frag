#version 430 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSurfaceAtlas;
uniform int uDebugMode;

void main() {
    vec4 c = texture(uSurfaceAtlas, vUV);

    // Reserved/invalid atlas area: dark checker so chart bounds are visible.
    if (c.a <= 0.0) {
        vec2 grid = floor(vUV * vec2(64.0, 32.0));
        float checker = mod(grid.x + grid.y, 2.0);
        fragColor = vec4(vec3(0.025 + checker * 0.015), 1.0);
        return;
    }

    fragColor = vec4(c.rgb, 1.0);
}
