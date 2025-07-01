#pragma once

#include <acul/gpu/device.hpp>
#include <glm/glm.hpp>
#include <oneapi/tbb/parallel_for.h>
#include "umbf.hpp"

namespace umbf
{
    namespace utils
    {
        /**
         * @brief Fills the pixel data of an image with a specified color based on the image format.
         *
         * This function determines the appropriate data type for the pixel values based on
         * the image format and calls the corresponding template function to fill the pixel data.
         *
         * @param color The color to fill the image with, represented as a glm::vec4.
         * @param imageInfo The ImageInfo structure containing the image details, including the format.
         */
        APPLIB_API void fill_color_pixels(const glm::vec4 &color, Image2D &image_info);

        /**
         * @brief Copies pixel data from the source image to a specified area in the destination image.
         *
         * This function copies pixel data from the source image to a specified rectangular area
         * in the destination image. The operation ensures that the source and destination images
         * have matching formats and that the specified area is within the bounds of the destination image.
         *
         * @param src The source image information containing the pixel data to be copied.
         * @param dst The destination image information where the pixel data will be copied.
         * @param rect The rectangular area in the destination image to which the pixel data will be copied.
         * @throws acul::runtime_error if the image formats of the source and destination images do not match
         *         or if the specified rectangular area is out of the bounds of the destination image.
         */
        APPLIB_API void copy_pixels_to_area(const Image2D &src, Image2D &dst, const Atlas::Rect &rect);

        /**
         * @brief Converts the provided image to a specified format and channel configuration.
         *
         * @param image Reference to the image structure containing image data and metadata.
         * @param format Desired format for the destination image, as defined by the Vulkan API.
         * @param channels Number of channels for the destination image.
         * @return  A new buffer is dynamically allocated for the converted image data based on the specified
         * destination format and channel configuration.
         */
        APPLIB_API void *convert_image(const Image2D &image, vk::Format format, int channels);

        APPLIB_API void filter_mat_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignes,
                                               size_t face_count, u64 default_id,
                                               acul::vector<acul::shared_ptr<MaterialRange>> &dst);

        namespace mesh
        {
            using namespace umbf::mesh;
            APPLIB_API void fill_vertex_groups(const Model &model, acul::vector<VertexGroup> &groups);
        } // namespace mesh

        class DeviceSelector final : public acul::gpu::physical_device_selector
        {
        public:
            DeviceSelector(const acul::shared_ptr<Device> &config) : _config(config) {}

            virtual const vk::PhysicalDevice *select(const std::vector<vk::PhysicalDevice> &devices) override
            {
                if (!_config) return nullptr;
                i8 device_id = _config->device;
                if (device_id < 0 || device_id >= (int)devices.size())
                {
                    LOG_WARN("Invalid device id found in configuration file");
                    return nullptr;
                }
                return &devices[device_id];
            }

        private:
            acul::shared_ptr<Device> _config;
        };
    } // namespace utils
} // namespace umbf