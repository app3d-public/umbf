#pragma once

#include <glm/glm.hpp>
#include <oneapi/tbb/parallel_for.h>
#include "asset.hpp"

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
        APPLIB_API void fillColorPixels(const glm::vec4 &color, Image2D &imageInfo);

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
        APPLIB_API void copyPixelsToArea(const Image2D &src, Image2D &dst, const Atlas::Rect &rect);

        /**
         * @brief A template function to convert the bit depth of an image from one type to another
         * @tparam S The source type of the image
         * @tparam T The destination type of the image
         * @param source A shared pointer to the source image
         * @param size The size of the image
         * @param srcChannels The number of channels in the source image
         * @param dstChannels The number of channels in the destination image
         * @return A shared pointer to the converted image
         */
        template <typename S, typename T>
        void *convertImageChannelBits(void *source, u64 size, int srcChannels, int dstChannels)
        {
            auto src = reinterpret_cast<S *>(source);
            T *buffer = (T *)acul::mem_allocator<std::byte>::allocate(size * dstChannels / srcChannels);
            f64 maxValue;

            if constexpr (std::is_floating_point<S>::value)
                maxValue = 1.0f;
            else
                maxValue = static_cast<f64>(std::numeric_limits<T>::max());

            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<size_t>(0, size / srcChannels),
                [&](const oneapi::tbb::blocked_range<size_t> &r) {
                    for (size_t pixel = r.begin(); pixel < r.end(); ++pixel)
                    {
                        int srcIndex = pixel * srcChannels;
                        int dstIndex = pixel * dstChannels;

                        for (int ch = 0; ch < dstChannels; ++ch)
                        {
                            if (ch < srcChannels)
                            {
                                if constexpr (std::is_same_v<S, f16> || std::is_floating_point<S>::value)
                                {
                                    if constexpr (std::is_same_v<T, f16> || std::is_floating_point<T>::value)
                                        buffer[dstIndex + ch] = static_cast<T>(src[srcIndex + ch]);
                                    else
                                        buffer[dstIndex + ch] = static_cast<T>(src[srcIndex + ch] * maxValue);
                                }
                                else
                                {
                                    if constexpr (std::is_same_v<T, f16> || std::is_floating_point_v<T>)
                                        buffer[dstIndex + ch] = static_cast<T>(src[srcIndex + ch]) /
                                                                static_cast<f32>(std::numeric_limits<S>::max());
                                    else
                                        buffer[dstIndex + ch] =
                                            static_cast<T>((static_cast<f32>(src[srcIndex + ch]) /
                                                            static_cast<f32>(std::numeric_limits<S>::max())) *
                                                           maxValue);
                                }
                            }
                            else
                                buffer[dstIndex + ch] = static_cast<T>(maxValue);
                        }
                    }
                });

            return reinterpret_cast<void *>(buffer);
        }

        /**
         * @brief Converts the source image to a specified format and channel depth.
         *
         * This function facilitates the conversion of image data to a different bit depth and channel
         * configuration based on the provided source format.
         *
         * @tparam T Destination type for the image conversion.
         * @param srcFormat Source format of the image, as defined by the Vulkan API.
         * @param source Pointer to the source image data.
         * @param size Size of the source image data.
         * @param srcChannels Number of channels in the source image.
         * @param dstChannels Number of channels in the destination image.
         * @return A new buffer is dynamically allocated for the converted image data.
         */
        template <typename T>
        void *getImageConvertBySrc(vk::Format srcFormat, void *source, u64 size, int srcChannels, int dstChannels)
        {
            switch (srcFormat)
            {
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8A8Uint:
                case vk::Format::eR8G8B8A8Unorm:
                    return convertImageChannelBits<T, u8>(source, size, srcChannels, dstChannels);
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8A8Snorm:
                    return convertImageChannelBits<T, i8>(source, size, srcChannels, dstChannels);
                case vk::Format::eR16G16B16A16Uint:
                    return convertImageChannelBits<T, u16>(source, size, srcChannels, dstChannels);
                case vk::Format::eR32G32B32A32Uint:
                    return convertImageChannelBits<T, u32>(source, size, srcChannels, dstChannels);
                case vk::Format::eR16G16B16A16Sfloat:
                    return convertImageChannelBits<T, f16>(source, size, srcChannels, dstChannels);
                case vk::Format::eR32G32B32A32Sfloat:
                    return convertImageChannelBits<T, f32>(source, size, srcChannels, dstChannels);
                default:
                    return nullptr;
            }
        }

        /**
         * @brief Converts the provided image to a specified format and channel configuration.
         *
         * @param image Reference to the image structure containing image data and metadata.
         * @param dstFormat Desired format for the destination image, as defined by the Vulkan API.
         * @param dstChannels Number of channels for the destination image.
         * @return  A new buffer is dynamically allocated for the converted image data based on the specified
         * destination format and channel configuration.
         */
        APPLIB_API void *convertImage(const Image2D &image, vk::Format dstFormat, int dstChannels);

        APPLIB_API void filterMatAssignments(const acul::vector<acul::shared_ptr<MatRangeAssignAtrr>> &assignes,
                                             size_t faceCount, u64 defaultID,
                                             acul::vector<acul::shared_ptr<MatRangeAssignAtrr>> &dst);

        namespace mesh
        {
            using namespace umbf::mesh;
            APPLIB_API void fillVertexGroups(const Model &model, acul::vector<VertexGroup> &groups);
        } // namespace mesh
    } // namespace utils
} // namespace umbf