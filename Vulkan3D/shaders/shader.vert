#version 460

// Inputs from your C++ vertex buffer (Layout Locations)
layout(location = 0) in vec3 inPosition; // Pos3
layout(location = 1) in vec2 inUV;    // UV
layout(location = 2) in vec3 inNormal; // Normal

// Output passing to the fragment shader
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNor;

layout ( push_constant ) uniform MeshPushConstants {
    mat4 model_matrix;
};

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 VPMatrix;
}; 

void main() {
    // Pass the color straight through

     mat3 normalMatrix = transpose(inverse(mat3(model_matrix)));

    // Set the final vertex position (w component is 1.0 for 3D coordinates)
    gl_Position = VPMatrix * model_matrix * vec4(inPosition, 1.0);
    fragUV = inUV;
    fragNor = normalize(normalMatrix * inNormal);;
}
