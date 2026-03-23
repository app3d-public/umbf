#pragma once

#include "umbf.hpp"

namespace umbf
{
    namespace utils
    {
        // Create transparent pixel depending on image format
        APPLIB_API acul::unique_ptr<void> make_clear_pixel(const umbf::ImageFormat &format, size_t channel_count);

        /**
         * @brief Fills the pixel data of an image with a specified color based on the image format.
         *
         * This function determines the appropriate data type for the pixel values based on
         * the image format and calls the corresponding template function to fill the pixel data.
         *
         * @param color The color to fill the image with, represented as a acul::vec4.
         * @param image The ImageInfo structure containing the image details, including the format.
         */
        APPLIB_API void fill_color_pixels(void *color, Image2D &image);

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
         * @brief Converts a raw pixel buffer from one format/channel layout to another.
         *
         * @param source Pointer to source pixels.
         * @param source_size Source buffer size in bytes.
         * @param src_format Source channel format.
         * @param src_channels Source channel count.
         * @param dst_format Destination channel format.
         * @param dst_channels Destination channel count.
         * @return Newly allocated converted buffer or nullptr on failure.
         */
        APPLIB_API void *convert_buffer(const void *source, size_t source_size, const ImageFormat &src_format,
                                        int src_channels, const ImageFormat &dst_format, int dst_channels);

        /**
         * @brief Converts the provided image to a specified format and channel configuration.
         *
         * @param image Reference to the image structure containing image data and metadata.
         * @param format Desired format for the destination image.
         * @param channels Number of channels for the destination image.
         * @return  A new buffer is dynamically allocated for the converted image data based on the specified
         * destination format and channel configuration.
         */
        inline void *convert_image(const Image2D &image, ImageFormat format, int channels)
        {
            const int src_channels = static_cast<int>(image.channels.size());
            return convert_buffer(image.pixels, image.size(), image.format, src_channels, format, channels);
        }

        APPLIB_API void filter_mat_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignes,
                                               size_t face_count, u64 default_id,
                                               acul::vector<acul::shared_ptr<MaterialRange>> &dst);

        namespace mesh
        {
            using namespace umbf::mesh;
            APPLIB_API void fill_vertex_groups(const Model &model, acul::vector<VertexGroup> &groups);
        } // namespace mesh
    } // namespace utils
} // namespace umbf
