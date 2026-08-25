#version 460

// Input received from the vertex shader (must match location)
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNor;

// Final output color to the framebuffer / swapchain
layout(location = 0) out vec4 outColor;

layout(set =1, binding = 0) uniform sampler2D myTexture;

layout(set = 1, binding = 1) uniform uvScaleUniform {
    vec2 uvScale;
};

vec3 lightDir = vec3(0.2f,0.5f,0.4f);
float ambient = 0.1f;

void main() {
    // Output the color (alpha/opacity set to 1.0 for fully opaque)

    vec3 lightDirNormalize = normalize(lightDir);
    float diff = max(dot(fragNor, lightDirNormalize), 0.0);
    vec3 lightingColor = vec3(diff + ambient);

    vec4 textureColor = texture(myTexture,fragUV * uvScale);

    vec3 finalColor = pow(textureColor.rgb * lightingColor,vec3(1.0 / 2.2));

    outColor = vec4(finalColor,1.0f);
}
