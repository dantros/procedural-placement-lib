#ifndef PROCEDURALPLACEMENTLIB_GENERATION_KERNEL_HPP
#define PROCEDURALPLACEMENTLIB_GENERATION_KERNEL_HPP

#include "placement_pipeline_kernel.hpp"

#include "glutils/buffer.hpp"
#include "glutils/program.hpp"
#include "glutils/guard.hpp"

#include "glm/glm.hpp"

namespace placement {

    /// Wrapper for the candidate position generation compute shader.
    class GenerationKernel final : public PlacementPipelineKernel
    {
    public:
        GenerationKernel();

        /// Get the texture unit the heightmap sampler will read from.
        [[nodiscard]]
        auto getHeightTextureUnit() const -> glutils::GLuint {return m_heightmap.getTextureUnit();}

        /// Set the texture unit the heightmap will be read from.
        void setHeightTextureUnit(glutils::GLuint new_index) {m_heightmap.setTextureUnit(*this, new_index);}

        /// Get the texture unit the densitymap will be read from.
        [[nodiscard]] auto getDensitymapSampler() const -> glutils::GLuint {return m_densitymap.getTextureUnit();}

        /// Set the texture unit the densitymap will be read from.
        void setDensityTextureUnit(glutils::GLuint new_index) {m_densitymap.setTextureUnit(*this, new_index);}

        /**
         * @brief Set the values for the arguments.
         * @param world_scale Dimensions of the world. Determines how the density and height textures map onto world
         * space. For a any given point in a texture with coordinates (u, v), the corresponding horizontal position in
         * world space will be (u * @p world_space.x , v * @p world_space.z ). Similarly, values in the height
         * map will be interpreted by multiplying them by @p world_space.y .
         * @param footprint The collision radius for each generated point. No two points will have a separation between
         * them inferior to two times the footprint (in world space units). This is valid even for points marked as
         * discarded in the index buffer, as well as those generated by different calls to dispatchCompute, as long as
         * the @p world_scale, @p footprint and placement stencil used are identical.
         * @param lower_bound the lower limit of the placement area. All valid candidates (i.e. those marked with a 1 in
         * the index buffer) will have X and Z coordinates such that x >= lower_bound.x and z >= lower_bound.y . Note
         * that @p lower_bound is a horizontal position, and as such its Y axis corresponds to the Z axis in world space.
         * @param upper_bound the upper limit of the placement area. Analogous to @p lower_bound, but all valid points
         * have X and Z coordinates such that x < upper_bound.x and z < upper_bound.z (note that less than operator is
         * used here, while the lower bound uses greater _or equal_).
         * @return the total number of candidates that would be generated by a call to dispatchCompute() with the
         * argument values just set. This value should be used to calculate the size of the position and index buffers,
         * and can be queried with calculateCandidateCount().
         */
        std::size_t setArgs(const glm::vec3& world_scale, float footprint, glm::vec2 lower_bound, glm::vec2 upper_bound);

        /**
         * @brief calculate the number of candidates that will be generated by the current argument values.
         * This value determines the size of the position and index buffers. This value is invalidated by any call to
         * setArgs(), that is, whenever any argument changes except for the heightmap and densitymap.
         * @return the number of candidates generated by dispatchCompute().
         */
        [[nodiscard]]
        std::size_t calculateCandidateCount() const
        {
            const auto num_invocations = m_num_work_groups * s_work_group_size;
            return num_invocations.x * num_invocations.y;
        }

        /// Execute the kernel using the previously set arguments.
        void dispatchCompute() const;

    private:
        /// shader source code
        static const std::string s_source_string;

        /// The work group size of this kernel.
        static constexpr glm::uvec2 s_work_group_size {8, 8};

        TextureSampler m_heightmap;
        TextureSampler m_densitymap;
        glm::uvec2 m_num_work_groups {0, 0};

        /// Calculate the number of workgroups required to cover the placement area.
        [[nodiscard]]
        static auto m_calculateNumWorkGroups(float footprint, glm::vec2 lower_bound, glm::vec2 upper_bound) -> glm::uvec2;
    };

} // placement

#endif //PROCEDURALPLACEMENTLIB_GENERATION_KERNEL_HPP
