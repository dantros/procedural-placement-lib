#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "placement.hpp"
#include "placement_pipeline.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <memory>

template <auto DeleteFunction>
struct Deleter
{
    template <class T>
    void operator() (T* ptr) { DeleteFunction(ptr); }
};

GLuint loadTexture(GladGLContext& gl, const char* path)
{
    GLuint texture;
    glm::ivec2 texture_size;
    int channels;
    std::unique_ptr<stbi_uc[], Deleter<stbi_image_free>> texture_data
            {stbi_load(path, &texture_size.x, &texture_size.y, &channels, 0)};

    REQUIRE(texture_data);

    gl.GenTextures(1, &texture);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    const GLenum formats[]{GL_RED, GL_RG, GL_RGB, GL_RGBA};
    const GLenum format = formats[channels - 1];
    gl.TexImage2D(GL_TEXTURE_2D, 0, format, texture_size.x, texture_size.y, 0, format, GL_UNSIGNED_BYTE,
                  texture_data.get());
    gl.GenerateMipmap(GL_TEXTURE_2D);

    return texture;
}

TEST_CASE("Simple placement", "[simple][placement]")
{
    struct GLFWInitGuard
    {
        GLFWInitGuard() { glfwInit(); }
        ~GLFWInitGuard() { glfwTerminate(); }
    } glfw_init_guard;

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    std::unique_ptr<GLFWwindow, Deleter<glfwDestroyWindow>> window{glfwCreateWindow(32, 32, "TEST", nullptr, nullptr)};
    REQUIRE(window);
    glfwMakeContextCurrent(window.get());

    GladGLContext gl;
    REQUIRE(gladLoadGLContext(&gl, glfwGetProcAddress));

    placement::loadGL(glfwGetProcAddress);

    const GLuint white_texture = loadTexture(gl, "assets/white.png");
    const GLuint black_texture = loadTexture(gl, "assets/black.png");

    SECTION("Simple implementation")
    {
        placement::WorldData world_data{black_texture, white_texture};

        SECTION("Placement with < 0 area should return an empty vector")
        {
            auto points = placement::computePlacement(world_data, 1.0f, {0.0f, 0.0f}, {-1.0f, -1.0f});
            CHECK(points.empty());

            points = placement::computePlacement(world_data, 1.0f, {0.0f, 0.0f}, {10.0f, -1.0f});
            CHECK(points.empty());

            points = placement::computePlacement(world_data, 1.0f, {0.0f, 0.0f}, {-1.0f, 10.0f});
            CHECK(points.empty());
        }

        SECTION("Placement with space for a single object")
        {
            auto points = placement::computePlacement(world_data, 0.5f, {0.0f, 0.0f}, {1.0f, 1.0f});
            CHECK(points.size() == 1);

            points = placement::computePlacement(world_data, 0.5f, {1.5f, 1.5f}, {2.5f, 2.5f});
            CHECK(points.size() == 1);
        }

        SECTION("Full area placement")
        {
            constexpr float footprint = 0.5f;
            auto points = placement::computePlacement(world_data, footprint, {0.0f, 0.0f},
                                                      {world_data.scale.x + footprint, world_data.scale.z + footprint});
            CHECK(points.size() == 100);
        }
    }

    SECTION("Pipeline Implementation")
    {
        placement::PlacementPipeline pipeline;
        const glm::vec3 world_scale = {10.0f, 1.0f, 10.0f};
        pipeline.setWorldScale(world_scale);
        pipeline.setHeightTexture(black_texture);
        pipeline.setDensityTexture(white_texture);

        SECTION("Placement with < 0 area should return an empty vector")
        {
            auto points = pipeline.computePlacement(1.0f, {0.0f, 0.0f}, {-1.0f, -1.0f});
            CHECK(points.empty());

            points = pipeline.computePlacement(1.0f, {0.0f, 0.0f}, {10.0f, -1.0f});
            CHECK(points.empty());

            points = pipeline.computePlacement(1.0f, {0.0f, 0.0f}, {-1.0f, 10.0f});
            CHECK(points.empty());
        }

        SECTION("Placement with space for a single object")
        {
            auto points = pipeline.computePlacement(0.5f, {0.0f, 0.0f}, {1.0f, 1.0f});
            CHECK(points.size() == 1);

            points = pipeline.computePlacement(0.5f, {1.5f, 1.5f}, {2.5f, 2.5f});
            CHECK(points.size() == 1);
        }

        SECTION("Full area placement")
        {
            constexpr float footprint = 0.5f;
            auto points = pipeline.computePlacement(footprint, {0.0f, 0.0f}, {world_scale.x + footprint, world_scale.z + footprint});
            CHECK(points.size() == 100);
        }
    }
}