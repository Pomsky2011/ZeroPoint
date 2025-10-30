#version 450

// Fullscreen quad without vertex buffer
// Outputs: position and UV coordinates
layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate fullscreen quad from vertex ID
    // Vertices: 0,1,2 = first triangle; 3,4,5 = second triangle
    vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),  // Bottom-left
        vec2( 1.0, -1.0),  // Bottom-right
        vec2(-1.0,  1.0),  // Top-left
        vec2(-1.0,  1.0),  // Top-left
        vec2( 1.0, -1.0),  // Bottom-right
        vec2( 1.0,  1.0)   // Top-right
    );

    vec2 texCoords[6] = vec2[](
        vec2(0.0, 0.0),  // Bottom-left
        vec2(1.0, 0.0),  // Bottom-right
        vec2(0.0, 1.0),  // Top-left
        vec2(0.0, 1.0),  // Top-left
        vec2(1.0, 0.0),  // Bottom-right
        vec2(1.0, 1.0)   // Top-right
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = texCoords[gl_VertexIndex];
}
