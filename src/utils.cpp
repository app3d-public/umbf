#include <acul/log.hpp>
#include <numeric>
#include <umbf/utils.hpp>
#include "umbf/umbf.hpp"

namespace umbf
{
    namespace utils
    {
        template <typename T>
        void fill_color_pixels_impl(const glm::vec4 &color, Image2D &image_info)
        {
            T *data = (T *)acul::mem_allocator<std::byte>::allocate(image_info.size());
            if (color[0] == color[1] && color[0] == color[2] && color[0] == color[3])
                std::fill(data, data + image_info.size(), color[0]);
            else
            {
                assert((image_info.channel_count == 3 || image_info.channel_count == 4) &&
                       "Fill color only supports RGB or RGBA image");
                for (vk::DeviceSize i = 0; i < image_info.size(); i += image_info.channel_count)
                    for (int ch = 0; ch < image_info.channel_count; ch++) data[i + ch] = color[ch];
            }

            image_info.pixels = data;
        }

        void fill_color_pixels(const glm::vec4 &color, Image2D &image_info)
        {
            switch (image_info.format)
            {
                case vk::Format::eR8G8B8Unorm:
                case vk::Format::eR8G8B8A8Unorm:
                case vk::Format::eR8G8B8Srgb:
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8Uint:
                case vk::Format::eR8G8B8A8Uint:
                    fill_color_pixels_impl<u8>(color, image_info);
                    break;
                case vk::Format::eR8G8B8Sint:
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8Snorm:
                case vk::Format::eR8G8B8A8Snorm:
                    fill_color_pixels_impl<i8>(color, image_info);
                    break;
                case vk::Format::eR16G16B16Unorm:
                case vk::Format::eR16G16B16A16Unorm:
                case vk::Format::eR16G16B16Uint:
                case vk::Format::eR16G16B16A16Uint:
                    fill_color_pixels_impl<u16>(color, image_info);
                    break;
                case vk::Format::eR16G16B16Sint:
                case vk::Format::eR16G16B16A16Sint:
                case vk::Format::eR16G16B16Snorm:
                case vk::Format::eR16G16B16A16Snorm:
                    fill_color_pixels_impl<i16>(color, image_info);
                    break;
                case vk::Format::eR32G32B32Uint:
                case vk::Format::eR32G32B32A32Uint:
                    fill_color_pixels_impl<u32>(color, image_info);
                    break;
                case vk::Format::eR32G32B32Sint:
                case vk::Format::eR32G32B32A32Sint:
                    fill_color_pixels_impl<i32>(color, image_info);
                    break;
                case vk::Format::eR16G16B16Sfloat:
                case vk::Format::eR16G16B16A16Sfloat:
                    fill_color_pixels_impl<f16>(color, image_info);
                    break;
                case vk::Format::eR32G32B32Sfloat:
                case vk::Format::eR32G32B32A32Sfloat:
                    fill_color_pixels_impl<f32>(color, image_info);
                    break;
                default:
                    LOG_WARN("Cannot fill pixel buffer. Unsupported format: %s",
                             vk::to_string(image_info.format).c_str());
                    break;
            }
        }

        template <typename T>
        void copy_pixels_to_area_impl(const Image2D &src, Image2D &dst, const Atlas::Rect &rect)
        {
            const T *pSrc = reinterpret_cast<const T *>(src.pixels);
            T *pDst = reinterpret_cast<T *>(dst.pixels);
            if (rect.x + rect.w <= dst.width && rect.y + rect.h <= dst.height)
                for (int y = 0; y < rect.h; ++y)
                    memcpy(pDst + ((rect.y + y) * dst.width + rect.x) * dst.channel_count,
                           pSrc + (y * rect.w) * dst.channel_count, rect.w * dst.channel_count);
            else
                throw acul::runtime_error("Dst area is out of image bounds");
        }

        void copy_pixels_to_area(const Image2D &src, Image2D &dst, const Atlas::Rect &rect)
        {
            if (src.format != dst.format) throw acul::runtime_error("Image format mismatch");
            switch (dst.format)
            {
                case vk::Format::eR8G8B8Unorm:
                case vk::Format::eR8G8B8A8Unorm:
                case vk::Format::eR8G8B8Srgb:
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8Uint:
                case vk::Format::eR8G8B8A8Uint:
                    copy_pixels_to_area_impl<u8>(src, dst, rect);
                    break;
                case vk::Format::eR8G8B8Sint:
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8Snorm:
                case vk::Format::eR8G8B8A8Snorm:
                    copy_pixels_to_area_impl<i8>(src, dst, rect);
                    break;
                case vk::Format::eR16G16B16Unorm:
                case vk::Format::eR16G16B16A16Unorm:
                case vk::Format::eR16G16B16Uint:
                case vk::Format::eR16G16B16A16Uint:
                    copy_pixels_to_area_impl<u16>(src, dst, rect);
                    break;
                case vk::Format::eR16G16B16Sint:
                case vk::Format::eR16G16B16A16Sint:
                case vk::Format::eR16G16B16Snorm:
                case vk::Format::eR16G16B16A16Snorm:
                    copy_pixels_to_area_impl<i16>(src, dst, rect);
                    break;
                case vk::Format::eR32G32B32Uint:
                case vk::Format::eR32G32B32A32Uint:
                    copy_pixels_to_area_impl<u32>(src, dst, rect);
                    break;
                case vk::Format::eR32G32B32Sint:
                case vk::Format::eR32G32B32A32Sint:
                    copy_pixels_to_area_impl<i32>(src, dst, rect);
                    break;
                case vk::Format::eR16G16B16Sfloat:
                case vk::Format::eR16G16B16A16Sfloat:
                    copy_pixels_to_area_impl<f16>(src, dst, rect);
                    break;
                case vk::Format::eR32G32B32Sfloat:
                case vk::Format::eR32G32B32A32Sfloat:
                    copy_pixels_to_area_impl<f32>(src, dst, rect);
                    break;
                default:
                    LOG_WARN("Cannot copy pixel buffer. Unsupported format: %s", vk::to_string(dst.format).c_str());
                    break;
            }
        }

        // A template function to convert the bit depth of an image from one type to another
        template <typename S, typename T>
        void *convert_image_channel_bits(void *source, u64 size, int src_channels, int dst_channels)
        {
            assert(source);
            auto src = reinterpret_cast<S *>(source);
            const u64 pixel_count = size / sizeof(S) / src_channels;
            const u64 total_count = pixel_count * dst_channels;

            T *buffer = (T *)acul::mem_allocator<std::byte>::allocate(total_count * sizeof(T));
            f64 max_value;

            if constexpr (std::is_floating_point<S>::value)
                max_value = 1.0f;
            else
                max_value = static_cast<f64>(std::numeric_limits<T>::max());
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<size_t>(0, size / src_channels),
                [&](const oneapi::tbb::blocked_range<size_t> &r) {
                    for (size_t pixel = r.begin(); pixel < r.end(); ++pixel)
                    {
                        int src_index = pixel * src_channels;
                        int dst_index = pixel * dst_channels;

                        for (int ch = 0; ch < dst_channels; ++ch)
                        {
                            if (ch < src_channels)
                            {
                                if constexpr (std::is_same_v<S, f16> || std::is_floating_point<S>::value)
                                {
                                    if constexpr (std::is_same_v<T, f16> || std::is_floating_point<T>::value)
                                        buffer[dst_index + ch] = static_cast<T>(src[src_index + ch]);
                                    else
                                        buffer[dst_index + ch] = static_cast<T>(src[src_index + ch] * max_value);
                                }
                                else
                                {
                                    if constexpr (std::is_same_v<T, f16> || std::is_floating_point_v<T>)
                                        buffer[dst_index + ch] = static_cast<T>(src[src_index + ch]) /
                                                                 static_cast<f32>(std::numeric_limits<S>::max());
                                    else
                                        buffer[dst_index + ch] =
                                            static_cast<T>((static_cast<f32>(src[src_index + ch]) /
                                                            static_cast<f32>(std::numeric_limits<S>::max())) *
                                                           max_value);
                                }
                            }
                            else
                                buffer[dst_index + ch] = static_cast<T>(max_value);
                        }
                    }
                });

            return reinterpret_cast<void *>(buffer);
        }

        // Converts the source image to a specified format and channel depth.
        template <typename T>
        void *get_image_convert_by_src(vk::Format format, void *source, u64 size, int src_channels, int dst_channels)
        {
            switch (format)
            {
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8A8Uint:
                case vk::Format::eR8G8B8A8Unorm:
                    return convert_image_channel_bits<T, u8>(source, size, src_channels, dst_channels);
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8A8Snorm:
                    return convert_image_channel_bits<T, i8>(source, size, src_channels, dst_channels);
                case vk::Format::eR16G16B16A16Uint:
                    return convert_image_channel_bits<T, u16>(source, size, src_channels, dst_channels);
                case vk::Format::eR32G32B32A32Uint:
                    return convert_image_channel_bits<T, u32>(source, size, src_channels, dst_channels);
                case vk::Format::eR16G16B16A16Sfloat:
                    return convert_image_channel_bits<T, f16>(source, size, src_channels, dst_channels);
                case vk::Format::eR32G32B32A32Sfloat:
                    return convert_image_channel_bits<T, f32>(source, size, src_channels, dst_channels);
                default:
                    return nullptr;
            }
        }

        void *convert_image(const Image2D &image, vk::Format format, int channels)
        {
            switch (image.format)
            {
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8A8Uint:
                case vk::Format::eR8G8B8A8Unorm:
                    return get_image_convert_by_src<u8>(format, image.pixels, image.size() / image.bytes_per_channel,
                                                        image.channel_count, channels);
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8A8Snorm:
                    return get_image_convert_by_src<i8>(format, image.pixels, image.size() / image.bytes_per_channel,
                                                        image.channel_count, channels);
                case vk::Format::eR16G16B16A16Uint:
                    return get_image_convert_by_src<u16>(format, image.pixels, image.size() / image.bytes_per_channel,
                                                         image.channel_count, channels);
                case vk::Format::eR32G32B32A32Uint:
                    return get_image_convert_by_src<u32>(format, image.pixels, image.size() / image.bytes_per_channel,
                                                         image.channel_count, channels);
                case vk::Format::eR16G16B16A16Sfloat:
                    return get_image_convert_by_src<f16>(format, image.pixels, image.size() / image.bytes_per_channel,
                                                         image.channel_count, channels);
                case vk::Format::eR32G32B32A32Sfloat:
                    return get_image_convert_by_src<f32>(format, image.pixels, image.size() / image.bytes_per_channel,
                                                         image.channel_count, channels);
                default:
                    return nullptr;
            }
        }

        void filter_mat_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignes, size_t faceCount,
                                    u64 default_id, acul::vector<acul::shared_ptr<MaterialRange>> &dst)
        {
            auto defaultAssign = acul::make_shared<MaterialRange>();
            defaultAssign->mat_id = default_id;
            defaultAssign->faces.resize(faceCount);
            std::iota(defaultAssign->faces.begin(), defaultAssign->faces.end(), 0);

            if (assignes.empty())
                dst.push_back(defaultAssign);
            else
            {
                acul::vector<bool> faceIncluded(faceCount, false);

                for (const auto &assign : assignes)
                    for (const auto &face : assign->faces) faceIncluded[face] = true;
                defaultAssign->faces.erase(std::remove_if(defaultAssign->faces.begin(), defaultAssign->faces.end(),
                                                          [&](u32 index) { return faceIncluded[index]; }),
                                           defaultAssign->faces.end());
                if (!defaultAssign->faces.empty()) dst.push_back(defaultAssign);
                for (const auto &assign : assignes) dst.push_back(assign);
            }
        }

        namespace mesh
        {
            void fill_vertex_groups(const Model &model, acul::vector<VertexGroup> &groups)
            {
                groups.resize(model.group_count);
                for (size_t f = 0; f < model.faces.size(); ++f)
                {
                    for (const auto &vref : model.faces[f].vertices)
                    {
                        groups[vref.group].faces.push_back(f);
                        groups[vref.group].vertices.push_back(vref.vertex);
                    }
                }
            }
        } // namespace mesh
    } // namespace utils
} // namespace umbf