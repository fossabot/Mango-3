//! \file      ibl_step.cpp
//! \author    Paul Himmler
//! \version   1.0
//! \date      2020
//! \copyright Apache License 2.0

#include <graphics/buffer.hpp>
#include <graphics/shader.hpp>
#include <graphics/shader_program.hpp>
#include <graphics/texture.hpp>
#include <graphics/vertex_array.hpp>
#include <imgui.h>
#include <mango/profile.hpp>
#include <mango/render_system.hpp>
#include <mango/scene_ecs.hpp>
#include <rendering/steps/ibl_step.hpp>
#include <util/helpers.hpp>

using namespace mango;

static const float cubemap_vertices[36] = { -1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
                                            -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f, -1.0f, 1.0f };

static const uint8 cubemap_indices[18] = { 8, 9, 0, 2, 1, 3, 3, 2, 5, 4, 7, 6, 6, 0, 7, 1, 10, 11 };

//! \brief Default texture that is bound to every texture unit not in use to prevent warnings.
texture_ptr default_ibl_texture;

bool ibl_step::create()
{
    PROFILE_ZONE;
    // compute shader to convert from equirectangular projected hdr textures to a cube map.
    shader_configuration shader_config;
    shader_config.m_path       = "res/shader/c_equi_to_cubemap.glsl";
    shader_config.m_type       = shader_type::COMPUTE_SHADER;
    shader_ptr to_cube_compute = shader::create(shader_config);
    if (!check_creation(to_cube_compute.get(), "cubemap compute shader", "Ibl Step"))
        return false;

    m_equi_to_cubemap = shader_program::create_compute_pipeline(to_cube_compute);
    if (!check_creation(m_equi_to_cubemap.get(), "cubemap compute shader program", "Ibl Step"))
        return false;

    // compute shader to build the irradiance cubemap for image based lighting.
    shader_config.m_path              = "res/shader/c_irradiance_map.glsl";
    shader_config.m_type              = shader_type::COMPUTE_SHADER;
    shader_ptr irradiance_map_compute = shader::create(shader_config);
    if (!check_creation(irradiance_map_compute.get(), "irradiance map compute shader", "Ibl Step"))
        return false;

    m_build_irradiance_map = shader_program::create_compute_pipeline(irradiance_map_compute);
    if (!check_creation(m_build_irradiance_map.get(), "irradiance map compute shader program", "Ibl Step"))
        return false;

    // compute shader to build the prefiltered specular cubemap for image based lighting.
    shader_config.m_path                        = "res/shader/c_prefilter_specular_map.glsl";
    shader_config.m_type                        = shader_type::COMPUTE_SHADER;
    shader_ptr specular_prefiltered_map_compute = shader::create(shader_config);
    if (!check_creation(specular_prefiltered_map_compute.get(), "prefilter specular ibl compute shader", "Ibl Step"))
        return false;

    m_build_specular_prefiltered_map = shader_program::create_compute_pipeline(specular_prefiltered_map_compute);
    if (!check_creation(m_build_specular_prefiltered_map.get(), "prefilter specular ibl compute shader program", "Ibl Step"))
        return false;

    // compute shader to build the brdf integration lookup texture for image based lighting. Could be done only once.
    shader_config.m_path                = "res/shader/c_brdf_integration.glsl";
    shader_config.m_type                = shader_type::COMPUTE_SHADER;
    shader_ptr brdf_integration_compute = shader::create(shader_config);
    if (!check_creation(brdf_integration_compute.get(), "ibl brdf integration compute shader", "Ibl Step"))
        return false;

    m_build_integration_lut = shader_program::create_compute_pipeline(brdf_integration_compute);
    if (!check_creation(m_build_integration_lut.get(), "ibl brdf integration compute shader program", "Ibl Step"))
        return false;

    // cubemap rendering
    shader_config.m_path      = "res/shader/v_cubemap.glsl";
    shader_config.m_type      = shader_type::VERTEX_SHADER;
    shader_ptr cubemap_vertex = shader::create(shader_config);
    if (!check_creation(cubemap_vertex.get(), "cubemap vertex shader", "Ibl Step"))
        return false;

    shader_config.m_path        = "res/shader/f_cubemap.glsl";
    shader_config.m_type        = shader_type::FRAGMENT_SHADER;
    shader_ptr cubemap_fragment = shader::create(shader_config);
    if (!check_creation(cubemap_fragment.get(), "cubemap fragment shader", "Ibl Step"))
        return false;

    m_draw_environment = shader_program::create_graphics_pipeline(cubemap_vertex, nullptr, nullptr, nullptr, cubemap_fragment);
    if (!check_creation(m_draw_environment.get(), "cubemap rendering shader program", "Ibl Step"))
        return false;

    m_cube_geometry = vertex_array::create();
    if (!check_creation(m_cube_geometry.get(), "cubemap geometry vertex array", "Ibl Step"))
        return false;

    buffer_configuration b_config;
    b_config.m_access   = buffer_access::NONE;
    b_config.m_size     = sizeof(cubemap_vertices);
    b_config.m_target   = buffer_target::VERTEX_BUFFER;
    const void* vb_data = static_cast<const void*>(cubemap_vertices);
    b_config.m_data     = vb_data;
    buffer_ptr vb       = buffer::create(b_config);

    m_cube_geometry->bind_vertex_buffer(0, vb, 0, sizeof(float) * 3);
    m_cube_geometry->set_vertex_attribute(0, 0, format::RGB32F, 0);

    b_config.m_size     = sizeof(cubemap_indices);
    b_config.m_target   = buffer_target::INDEX_BUFFER;
    const void* ib_data = static_cast<const void*>(cubemap_indices);
    b_config.m_data     = ib_data;
    buffer_ptr ib       = buffer::create(b_config);

    m_cube_geometry->bind_index_buffer(ib);

    m_ibl_data.current_rotation_scale = glm::mat3(1.0f);
    m_ibl_data.render_level           = 0.0f;

    texture_configuration texture_config;
    texture_config.m_generate_mipmaps        = 1;
    texture_config.m_is_standard_color_space = false;
    texture_config.m_is_cubemap              = false;
    texture_config.m_texture_min_filter      = texture_parameter::FILTER_LINEAR;
    texture_config.m_texture_mag_filter      = texture_parameter::FILTER_LINEAR;
    texture_config.m_texture_wrap_s          = texture_parameter::WRAP_CLAMP_TO_EDGE;
    texture_config.m_texture_wrap_t          = texture_parameter::WRAP_CLAMP_TO_EDGE;
    m_brdf_integration_lut                   = texture::create(texture_config);
    m_brdf_integration_lut->set_data(format::RGBA16F, m_integration_lut_width, m_integration_lut_height, format::RGBA, format::FLOAT, nullptr);

    // creating a temporal command buffer for compute shader execution.
    command_buffer_ptr compute_commands = command_buffer::create();

    // build integration look up texture
    compute_commands->bind_shader_program(m_build_integration_lut);

    // bind output lut
    compute_commands->bind_image_texture(0, m_brdf_integration_lut, 0, false, 0, base_access::WRITE_ONLY, format::RGBA16F);
    // bind uniforms
    glm::vec2 out = glm::vec2(m_brdf_integration_lut->get_width(), m_brdf_integration_lut->get_height());
    compute_commands->bind_single_uniform(0, &(out), sizeof(out));
    // execute compute
    compute_commands->dispatch_compute(m_brdf_integration_lut->get_width() / 8, m_brdf_integration_lut->get_height() / 8, 1);

    compute_commands->add_memory_barrier(memory_barrier_bit::SHADER_IMAGE_ACCESS_BARRIER_BIT);

    compute_commands->bind_shader_program(nullptr);

    {
        GL_NAMED_PROFILE_ZONE("Generating brdf lookup");
        compute_commands->execute();
    }

    // default texture needed
    texture_config.m_texture_min_filter = texture_parameter::FILTER_NEAREST;
    texture_config.m_texture_mag_filter = texture_parameter::FILTER_NEAREST;
    texture_config.m_is_cubemap         = true;
    default_ibl_texture                 = texture::create(texture_config);
    if (!check_creation(default_ibl_texture.get(), "default ibl texture", "Ibl Step"))
        return false;

    g_ubyte albedo[3] = { 127, 127, 127 };
    default_ibl_texture->set_data(format::RGB8, 1, 1, format::RGB, format::UNSIGNED_BYTE, albedo);

    return true;
}

void ibl_step::update(float dt)
{
    MANGO_UNUSED(dt);
}

void ibl_step::attach() {}

void ibl_step::configure(const ibl_step_configuration& configuration)
{
    m_ibl_data.render_level = configuration.get_render_level();
    MANGO_ASSERT(m_ibl_data.render_level > 0.0f && m_ibl_data.render_level < 8.1f, "Shadow Map Resolution has to be between 0.0 and 8.0f!");
}

void ibl_step::execute(command_buffer_ptr& command_buffer, uniform_buffer_ptr frame_uniform_buffer)
{
    PROFILE_ZONE;
    if (m_ibl_data.render_level < 0.0f)
        return;

    command_buffer->bind_shader_program(m_draw_environment);
    command_buffer->bind_vertex_array(m_cube_geometry);
    bind_uniform_buffer_cmd cmd = frame_uniform_buffer->bind_uniform_buffer(UB_SLOT_IBL_DATA, sizeof(ibl_data), &m_ibl_data);
    command_buffer->submit<bind_uniform_buffer_cmd>(cmd);
    if (m_cubemap)
        command_buffer->bind_texture(0, m_prefiltered_specular, 0);
    else
        command_buffer->bind_texture(0, default_ibl_texture, 0);

    command_buffer->draw_elements(primitive_topology::TRIANGLE_STRIP, 0, 18, index_type::UBYTE);

    command_buffer->bind_vertex_array(nullptr);
    command_buffer->bind_shader_program(nullptr);
}

void ibl_step::destroy() {}

void ibl_step::load_from_hdr(const texture_ptr& hdr_texture)
{
    PROFILE_ZONE;
    if (!hdr_texture)
    {
        m_cubemap              = nullptr;
        m_irradiance_map       = nullptr;
        m_prefiltered_specular = nullptr;
        return;
    }
    texture_configuration texture_config;
    texture_config.m_generate_mipmaps        = calculate_mip_count(m_cube_width, m_cube_height);
    texture_config.m_is_standard_color_space = false;
    texture_config.m_is_cubemap              = true;
    texture_config.m_texture_min_filter      = texture_parameter::FILTER_LINEAR_MIPMAP_LINEAR;
    texture_config.m_texture_mag_filter      = texture_parameter::FILTER_LINEAR;
    texture_config.m_texture_wrap_s          = texture_parameter::WRAP_CLAMP_TO_EDGE;
    texture_config.m_texture_wrap_t          = texture_parameter::WRAP_CLAMP_TO_EDGE;

    if (m_cubemap)
        m_cubemap->release();
    m_cubemap = texture::create(texture_config);
    m_cubemap->set_data(format::RGBA16F, m_cube_width, m_cube_height, format::RGBA, format::FLOAT, nullptr);

    texture_config.m_generate_mipmaps = calculate_mip_count(m_prefiltered_base_width, m_prefiltered_base_height);
    if (m_prefiltered_specular)
        m_prefiltered_specular->release();
    m_prefiltered_specular = texture::create(texture_config);
    m_prefiltered_specular->set_data(format::RGBA16F, m_prefiltered_base_width, m_prefiltered_base_height, format::RGBA, format::FLOAT, nullptr);

    texture_config.m_generate_mipmaps   = 1;
    texture_config.m_texture_min_filter = texture_parameter::FILTER_LINEAR;
    if (m_irradiance_map)
        m_irradiance_map->release();
    m_irradiance_map = texture::create(texture_config);
    m_irradiance_map->set_data(format::RGBA16F, m_irradiance_width, m_irradiance_height, format::RGBA, format::FLOAT, nullptr);

    glm::vec2 out;

    // creating a temporal command buffer for compute shader execution.
    command_buffer_ptr compute_commands = command_buffer::create();

    // equirectangular to cubemap
    compute_commands->bind_shader_program(m_equi_to_cubemap);

    // bind input hdr texture
    compute_commands->bind_texture(0, hdr_texture, 0);
    // bind output cubemap
    compute_commands->bind_image_texture(1, m_cubemap, 0, true, 0, base_access::WRITE_ONLY, format::RGBA16F);
    // bind uniforms
    out = glm::vec2(m_cubemap->get_width(), m_cubemap->get_height());
    compute_commands->bind_single_uniform(1, &(out), sizeof(out));
    // execute compute
    compute_commands->dispatch_compute(m_cubemap->get_width() / 32, m_cubemap->get_height() / 32, 6);

    // We need to recalculate mipmaps
    compute_commands->calculate_mipmaps(m_cubemap);

    compute_commands->add_memory_barrier(memory_barrier_bit::SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // build irradiance map
    compute_commands->bind_shader_program(m_build_irradiance_map);

    // bind input cubemap
    compute_commands->bind_texture(0, m_cubemap, 0);
    // bind output irradiance map
    compute_commands->bind_image_texture(1, m_irradiance_map, 0, true, 0, base_access::WRITE_ONLY, format::RGBA16F);
    // bind uniforms
    out = glm::vec2(m_irradiance_map->get_width(), m_irradiance_map->get_height());
    compute_commands->bind_single_uniform(1, &(out), sizeof(out));
    // execute compute
    compute_commands->dispatch_compute(m_irradiance_map->get_width() / 32, m_irradiance_map->get_height() / 32, 6);

    compute_commands->add_memory_barrier(memory_barrier_bit::SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // build prefiltered specular mipchain
    compute_commands->bind_shader_program(m_build_specular_prefiltered_map);
    // bind input cubemap
    compute_commands->bind_texture(0, m_cubemap, 0);
    uint32 mip_count = m_prefiltered_specular->mipmaps();
    for (uint32 mip = 0; mip < mip_count; ++mip)
    {
        const uint32 mipmap_width  = m_prefiltered_base_width >> mip;
        const uint32 mipmap_height = m_prefiltered_base_height >> mip;
        float roughness            = (float)mip / (float)(mip_count - 1);

        // bind correct mipmap
        compute_commands->bind_image_texture(1, m_prefiltered_specular, static_cast<g_int>(mip), true, 0, base_access::WRITE_ONLY, format::RGBA16F);

        out = glm::vec2(mipmap_width, mipmap_height);
        compute_commands->bind_single_uniform(1, &(out), sizeof(out));
        compute_commands->bind_single_uniform(2, &(roughness), sizeof(roughness));

        compute_commands->dispatch_compute(m_prefiltered_specular->get_width() / 32, m_prefiltered_specular->get_height() / 32, 6);
    }

    compute_commands->bind_shader_program(nullptr);

    {
        GL_NAMED_PROFILE_ZONE("Generating IBL");
        compute_commands->execute();
    }
}

void ibl_step::bind_image_based_light_maps(command_buffer_ptr& command_buffer)
{
    PROFILE_ZONE;
    if (m_cubemap)
    {
        command_buffer->bind_texture(5, m_irradiance_map, 5);       // TODO Paul: Binding and location...
        command_buffer->bind_texture(6, m_prefiltered_specular, 6); // TODO Paul: Binding and location...
        command_buffer->bind_texture(7, m_brdf_integration_lut, 7); // TODO Paul: Binding and location...
    }
    else
    {
        command_buffer->bind_texture(5, default_ibl_texture, 5);    // TODO Paul: Binding and location...
        command_buffer->bind_texture(6, default_ibl_texture, 6);    // TODO Paul: Binding and location...
        command_buffer->bind_texture(7, m_brdf_integration_lut, 7); // TODO Paul: Binding and location...
    }
}

void ibl_step::on_ui_widget()
{
    // Render Level 0.0 - 8.0
    bool should_render = !(m_ibl_data.render_level < -1e-5f);
    static float tmp   = 0.0f;
    ImGui::Checkbox("Render IBL Visualization##ibl_step", &should_render);
    if (!should_render)
    {
        m_ibl_data.render_level = -1.0f;
    }
    else
    {
        m_ibl_data.render_level = tmp;
        ImGui::SliderFloat("Blur Level##ibl_step", &static_cast<float>(m_ibl_data.render_level), 0.0f, 8.0f);
        tmp = m_ibl_data.render_level;
    }
}
