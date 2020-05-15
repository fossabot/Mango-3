#version 430 core

layout (location = 0) out vec4 gbuffer_c0; // base_color / reflection_color (rgba8)
layout (location = 1) out vec4 gbuffer_c1; // normal (rgb10)
layout (location = 2) out vec4 gbuffer_c2; // emissive (rgb8) and something else
layout (location = 3) out vec4 gbuffer_c3; // roughness (r8), metallic (g8) and something else

in vec3 shared_normal;
in vec2 shared_texcoord;
in vec3 shared_tangent;
in vec3 shared_bitangent;

layout (location = 0, binding = 0) uniform sampler2D t_base_color;
layout (location = 1, binding = 1) uniform sampler2D t_roughness_metallic;
layout (location = 2, binding = 2) uniform sampler2D t_normal;
layout (location = 3, binding = 3) uniform sampler2D t_emissive_color;

layout(binding = 1, std140) uniform scene_material_uniforms
{
    //    name                       // offset // size
    vec4  base_color;                // 0 // 16
    vec3  emissive_color;            // 16 // 16
    float metallic;                  // 32 // 4
    float roughness;                 // 36 // 4

    bool base_color_texture;         // 40 // 4
    bool roughness_metallic_texture; // 44 // 4
    bool normal_texture;             // 48 // 4
    bool emissive_color_texture;     // 52 // 4
    // padding 4
    // padding 4
    // size : 64
};

vec4 get_base_color()
{
    return base_color_texture ? texture(t_base_color, shared_texcoord) : base_color;
}

vec3 get_emissive()
{
    return emissive_color_texture ? texture(t_emissive_color, shared_texcoord).rgb : emissive_color;
}

vec2 get_roughness_and_metallic()
{
    return roughness_metallic_texture ? texture(t_roughness_metallic, shared_texcoord).gb : vec2(roughness, metallic);
}

vec3 get_normal()
{
    vec3 normal = normalize(shared_normal);
    if(normal_texture)
    {
        mat3 tbn = mat3(normalize(shared_tangent), normalize(shared_bitangent), normal);
        vec3 mapped_normal = normalize(texture(t_normal, shared_texcoord).rgb * 2.0 - 1.0);
        normal = normalize(tbn * mapped_normal.rgb);
    }
    return normal * 0.5 + 0.5;
}

void main()
{
    gbuffer_c0 = vec4(get_base_color());
    gbuffer_c1 = vec4(get_normal(), 0.0);
    gbuffer_c2 = vec4(get_emissive(), 0.0);
    gbuffer_c3 = vec4(get_roughness_and_metallic(), 0.0, 0.0);
}
