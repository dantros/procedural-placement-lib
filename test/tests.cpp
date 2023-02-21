#include "placement/placement.hpp"
#include "placement/placement_pipeline.hpp"
#include "../src/disk_distribution_generator.hpp"

#include "glutils/debug.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <memory>
#include <ostream>
#include <algorithm>

// included here to make it available to catch.hpp
#include "ostream_operators.hpp"
#include "catch.hpp"

using namespace placement;

GladGLContext gl;

class TextureLoader
{
public:
    ~TextureLoader()
    {
        clear();
    }

    GLuint load(const char *filename)
    {
        const GLuint new_tex = s_loadTexture(filename);
        m_loaded_textures[filename] = new_tex;
        return new_tex;
    }

    GLuint load(const std::string &filename)
    {
        return load(filename.c_str());
    }

    GLuint get(const char *filename) const
    {
        const auto it = m_loaded_textures.find(filename);
        if (it == m_loaded_textures.end())
            throw std::runtime_error("no loaded texture with given filename");
        return it->second;
    }

    GLuint get(const std::string &filename) const
    {
        return get(filename.c_str());
    }

    GLuint operator[](const std::string &filename)
    {
        return operator[](filename.c_str());
    }

    GLuint operator[](const char *filename)
    {
        const auto it = m_loaded_textures.find(filename);
        if (it == m_loaded_textures.end())
            return load(filename);
        return it->second;
    }

    void unload(const char *filename)
    {
        const auto it = m_loaded_textures.find(filename);
        if (it != m_loaded_textures.end())
        {
            gl.DeleteTextures(1, &it->second);
            m_loaded_textures.erase(it);
        }
    }

    void unload(const std::string &filename)
    { unload(filename.c_str()); }

    void clear()
    {
        if (m_loaded_textures.empty())
            return;

        std::vector<GLuint> names;
        names.reserve(m_loaded_textures.size());
        for (const auto &pair: m_loaded_textures)
            names.emplace_back(pair.second);
        m_loaded_textures.clear();
        gl.DeleteTextures(names.size(), names.data());
    }

private:
    std::map<std::string, GLuint> m_loaded_textures;

    static GLuint s_loadTexture(const char *filename)
    {
        GLuint texture;
        glm::ivec2 texture_size;
        int channels;
        std::unique_ptr<stbi_uc[]> texture_data{stbi_load(filename, &texture_size.x, &texture_size.y, &channels, 0)};

        if (!texture_data)
            throw std::runtime_error(stbi_failure_reason());

        gl.GenTextures(1, &texture);
        gl.BindTexture(GL_TEXTURE_2D, texture);
        const GLenum formats[]{GL_RED, GL_RG, GL_RGB, GL_RGBA};
        const GLenum format = formats[channels - 1];
        gl.TexImage2D(GL_TEXTURE_2D, 0, format, texture_size.x, texture_size.y, 0, format, GL_UNSIGNED_BYTE,
                      texture_data.get());
        gl.GenerateMipmap(GL_TEXTURE_2D);

        return texture;
    }
};

TextureLoader s_texture_loader;

class ContextInitializer : public Catch::TestEventListenerBase
{
public:
    using Catch::TestEventListenerBase::TestEventListenerBase;

    void testRunStarting(const Catch::TestRunInfo &) override
    {
        if (!glfwInit())
        {
            const char *msg = nullptr;
            glfwGetError(&msg);
            throw std::runtime_error(msg);
        }

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

        m_window = glfwCreateWindow(1, 1, "TEST", nullptr, nullptr);
        if (!m_window)
        {
            const char *msg = nullptr;
            glfwGetError(&msg);
            throw std::runtime_error(msg);
        }
        glfwMakeContextCurrent(m_window);

        if (!gladLoadGLContext(&gl, glfwGetProcAddress) or !placement::loadGLContext(glfwGetProcAddress))
            throw std::runtime_error("OpenGL context loading failed");

        gl.DebugMessageCallback(s_glDebugCallback, nullptr);
        gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }

    void testRunEnded(const Catch::TestRunStats &) override
    {
        s_texture_loader.clear();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    void sectionEnded(const Catch::SectionStats &) override
    {
        gl.Finish();
    }

private:

    GLFWwindow *m_window{nullptr};

    static void s_glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                  const GLchar *message, const void *user_ptr)
    {
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            return;

        UNSCOPED_INFO("[GL DEBUG MESSAGE " << id << "] " << message);
    }
};

CATCH_REGISTER_LISTENER(ContextInitializer)

template<typename Vec>
bool vecOrder(const Vec &l, const Vec &r)
{
    if constexpr (Vec::length() == 1)
        return std::make_tuple(l.x) < std::make_tuple(r.x);

    if constexpr (Vec::length() == 2)
        return std::make_tuple(l.x, l.y) < std::make_tuple(r.x, r.y);

    if constexpr (Vec::length() == 3)
        return std::make_tuple(l.x, l.y, l.z) < std::make_tuple(r.x, r.y, r.z);

    if constexpr (Vec::length() == 4)
        return std::make_tuple(l.x, l.y, l.z, l.w) < std::make_tuple(r.x, r.y, r.z, r.w);
};

bool elementCompare(const placement::Result::Element &l, const placement::Result::Element &r)
{
    return std::make_tuple(l.class_index, l.position.x, l.position.y, l.position.z) <
           std::make_tuple(r.class_index, r.position.x, r.position.y, r.position.z);
};

namespace placement {
bool operator==(const Result::Element &l, const Result::Element &r)
{ return l.position == r.position && l.class_index == r.class_index; }
} // placement

TEST_CASE("PlacementPipeline", "[pipeline]")
{
    using Element = placement::Result::Element;

    placement::PlacementPipeline pipeline;

    placement::WorldData world_data{{10.0f, 1.0f, 10.0f}, s_texture_loader["assets/black.png"]};
    placement::LayerData layer_data{1.0f, {{s_texture_loader["assets/white.png"]}}};

    SECTION("Placement with < 0 area should return an empty vector")
    {
        auto result = pipeline.computePlacement(world_data, layer_data, {0.0f, 0.0f}, {-1.0f, -1.0f}).readResult();
        CHECK(result.getNumClasses() == 1);
        CHECK(result.getElementArrayLength() == 0);

        auto points = result.copyAllToHost();
        CHECK(points.empty());

        result = pipeline.computePlacement(world_data, layer_data, {0.0f, 0.0f}, {10.0f, -1.0f}).readResult();
        CHECK(result.getNumClasses() == 1);
        CHECK(result.getElementArrayLength() == 0);

        points = result.copyAllToHost();
        CHECK(points.empty());

        result = pipeline.computePlacement(world_data, layer_data, {0.0f, 0.0f}, {-1.0f, 10.0f}).readResult();
        CHECK(result.getNumClasses() == 1);
        CHECK(result.getElementArrayLength() == 0);

        points = result.copyAllToHost();
        REQUIRE(!points.empty());
    }

    SECTION("Determinism (simple)")
    {
        world_data.scale = {1.0f, 1.0f, 1.0f};

        auto positions_0 = pipeline.computePlacement(world_data, layer_data, glm::vec2(0.f), glm::vec2(1.f))
                                   .readResult()
                                   .copyAllToHost();
        auto positions_1 = pipeline.computePlacement(world_data, layer_data, glm::vec2(0.f), glm::vec2(1.f))
                                   .readResult()
                                   .copyAllToHost();

        CHECK(!positions_0.empty());
        CHECK(!positions_1.empty());

        {
            CAPTURE(positions_0, positions_1);
            REQUIRE(positions_0.size() == positions_1.size());
        }

        std::sort(positions_0.begin(), positions_0.end(), elementCompare);
        std::sort(positions_1.begin(), positions_1.end(), elementCompare);

        std::vector<placement::Result::Element> diff;
        diff.resize(positions_0.size());

        const auto diff_end = std::set_symmetric_difference(positions_0.begin(), positions_0.end(),
                                                            positions_1.begin(), positions_1.end(),
                                                            diff.begin(), elementCompare);
        diff.erase(diff_end, diff.end());
        CAPTURE(diff);
        CHECK(diff.empty());
    }

    const float footprint = GENERATE(take(3, random(0.01f, 0.1f)));
    INFO("footprint = " << footprint);

    layer_data.footprint = footprint;

    const float boundary_offset_x = GENERATE(take(3, random(0.f, 0.4f)));
    const float boundary_offset_y = GENERATE(take(3, random(0.f, 0.4f)));
    const glm::vec2 lower_bound(boundary_offset_x, boundary_offset_y);

    INFO("lower_bound = " << lower_bound);

    const float boundary_size_x = GENERATE(take(3, random(0.6f, 1.0f)));
    const float boundary_size_y = GENERATE(take(3, random(0.6f, 1.0f)));
    const glm::vec2 upper_bound = lower_bound + glm::vec2(boundary_size_x, boundary_size_y);

    INFO("upper_bound = " << upper_bound);

    SECTION("Determinism")
    {
        auto compute_placement = [&]()
        {
            auto positions = pipeline.computePlacement(world_data, layer_data, lower_bound, upper_bound)
                                     .readResult()
                                     .copyAllToHost();
            std::sort(positions.begin(), positions.end(), elementCompare);
            return positions;
        };

        const auto result_0 = compute_placement();
        CAPTURE(result_0);
        CHECK(!result_0.empty());

        const auto result_1 = compute_placement();
        CAPTURE(result_1);
        CHECK(!result_1.empty());

        auto compute_diff = [](const std::vector<Element> &l, const std::vector<Element> &r)
        {
            std::vector<Element> diff;
            diff.resize(std::max(l.size(), r.size()));
            const auto diff_end = std::set_symmetric_difference(l.begin(), l.end(), r.begin(), r.end(),
                                                                diff.begin(), elementCompare);
            diff.erase(diff_end, diff.end());
            return diff;
        };

        const auto diff_01 = compute_diff(result_0, result_1);
        CAPTURE(diff_01);
        CHECK(diff_01.empty());

        const auto result_2 = compute_placement();
        CAPTURE(result_2);
        CHECK(!result_2.empty());

        const auto diff_02 = compute_diff(result_0, result_2);
        CAPTURE(diff_02);
        CHECK(diff_02.empty());
    }

    SECTION("Boundary and separation")
    {
        const auto elements = pipeline.computePlacement(world_data, layer_data, lower_bound, upper_bound)
                                      .readResult()
                                      .copyAllToHost();

        REQUIRE(!elements.empty());

        for (int i = 0; i < elements.size(); i++)
        {
            CAPTURE(i, elements[i].position);
            const glm::vec2 point{elements[i].position.x, elements[i].position.y};
            CHECK(glm::all(glm::greaterThanEqual(point, lower_bound) && glm::lessThan(point, upper_bound)));

            for (int j = 0; j < i; j++)
            {
                CAPTURE(j, elements[j].position);
                CHECK(glm::length(point - glm::vec2(elements[j].position.x, elements[j].position.z)) >=
                      Approx(footprint));
            }
        }
    }

    SECTION("CPU/GPU read")
    {
        const auto results = pipeline.computePlacement(world_data, layer_data, lower_bound, upper_bound).readResult();

        REQUIRE(results.getElementArrayLength() > 0);

        std::vector<Element> gpu_results;
        gpu_results.resize(results.getElementArrayLength());

        {
            GL::Buffer buffer;
            const auto buffer_size = static_cast<GLsizeiptr>(results.getElementArrayLength() * sizeof(Element));
            buffer.allocateImmutable(buffer_size, GL::Buffer::StorageFlags::none);

            results.copyAll(buffer);

            buffer.read(0, buffer_size, gpu_results.data());
        }

        const auto cpu_results = results.copyAllToHost();
        CHECK(cpu_results.size() == results.getElementArrayLength());

        CHECK(gpu_results == cpu_results);
    }
}

TEST_CASE("PlacementPipeline (multiclass)", "[pipeline][multiclass]")
{
    using namespace placement;

    constexpr float footprint = 0.01f;

    PlacementPipeline pipeline;
    WorldData world_data{{1.f, 1.f, 1.f}, s_texture_loader["assets/heightmap.png"]};
    LayerData layer_data{footprint, {{s_texture_loader["assets/densitymaps/linear_gradient.png"], .2},
                                     {s_texture_loader["assets/densitymaps/bilinear_gradient.png"], .2},
                                     {s_texture_loader["assets/densitymaps/radial_gradient.png"], .2},
                                     {s_texture_loader["assets/densitymaps/square_gradient.png"], .2},
                                     {s_texture_loader["assets/densitymaps/cone_gradient.png"], .2}}};

    constexpr std::size_t num_classes = 5;
    REQUIRE(layer_data.densitymaps.size() == num_classes);

    const glm::vec2 lower_bound{0};
    const glm::vec2 upper_bound{1};

    auto results = pipeline.computePlacement(world_data, layer_data, lower_bound, upper_bound).readResult();

    SECTION("Accessors")
    {
        SECTION("Host")
        {
            const auto all_results = results.copyAllToHost();
            REQUIRE(all_results.size() == num_classes);
            CHECK(results.getElementArrayLength() == all_results.size());

            auto begin_iter = all_results.begin();
            std::vector<Result::Element> all_results_subsection;
            for (std::size_t i = 0; i < num_classes; i++)
            {
                const auto class_size = results.getClassElementCount(i);
                all_results_subsection.insert(all_results_subsection.cend(), begin_iter, begin_iter + class_size);
                begin_iter += class_size;

                const auto class_results = results.copyClassToHost(i);
                CHECK(results.getClassElementCount(i) == class_results.size());

                CHECK(class_results == all_results_subsection);
                all_results_subsection.clear();
            }
        }

        SECTION("Device")
        {
            GL::Buffer buffer;
            const auto buffer_size = results.getElementArrayLength() * sizeof(glm::vec4);
            buffer.allocateImmutable(buffer_size, GL::Buffer::StorageFlags::none);

            results.copyAll(buffer);

            std::vector<Result::Element> all_elements;
            all_elements.resize(results.getElementArrayLength());
            buffer.read(0, buffer_size, all_elements.data());

            const auto expected = results.copyAllToHost();

            CHECK(all_elements == expected);
        }
    }

    SECTION("Boundaries and separation")
    {
        const auto elements = results.copyAllToHost();
        std::vector<glm::vec3> parsed;
        parsed.reserve(results.getElementArrayLength());

        for (auto &element: elements)
        {
            const auto position = element.position;
            for (const auto &other_position: parsed)
                REQUIRE(glm::distance(position, other_position) >= Approx(footprint));
            parsed.emplace_back(position);
        }
    }

    SECTION("Determinism")
    {
        auto results_1 = pipeline.computePlacement(world_data, layer_data, lower_bound, upper_bound).readResult();
        auto results_2 = pipeline.computePlacement(world_data, layer_data, lower_bound, upper_bound).readResult();

        const auto positions_0 = results.copyAllToHost();
        const auto positions_1 = results_1.copyAllToHost();
        const auto positions_2 = results_2.copyAllToHost();

        CHECK(positions_0 == positions_1);
        CHECK(positions_0 == positions_2);
    }
}

TEST_CASE("GenerationKernel", "[generation][kernel]")
{
    GenerationKernel kernel;

    SECTION("correctness")
    {
        constexpr auto wg_size = GenerationKernel::work_group_size;
        constexpr glm::vec2 wg_scale{1.0f};

        glm::vec2 position_stencil[wg_size.x][wg_size.y];
        for (auto i = 0u; i < wg_size.x; i++)
            for (auto j = 0u; j < wg_size.y; j++)
                position_stencil[i][j] = glm::vec2(i, j) * wg_scale;

        kernel.setWorkGroupPattern(position_stencil);
        kernel.setWorkGroupScale(wg_scale);
        kernel.setWorkGroupOffset({0, 0});

        constexpr glm::vec3 world_scale{1.0f};
        kernel.setWorldScale(world_scale);

        const auto black_texture = s_texture_loader["assets/black.png"];

        const uint height_texture_unit = 0;
        kernel.setHeightmapTextureUnit(height_texture_unit);
        gl.BindTextureUnit(height_texture_unit, black_texture);

        const auto footprint = GENERATE(take(3, random(0.01f, 0.1f)));
        INFO("footprint=" << footprint);
        kernel.setFootprint(footprint);

        const glm::uvec2 wg_count{world_scale / (glm::vec3(wg_scale, 1.0f) * glm::vec3(wg_size))};

        const std::size_t candidate_count = wg_count.x * wg_count.y * wg_size.x * wg_size.y;

        GL::Buffer buffer;
        const GL::Buffer::Range candidate_range{0, GenerationKernel::getCandidateBufferSizeRequirement({wg_count, 1})};
        const GL::Buffer::Range world_uv_range{candidate_range.offset + candidate_range.size,
                                               GenerationKernel::getWorldUVBufferSizeRequirement({wg_count, 1})};
        const GL::Buffer::Range density_range{world_uv_range.offset + world_uv_range.size,
                                              GenerationKernel::getDensityBufferMemoryRequirement({wg_count, 1})};

        buffer.allocateImmutable(candidate_range.size + world_uv_range.size + density_range.size,
                                 GL::BufferHandle::StorageFlags::map_read);

        constexpr uint candidate_binding_index = 0;
        constexpr uint world_uv_binding_index = 1;
        constexpr uint density_binding_index = 2;

        kernel.setCandidateBufferBindingIndex(candidate_binding_index);
        kernel.setWorldUVBufferBindingIndex(world_uv_binding_index);
        kernel.setDensityBufferBindingIndex(density_binding_index);

        buffer.bindRange(GL::Buffer::IndexedTarget::shader_storage, candidate_binding_index, candidate_range);
        buffer.bindRange(GL::Buffer::IndexedTarget::shader_storage, world_uv_binding_index, world_uv_range);
        buffer.bindRange(GL::Buffer::IndexedTarget::shader_storage, density_binding_index, density_range);

        kernel.useProgram();
        gl.DispatchCompute(wg_count.x, wg_count.y, 1);
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        std::vector<Result::Element> candidates;
        candidates.resize(candidate_count);
        buffer.read(candidate_range, candidates.data());

        std::vector<glm::vec2> world_uvs;
        world_uvs.resize(candidate_count);
        buffer.read(world_uv_range, world_uvs.data());

        std::vector<float> densities;
        densities.resize(candidate_count);
        buffer.read(density_range, densities.data());

        SECTION("correctness")
        {
            constexpr glm::vec2 lower_bound{0};
            constexpr glm::vec2 upper_bound{world_scale};

            for (std::size_t i = 0; i < candidate_count; i++)
            {
                CAPTURE(i);
                CHECK(glm::all(glm::lessThanEqual(candidates[i].position, world_scale)));
                CHECK(candidates[i].class_index == -1u);
                CHECK(glm::all(glm::lessThanEqual(world_uvs[i], glm::vec2(1))));
                CHECK(densities[i] == 1.0f);
            }
        }

        SECTION("determinism")
        {
            gl.DispatchCompute(wg_count.x, wg_count.y, 1);
            gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

            auto candidates_duplicate = candidates;
            auto world_uvs_duplicate = world_uvs;
            auto densities_duplicate = densities;

            buffer.read(candidate_range, candidates_duplicate.data());
            buffer.read(world_uv_range, world_uvs_duplicate.data());
            buffer.read(density_range, densities_duplicate.data());

            CHECK(candidates == candidates_duplicate);
            CHECK(world_uvs == world_uvs_duplicate);
            CHECK(densities == densities_duplicate);
        }
    }
}

TEST_CASE("EvaluationKernel", "[evaluation][kernel]")
{
    const GLsizeiptr wg_count_x = GENERATE(take(3, random(8, 32)));
    const GLsizeiptr wg_count_y = GENERATE(take(3, random(8, 32)));

    const glm::vec2 world_boundaries{10.f};

    const float lower_bound_x = GENERATE(take(1, random(0.f, 1.f)));
    const float lower_bound_y = GENERATE(take(1, random(0.f, 1.f)));
    const float placement_area_x = GENERATE(take(1, random(0.f, 1.f)));
    const float placement_area_y = GENERATE(take(1, random(0.f, 1.f)));

    const glm::vec2 lower_bound{lower_bound_x, lower_bound_y};
    const glm::vec2 upper_bound = lower_bound + glm::vec2{placement_area_x, placement_area_y};

    const GLsizeiptr candidate_count_x = wg_count_x * EvaluationKernel::work_group_size.x;
    const GLsizeiptr candidate_count_y = wg_count_y * EvaluationKernel::work_group_size.y;
    const GLsizeiptr candidate_count = candidate_count_x * candidate_count_y;

    EvaluationKernel kernel;

    kernel.setClassIndex(0);
    kernel.setLowerBound(lower_bound);
    kernel.setUpperBound(upper_bound);

    std::vector<Result::Element> candidates;
    std::vector<glm::vec2> world_uvs;
    std::vector<float> densities;

    for (std::size_t i = 0; i < wg_count_x; i++)
    {
        const float world_u = static_cast<float>(i) / static_cast<float>(candidate_count_x);
        const float position_x = world_u * world_boundaries.x;

        for (std::size_t j = 0; j < wg_count_y; j++)
        {
            const float world_v = static_cast<float>(j) / static_cast<float>(candidate_count_y);
            const float position_y = world_v * world_boundaries.y;

            candidates.push_back({glm::vec3(position_x, position_y, 0.f), 0u});
            world_uvs.emplace_back(world_u, world_v);
            densities.emplace_back(0.0f);
        }
    }

    constexpr GLsizeiptr candidate_size = sizeof(Result::Element);
    constexpr GLsizeiptr world_uv_size = sizeof(glm::vec2);
    constexpr GLsizeiptr density_size = sizeof(float);

    const GL::Buffer buffer;
    const GL::Buffer::Range candidate_range{0, candidate_size * candidate_count};
    const GL::Buffer::Range world_uv_range{candidate_range.size, world_uv_size * candidate_count};
    const GL::Buffer::Range density_range{candidate_range.size + world_uv_range.size, density_size * candidate_size};

    buffer.allocateImmutable(density_range.size + density_range.offset, GL::Buffer::StorageFlags::dynamic_storage);

    buffer.write(candidate_range, candidates.data());
    buffer.write(world_uv_range, world_uvs.data());
    buffer.write(density_range, densities.data());

    constexpr uint candidate_binding_index = 0;
    constexpr uint world_uv_binding_index = 1;
    constexpr uint density_binding_index = 2;

    kernel.setCandidateBufferBindingIndex(candidate_binding_index);
    kernel.setWorldUVBufferBindingIndex(world_uv_binding_index);
    kernel.setDensityBufferBindingIndex(density_binding_index);

    buffer.bindRange(GL::Buffer::IndexedTarget::shader_storage, candidate_binding_index, candidate_range);
    buffer.bindRange(GL::Buffer::IndexedTarget::shader_storage, world_uv_binding_index, world_uv_range);
    buffer.bindRange(GL::Buffer::IndexedTarget::shader_storage, density_binding_index, density_range);

    const GLuint density_texture = s_texture_loader["assets/white.png"];
    kernel.setDensityMapTextureUnit(0);
    gl.BindTextureUnit(0, density_texture);

    kernel.useProgram();
    gl.DispatchCompute(wg_count_x, wg_count_y, 1);
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    std::vector<Result::Element> evaluated_candidates;
    evaluated_candidates.resize(candidate_count);
    buffer.read(candidate_range, evaluated_candidates.data());

    for (const auto &evaluated_candidate: evaluated_candidates)
    {
        if (glm::all(glm::greaterThanEqual(glm::vec2(evaluated_candidate.position), lower_bound))
            && glm::all(glm::lessThan(glm::vec2(evaluated_candidate.position), upper_bound)))
            CHECK(evaluated_candidate.class_index == 0);
        else
            CHECK(evaluated_candidate.class_index == -1u);
    }
}

TEST_CASE("IndexationKernel", "[indexation][kernel]")
{
    using Indices = std::vector<unsigned int>;
    auto indices = GENERATE(Indices{0}, Indices{1},
                            Indices{0, 0}, Indices{0, 1}, Indices{1, 0}, Indices{1, 1},
                            take(6, chunk(10, random(0u, 1u))),
                            take(5, chunk(20, random(0u, 1u))),
                            take(3, chunk(64, random(0u, 1u))),
                            take(3, chunk(333, random(0u, 1u))),
                            take(3, chunk(1024, random(0u, 1u))),
                            take(3, chunk(15000, random(0u, 1u))));

    using Candidate = Result::Element;

    std::vector<Candidate> candidates;
    candidates.reserve(indices.size());
    for (auto i: indices)
        candidates.emplace_back(Candidate{glm::vec3(0.0f), i - 1});

    const unsigned int expected_count = std::count(indices.cbegin(), indices.cend(), 0);

    using namespace GL;

    const GLsizeiptr candidate_count = candidates.size();

    constexpr GLsizeiptr candidate_size = sizeof(Candidate);
    constexpr GLsizeiptr uint_size = sizeof(GLuint);

    Buffer buffer;
    const BufferHandle::Range candidate_range{0, candidate_count * candidate_size};
    const BufferHandle::Range index_range{candidate_range.size, candidate_count * uint_size};
    const BufferHandle::Range count_range{index_range.offset + index_range.size, uint_size};
    const GLsizeiptr buffer_size = count_range.offset + count_range.size;

    buffer.allocateImmutable(buffer_size, BufferHandle::StorageFlags::dynamic_storage);

    GLuint actual_count = 0;
    buffer.write(count_range, &actual_count);      // initialize sum
    buffer.write(candidate_range, candidates.data());  // initialize candidates

    constexpr uint candidate_binding_index = 0;
    constexpr uint index_binding_index = 1;
    constexpr uint count_binding_index = 2;

    IndexationKernel kernel;

    kernel.setCandidateBufferBindingIndex(candidate_binding_index);
    kernel.setIndexBufferBindingIndex(index_binding_index);
    kernel.setCountBufferBindingIndex(count_binding_index);

    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, candidate_binding_index, candidate_range);
    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, index_binding_index, index_range);
    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, count_binding_index, count_range);

    const auto wg_count = IndexationKernel::calculateNumWorkGroups(candidate_count);

    kernel.useProgram();
    gl.DispatchCompute(wg_count.x, wg_count.y, wg_count.z);
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    buffer.read(count_range, &actual_count);

    CHECK(actual_count == expected_count);

    std::vector<int> computed_indices;
    computed_indices.resize(indices.size());
    buffer.read(index_range, computed_indices.data());

    constexpr uint invalid_index = -1u;

    SECTION("correctness")
    {
        CAPTURE(indices);
        CAPTURE(computed_indices);

        const auto expected_invalid = indices.size() - expected_count;

        std::map<int, std::size_t> count;

        for (auto i: computed_indices)
            count[i]++;

        CHECK(count[invalid_index] == expected_invalid);

        std::vector<std::pair<int, std::size_t>> non_unique;
        for (const auto &p: count)
            if (p.first != -1u && p.second > 1)
                non_unique.emplace_back(p);

        CAPTURE(non_unique);
        CHECK(non_unique.empty());
    }

    SECTION("determinism")
    {
        GLuint second_count = 0;
        buffer.write(count_range, &second_count);

        gl.DispatchCompute(wg_count.x, wg_count.y, wg_count.z);
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        std::set<int> first_computed_set;
        for (int i: computed_indices)
            if (i != invalid_index)
                first_computed_set.insert(i);

        buffer.read(count_range, &second_count);

        CAPTURE(indices);
        CHECK(actual_count == second_count);

        std::vector<int> second_computed_indices;
        second_computed_indices.resize(indices.size());
        buffer.read(index_range, second_computed_indices.data());

        std::set<int> second_computed_set;
        for (int i: second_computed_indices)
            if (i != -1)
                second_computed_set.insert(i);

        CHECK(first_computed_set == second_computed_set);
    }
}

TEST_CASE("CopyKernel", "[copy][kernel]")
{
    std::vector<unsigned int> indices {GENERATE(take(6, chunk(10, random(0u, 1u))),
                                                take(5, chunk(20, random(0u, 1u))),
                                                take(3, chunk(64, random(0u, 1u))),
                                                take(3, chunk(333, random(0u, 1u))),
                                                take(3, chunk(1024, random(0u, 1u))),
                                                take(3, chunk(15000, random(0u, 1u))))};
    constexpr uint invalid_index = -1u;

    using Candidate = Result::Element;

    std::vector<Candidate> candidates;
    candidates.reserve(indices.size());

    std::vector<Candidate> valid_elements;
    valid_elements.reserve(indices.size());

    std::vector<unsigned int> copy_indices;
    copy_indices.reserve(indices.size());

    unsigned int valid_count = 0;
    for (auto index: indices)
    {
        uint class_index = index - 1;
        Candidate candidate{glm::vec3(candidates.size()), class_index};
        candidates.emplace_back(candidate);
        if (class_index != invalid_index)
        {
            valid_elements.emplace_back(candidate);
            copy_indices.emplace_back(valid_count);
            valid_count++;
        }
        else
            copy_indices.emplace_back(invalid_index);
    }

    CAPTURE(indices);

    using namespace GL;

    constexpr GLsizeiptr candidate_size = sizeof(Candidate);
    constexpr GLsizeiptr uint_size = sizeof(uint);
    const GLsizeiptr candidate_count = candidates.size();

    Buffer buffer;
    const BufferHandle::Range candidate_range{0, candidate_count * candidate_size};
    const BufferHandle::Range output_range{candidate_range.size, candidate_range.size};
    const BufferHandle::Range index_range{output_range.size + candidate_range.size, candidate_count * uint_size};
    const BufferHandle::Range count_range{index_range.size + index_range.size, uint_size};

    buffer.allocateImmutable(candidate_range.size + output_range.size + index_range.size + count_range.size,
                             BufferHandle::StorageFlags::dynamic_storage | Buffer::StorageFlags::map_read);

    buffer.write(candidate_range, candidates.data());
    buffer.write(count_range, &valid_count);
    buffer.write(index_range, copy_indices.data());

    CopyKernel kernel;

    constexpr uint candidate_buffer_binding = 0;
    constexpr uint output_buffer_binding = 1;
    constexpr uint index_buffer_binding = 2;
    constexpr uint count_buffer_binding = 3;

    kernel.setCandidateBufferBindingIndex(candidate_buffer_binding);
    kernel.setOutputBufferBindingIndex(output_buffer_binding);
    kernel.setIndexBufferBindingIndex(index_buffer_binding);
    kernel.setCountBufferBindingIndex(count_buffer_binding);

    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, candidate_buffer_binding, candidate_range);
    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, output_buffer_binding, output_range);
    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, index_buffer_binding, index_range);
    buffer.bindRange(BufferHandle::IndexedTarget::shader_storage, count_buffer_binding, count_range);

    const glm::uvec3 num_work_groups = CopyKernel::calculateNumWorkGroups(candidate_count);

    kernel.useProgram();
    gl.DispatchCompute(num_work_groups.x, num_work_groups.y, num_work_groups.z);
    gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    auto output_ptr = static_cast<const Candidate *>(buffer.mapRange(output_range, GL::Buffer::AccessFlags::read));

    std::vector<Candidate> copied_elements{output_ptr, output_ptr + valid_count};

    buffer.unmap();

    CHECK(valid_elements == copied_elements);
}

TEST_CASE("DiskDistributionGenerator")
{
    const uint seed = GENERATE(take(10, random(0u, -1u)));
    CAPTURE(seed);

    auto checkCollision = [](glm::vec2 p, glm::vec2 q, glm::vec2 bounds, float footprint)
    {
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
            {
                const glm::ivec2 tile_offset{dx, dy};
                const glm::vec2 offset = glm::vec2(tile_offset) * bounds;

                CHECK(glm::distance(p, q + offset) >= Approx(footprint));
            }
    };

    SECTION("GenerationKernel usage")
    {
        constexpr auto wg_size = GenerationKernel::work_group_size;
        CAPTURE(wg_size);

        DiskDistributionGenerator generator{.5f, wg_size * 2u};
        generator.setSeed(seed);
        generator.setMaxAttempts(100);

        const glm::vec2 bounds {1.f / glm::vec2(wg_size)};

        CAPTURE(bounds);

        for (std::size_t i = 0; i < 64; i++)
        {
            CAPTURE(i);
            REQUIRE_NOTHROW(generator.generate());
        }

        const auto &positions = generator.getPositions();

        for (auto p = positions.begin(); p != positions.end(); p++)
        {
            CAPTURE(*p, p - positions.begin());
            CHECK(p->x >= 0.0f);
            CHECK(p->y >= 0.0f);
            CHECK(p->x <= bounds.x);
            CHECK(p->y <= bounds.y);

            for (auto q = positions.begin(); q != p; q++)
            {
                CAPTURE(*q, q - positions.begin());
                if (p != q)
                    checkCollision(*p, *q, bounds, 1.0f);
            }
        }
    }

    SECTION("randomized")
    {
        const unsigned int x_cell_count = GENERATE(take(3, random(10u, 100u)));
        const unsigned int y_cell_count = GENERATE(take(3, random(10u, 100u)));
        const glm::uvec2 grid_size{x_cell_count, y_cell_count};

        const float footprint = GENERATE(take(3, random(0.001f, 1.0f)));

        const glm::vec2 bounds = glm::vec2(x_cell_count, y_cell_count) * footprint / std::sqrt(2.0f);

        CAPTURE(grid_size, bounds);

        SECTION("DiskDistributionGrid::getBounds()")
        {
            DiskDistributionGrid grid{footprint, grid_size};
            CHECK(grid.getBounds() == bounds);
        }

        DiskDistributionGenerator generator(footprint, {x_cell_count, y_cell_count});
        generator.setMaxAttempts(100);

        SECTION("trivial case")
        {
            glm::vec2 pos;
            REQUIRE_NOTHROW(pos = generator.generate());
            CHECK(pos.x <= bounds.x);
            CHECK(pos.x >= 0.0f);
            CHECK(pos.y <= bounds.y);
            CHECK(pos.y >= 0.0f);
        }

        SECTION("minimum distance")
        {
            for (int i = 0; i < int(bounds.x); i++)
                REQUIRE_NOTHROW(generator.generate());

            for (const glm::vec2 &p: generator.getPositions())
            {
                CAPTURE(p);
                for (const glm::vec2 &q: generator.getPositions())
                {
                    CAPTURE(q);
                    if (p != q)
                        checkCollision(p, q, bounds, footprint);
                }
            }
        }

        SECTION("bounds")
        {
            for (int i = 0; i < int(bounds.x); i++)
            {
                glm::vec2 position;
                REQUIRE_NOTHROW(position = generator.generate());
                CHECK(position.x <= bounds.x);
                CHECK(position.x >= 0.0f);
                CHECK(position.y <= bounds.y);
                CHECK(position.y >= 0.0f);
            }
        }
    }
}

TEST_CASE("SSBO alignment")
{
    GL::Buffer buffer;

    auto compile_compute_shader = [](const char *source_code)
    {
        GL::Program program;
        GL::Shader shader{GL::Shader::Type::compute};
        shader.setSource(source_code);
        shader.compile();

        if (shader.getParameter(GL::Shader::Parameter::compile_status) != GL_TRUE)
            throw std::runtime_error(shader.getInfoLog());

        program.attachShader(shader);
        program.link();

        if (program.getParameter(GL::Program::Parameter::link_status) != GL_TRUE)
            throw std::runtime_error(program.getInfoLog());

        program.detachShader(shader);

        return program;
    };

    SECTION("struct {vec3; uint;}")
    {
        struct Candidate
        {
            glm::vec3 position;
            uint index;
        };

        buffer.allocateImmutable(16 * sizeof(Candidate), GL::Buffer::StorageFlags::none);
        buffer.bindBase(GL::Buffer::IndexedTarget::shader_storage, 0);

        GL::Program program = compile_compute_shader(
                "#version 450 core\n"
                "layout(local_size_x = 16) in;"
                "struct Candidate { vec3 position; uint index; };\n"
                "layout(std430, binding=0) buffer Buffer { Candidate[] candidates; };\n"
                "void main() "
                "{"
                "   candidates[gl_GlobalInvocationID.x] = Candidate(vec3(gl_GlobalInvocationID.x),"
                "                                                   gl_GlobalInvocationID.x);"
                "}\n");

        std::vector<Candidate> candidates;
        candidates.resize(16);

        program.use();
        gl.DispatchCompute(1, 1, 1);
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        buffer.read(0, candidates.size() * sizeof(Candidate), candidates.data());

        for (uint i = 0; i < candidates.size(); i++)
        {
            CAPTURE(i);
            CHECK(candidates[i].position == glm::vec3(i));
            CHECK(candidates[i].index == i);
        }
    }

    SECTION("vec3")
    {
        constexpr std::size_t num_elements = 16;
        glm::vec4 results[num_elements];

        GL::Program program = compile_compute_shader(
                "#version 450 core\n"
                "layout(local_size_x=16) in;\n"
                "layout(std430, binding=0) buffer Buffer { vec3 positions[]; };\n"
                "void main() { positions[gl_GlobalInvocationID.x] = vec3(gl_GlobalInvocationID.x); }");

        buffer.allocateImmutable(sizeof(results), GL::Buffer::StorageFlags::none);
        buffer.bindBase(GL::Buffer::IndexedTarget::shader_storage, 0);

        program.use();
        gl.DispatchCompute(1, 1, 1);
        gl.MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

        buffer.read(0, sizeof(results), results);

        for (int i = 0; i < num_elements; i++)
            CHECK(glm::vec3(results[i]) == glm::vec3(i));
    }
}
