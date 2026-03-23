#include <acul/log.hpp>
#include <amal/half.hpp>
#include <numeric>
#include <oneapi/tbb/parallel_for.h>
#include <umbf/utils.hpp>

namespace umbf
{
    namespace utils
    {
        acul::unique_ptr<void> make_clear_pixel(const umbf::ImageFormat &format, size_t channel_count)
        {
            const size_t pixel_size = channel_count * format.bytes_per_channel;
            std::byte *data = acul::mem_allocator<std::byte>::allocate(pixel_size);
            std::memset(data, 0, pixel_size);
            return acul::unique_ptr<void>(data);
        }

        void fill_color_pixels(void *color_data, Image2D &image_info)
        {
            const size_t pixel_stride = image_info.channels.size() * image_info.format.bytes_per_channel;
            const size_t total_bytes = image_info.size();
            assert(pixel_stride == 0 || total_bytes % pixel_stride == 0);
            std::byte *dst = acul::mem_allocator<std::byte>::allocate(total_bytes);
            for (size_t i = 0; i < total_bytes; i += pixel_stride) memcpy(dst + i, color_data, pixel_stride);
            image_info.pixels = dst;
        }

        void copy_pixels_to_area(const Image2D &src, Image2D &dst, const Atlas::Rect &rect)
        {
            if (src.format != dst.format) throw acul::runtime_error("Image format mismatch");
            if (rect.x + rect.w > dst.width || rect.y + rect.h > dst.height)
                throw acul::runtime_error("Dst area is out of image bounds");

            const size_t bytes_per_pixel = dst.channels.size() * dst.format.bytes_per_channel;
            const size_t src_row_bytes = rect.w * bytes_per_pixel;
            const size_t dst_row_bytes = dst.width * bytes_per_pixel;

            const std::byte *src_pixels = static_cast<const std::byte *>(src.pixels);
            std::byte *dst_pixels = static_cast<std::byte *>(dst.pixels);

            for (int y = 0; y < rect.h; ++y)
            {
                const std::byte *src_row = src_pixels + y * src_row_bytes;
                std::byte *dst_row = dst_pixels + ((rect.y + y) * dst_row_bytes + rect.x * bytes_per_pixel);
                memcpy(dst_row, src_row, src_row_bytes);
            }
        }

        // A template function to convert the bit depth of an image from one type to another
        template <typename S, typename T>
        void *convert_image_channel_bits(const void *source, u64 size, int src_channels, int dst_channels)
        {
            assert(source);
            auto src = reinterpret_cast<const S *>(source);
            const u64 pixel_count = size / sizeof(S) / src_channels;
            const u64 total_count = pixel_count * dst_channels;

            T *buffer = (T *)acul::mem_allocator<std::byte>::allocate(total_count * sizeof(T));
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<size_t>(0, static_cast<size_t>(pixel_count)),
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
                                    {
                                        const f64 value = static_cast<f64>(src[src_index + ch]);
                                        const f64 clamped = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
                                        buffer[dst_index + ch] =
                                            static_cast<T>(clamped * static_cast<f64>(std::numeric_limits<T>::max()));
                                    }
                                }
                                else
                                {
                                    if constexpr (amal::is_floating_point_v<T>)
                                        buffer[dst_index + ch] = static_cast<T>(src[src_index + ch]) /
                                                                 static_cast<f32>(std::numeric_limits<S>::max());
                                    else
                                        buffer[dst_index + ch] =
                                            static_cast<T>((static_cast<f32>(src[src_index + ch]) /
                                                            static_cast<f32>(std::numeric_limits<S>::max())) *
                                                           static_cast<f64>(std::numeric_limits<T>::max()));
                                }
                            }
                            else
                            {
                                if constexpr (std::is_same_v<T, f16> || std::is_floating_point<T>::value)
                                    buffer[dst_index + ch] = static_cast<T>(1.0f);
                                else
                                    buffer[dst_index + ch] = std::numeric_limits<T>::max();
                            }
                        }
                    }
                });

            return reinterpret_cast<void *>(buffer);
        }

        // Converts the source image to a specified format and channel depth.
        template <typename T>
        void *convert_from_format(const ImageFormat &src_format, const void *source, u64 size, int src_channels,
                                  int dst_channels)
        {
            switch (src_format.type)
            {
                case ImageFormat::Type::uint:
                    switch (src_format.bytes_per_channel)
                    {
                        case 1:
                            return convert_image_channel_bits<u8, T>(source, size, src_channels, dst_channels);
                        case 2:
                            return convert_image_channel_bits<u16, T>(source, size, src_channels, dst_channels);
                        case 4:
                            return convert_image_channel_bits<u32, T>(source, size, src_channels, dst_channels);
                    }
                    break;
                case ImageFormat::Type::sfloat:
                    switch (src_format.bytes_per_channel)
                    {
                        case 2:
                            return convert_image_channel_bits<f16, T>(source, size, src_channels, dst_channels);
                        case 4:
                            return convert_image_channel_bits<f32, T>(source, size, src_channels, dst_channels);
                    }
                    break;
                default:
                    break;
            }
            return nullptr;
        }

        void *convert_buffer(const void *source, size_t source_size, const ImageFormat &src_format, int src_channels,
                             const ImageFormat &dst_format, int dst_channels)
        {
            if (!source || source_size == 0 || src_channels <= 0 || dst_channels <= 0) return nullptr;
            const size_t src_stride = static_cast<size_t>(src_channels) * src_format.bytes_per_channel;
            if (src_stride == 0 || source_size % src_stride != 0) return nullptr;

            if (src_format == dst_format && src_channels == dst_channels)
            {
                std::byte *copy = acul::mem_allocator<std::byte>::allocate(source_size);
                memcpy(copy, source, source_size);
                return copy;
            }

            switch (dst_format.type)
            {
                case ImageFormat::Type::uint:
                    switch (dst_format.bytes_per_channel)
                    {
                        case 1:
                            return convert_from_format<u8>(src_format, source, source_size, src_channels, dst_channels);
                        case 2:
                            return convert_from_format<u16>(src_format, source, source_size, src_channels,
                                                            dst_channels);
                        case 4:
                            return convert_from_format<u32>(src_format, source, source_size, src_channels,
                                                            dst_channels);
                    }
                    break;
                case ImageFormat::Type::sfloat:
                    switch (dst_format.bytes_per_channel)
                    {
                        case 2:
                            return convert_from_format<f16>(src_format, source, source_size, src_channels,
                                                            dst_channels);
                        case 4:
                            return convert_from_format<f32>(src_format, source, source_size, src_channels,
                                                            dst_channels);
                    }
                    break;
                default:
                    break;
            }
            return nullptr;
        }

        void filter_mat_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignes, size_t face_count,
                                    u64 default_id, acul::vector<acul::shared_ptr<MaterialRange>> &dst)
        {
            auto default_assign = acul::make_shared<MaterialRange>();
            default_assign->mat_id = default_id;
            default_assign->faces.resize(face_count);
            std::iota(default_assign->faces.begin(), default_assign->faces.end(), 0);

            if (assignes.empty())
                dst.push_back(default_assign);
            else
            {
                acul::vector<bool> face_included(face_count, false);

                for (const auto &assign : assignes)
                    for (u32 face : assign->faces)
                        if (face < face_count) face_included[face] = true;
                default_assign->faces.erase(std::remove_if(default_assign->faces.begin(), default_assign->faces.end(),
                                                           [&](u32 index) { return face_included[index]; }),
                                            default_assign->faces.end());
                if (!default_assign->faces.empty()) dst.push_back(default_assign);
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
