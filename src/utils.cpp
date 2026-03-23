#include <acul/log.hpp>
#include <amal/half.hpp>
#include <numeric>
#include <oneapi/tbb/parallel_for.h>
#include <umbf/utils.hpp>

namespace umbf
{
    namespace utils
    {
        using detail::MaxRectsCandidate;
        using detail::SkylineNode;

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

        void copy_pixels_to_area(const Image2D &src, Image2D &dst, const amal::irect &rect)
        {
            if (src.format != dst.format) throw acul::runtime_error("Image format mismatch");
            if (rect.offset.x + rect.size.x > dst.width || rect.offset.y + rect.size.y > dst.height)
                throw acul::runtime_error("Dst area is out of image bounds");

            const size_t bytes_per_pixel = dst.channels.size() * dst.format.bytes_per_channel;
            const size_t src_row_bytes = rect.size.x * bytes_per_pixel;
            const size_t dst_row_bytes = dst.width * bytes_per_pixel;

            const std::byte *src_pixels = static_cast<const std::byte *>(src.pixels);
            std::byte *dst_pixels = static_cast<std::byte *>(dst.pixels);

            for (int y = 0; y < rect.size.y; ++y)
            {
                const std::byte *src_row = src_pixels + y * src_row_bytes;
                std::byte *dst_row =
                    dst_pixels + ((rect.offset.y + y) * dst_row_bytes + rect.offset.x * bytes_per_pixel);
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

        struct SkylineCandidate
        {
            bool valid = false;
            bool from_waste_map = false;
            amal::irect rect{};
            u32 node_index = 0;
            i32 score_primary = 0;
            i32 score_secondary = 0;
        };

        struct MaxRectsAttemptResult
        {
            bool packed = false;
            u32 packed_count = 0;
            u64 unpacked_area = 0;
        };

        static bool try_push_unique_sorted(acul::vector<i32> &values, i32 value)
        {
            for (u32 i = 0; i < values.size(); ++i)
            {
                if (values[i] == value) return false;
                if (values[i] > value)
                {
                    values.insert(values.begin() + i, value);
                    return true;
                }
            }
            values.push_back(value);
            return true;
        }

        static void split_free_rect_by_used(const amal::irect &free_rect, const amal::irect &used_rect,
                                            acul::vector<amal::irect> &out)
        {
            if (!amal::is_rects_overlap(free_rect, used_rect))
            {
                out.push_back(free_rect);
                return;
            }

            const i32 free_left = amal::get_rect_left(free_rect);
            const i32 free_top = amal::get_rect_top(free_rect);
            const i32 free_right = amal::get_rect_right(free_rect);
            const i32 free_bottom = amal::get_rect_bottom(free_rect);

            const i32 used_left = amal::max(amal::get_rect_left(used_rect), free_left);
            const i32 used_top = amal::max(amal::get_rect_top(used_rect), free_top);
            const i32 used_right = amal::min(amal::get_rect_right(used_rect), free_right);
            const i32 used_bottom = amal::min(amal::get_rect_bottom(used_rect), free_bottom);

            const amal::irect top_rect{free_left, free_top, free_rect.size.x, used_top - free_top};
            const amal::irect bottom_rect{free_left, used_bottom, free_rect.size.x, free_bottom - used_bottom};
            const amal::irect left_rect{free_left, used_top, used_left - free_left, used_bottom - used_top};
            const amal::irect right_rect{used_right, used_top, free_right - used_right, used_bottom - used_top};

            if (!amal::is_rect_empty(top_rect)) out.push_back(top_rect);
            if (!amal::is_rect_empty(bottom_rect)) out.push_back(bottom_rect);
            if (!amal::is_rect_empty(left_rect)) out.push_back(left_rect);
            if (!amal::is_rect_empty(right_rect)) out.push_back(right_rect);
        }

        static void prune_free_rects(acul::vector<amal::irect> &free_rects)
        {
            for (u32 i = 0; i < free_rects.size();)
            {
                bool erased = false;
                for (u32 j = 0; j < free_rects.size(); ++j)
                {
                    if (i == j) continue;
                    if (amal::is_rect_contains(free_rects[j], free_rects[i]))
                    {
                        free_rects.erase(free_rects.begin() + i);
                        erased = true;
                        break;
                    }
                }
                if (!erased) ++i;
            }
        }

        static bool try_merge_pair(amal::irect &a, const amal::irect &b)
        {
            if (amal::get_rect_left(a) == amal::get_rect_left(b) && a.size.x == b.size.x)
            {
                if (amal::get_rect_bottom(a) == amal::get_rect_top(b))
                {
                    a.size.y += b.size.y;
                    return true;
                }
                if (amal::get_rect_bottom(b) == amal::get_rect_top(a))
                {
                    a.offset.y = b.offset.y;
                    a.size.y += b.size.y;
                    return true;
                }
            }

            if (amal::get_rect_top(a) == amal::get_rect_top(b) && a.size.y == b.size.y)
            {
                if (amal::get_rect_right(a) == amal::get_rect_left(b))
                {
                    a.size.x += b.size.x;
                    return true;
                }
                if (amal::get_rect_right(b) == amal::get_rect_left(a))
                {
                    a.offset.x = b.offset.x;
                    a.size.x += b.size.x;
                    return true;
                }
            }

            return false;
        }

        static void merge_free_rects(acul::vector<amal::irect> &free_rects)
        {
            bool merged = true;
            while (merged)
            {
                merged = false;
                for (u32 i = 0; i < free_rects.size() && !merged; ++i)
                {
                    for (u32 j = i + 1; j < free_rects.size(); ++j)
                    {
                        if (try_merge_pair(free_rects[i], free_rects[j]))
                        {
                            free_rects.erase(free_rects.begin() + j);
                            merged = true;
                            break;
                        }
                    }
                }
            }
        }

        static bool validate_locked_rects(const amal::ivec2 &atlas_size, const acul::vector<amal::irect> &rects,
                                          u32 locked_count)
        {
            const amal::irect atlas_rect = {{0}, atlas_size};
            if (amal::get_rect_left(atlas_rect) != 0 || amal::get_rect_top(atlas_rect) != 0) return false;
            if (amal::is_rect_empty(atlas_rect)) return false;
            if (locked_count > rects.size()) return false;

            for (u32 i = 0; i < locked_count; ++i)
            {
                const auto &rect = rects[i];
                if (amal::is_rect_empty(rect)) return false;
                if (!amal::is_rect_contains(atlas_rect, rect)) return false;
                for (u32 j = i + 1; j < locked_count; ++j)
                    if (amal::is_rects_overlap(rect, rects[j])) return false;
            }
            return true;
        }

        static void build_free_rects(const amal::irect &atlas_rect, const acul::vector<amal::irect> &locked_rects,
                                     acul::vector<amal::irect> &free_rects)
        {
            free_rects.clear();
            free_rects.push_back(atlas_rect);

            for (u32 i = 0; i < locked_rects.size(); ++i)
            {
                acul::vector<amal::irect> next_free_rects;
                for (u32 j = 0; j < free_rects.size(); ++j)
                    split_free_rect_by_used(free_rects[j], locked_rects[i], next_free_rects);
                free_rects = next_free_rects;
                prune_free_rects(free_rects);
                merge_free_rects(free_rects);
            }
        }

        static void update_free_rects_max_rects(acul::vector<amal::irect> &free_rects, const amal::irect &used_rect)
        {
            acul::vector<amal::irect> new_free_rects;

            for (u32 i = 0; i < free_rects.size();)
            {
                if (!amal::is_rects_overlap(free_rects[i], used_rect))
                {
                    ++i;
                    continue;
                }

                const amal::irect free_rect = free_rects[i];
                free_rects[i] = free_rects.back();
                free_rects.pop_back();

                const i32 free_left = amal::get_rect_left(free_rect);
                const i32 free_top = amal::get_rect_top(free_rect);
                const i32 free_right = amal::get_rect_right(free_rect);
                const i32 free_bottom = amal::get_rect_bottom(free_rect);

                if (used_rect.offset.y > free_top && used_rect.offset.y < free_bottom)
                {
                    const amal::irect top_rect{free_left, free_top, free_rect.size.x, used_rect.offset.y - free_top};
                    if (!amal::is_rect_empty(top_rect)) new_free_rects.push_back(top_rect);
                }

                const i32 used_bottom = amal::get_rect_bottom(used_rect);
                if (used_bottom < free_bottom)
                {
                    const amal::irect bottom_rect{free_left, used_bottom, free_rect.size.x, free_bottom - used_bottom};
                    if (!amal::is_rect_empty(bottom_rect)) new_free_rects.push_back(bottom_rect);
                }

                if (used_rect.offset.x > free_left && used_rect.offset.x < free_right)
                {
                    const amal::irect left_rect{free_left, free_top, used_rect.offset.x - free_left, free_rect.size.y};
                    if (!amal::is_rect_empty(left_rect)) new_free_rects.push_back(left_rect);
                }

                const i32 used_right = amal::get_rect_right(used_rect);
                if (used_right < free_right)
                {
                    const amal::irect right_rect{used_right, free_top, free_right - used_right, free_rect.size.y};
                    if (!amal::is_rect_empty(right_rect)) new_free_rects.push_back(right_rect);
                }
            }

            free_rects.insert(free_rects.end(), new_free_rects.begin(), new_free_rects.end());
            prune_free_rects(free_rects);
        }

        static void build_skyline_nodes(const amal::irect &atlas_rect, const acul::vector<amal::irect> &locked_rects,
                                        acul::vector<SkylineNode> &nodes)
        {
            nodes.clear();

            acul::vector<i32> x_edges;
            try_push_unique_sorted(x_edges, 0);
            try_push_unique_sorted(x_edges, atlas_rect.size.x);

            for (u32 i = 0; i < locked_rects.size(); ++i)
            {
                const i32 x0 = amal::get_rect_left(locked_rects[i]);
                const i32 x1 = amal::get_rect_right(locked_rects[i]);
                if (x0 > 0 && x0 < atlas_rect.size.x) try_push_unique_sorted(x_edges, x0);
                if (x1 > 0 && x1 < atlas_rect.size.x) try_push_unique_sorted(x_edges, x1);
            }

            if (x_edges.size() < 2) return;

            for (u32 i = 0; i + 1 < x_edges.size(); ++i)
            {
                const i32 segment_x = x_edges[i];
                const i32 segment_width = x_edges[i + 1] - x_edges[i];
                if (segment_width <= 0) continue;

                i32 segment_y = 0;
                for (u32 j = 0; j < locked_rects.size(); ++j)
                {
                    if (amal::get_rect_left(locked_rects[j]) <= segment_x &&
                        amal::get_rect_right(locked_rects[j]) >= segment_x + segment_width)
                        segment_y = amal::max(segment_y, amal::get_rect_bottom(locked_rects[j]));
                }

                if (!nodes.empty() && nodes.back().y == segment_y && nodes.back().x + nodes.back().width == segment_x)
                    nodes.back().width += segment_width;
                else
                    nodes.push_back({segment_x, segment_y, segment_width});
            }
        }

        static bool skyline_rect_fits(const acul::vector<SkylineNode> &nodes, u32 node_index, i32 rect_width_value,
                                      i32 rect_height_value, i32 atlas_height, i32 &out_y, i32 &out_wasted_area)
        {
            if (node_index >= nodes.size()) return false;
            if (rect_width_value <= 0 || rect_height_value <= 0) return false;
            if (nodes[node_index].x + rect_width_value > nodes.back().x + nodes.back().width) return false;

            i32 width_left = rect_width_value;
            i32 y = nodes[node_index].y;
            i32 wasted_area = 0;
            i32 x = nodes[node_index].x;

            for (u32 i = node_index; i < nodes.size(); ++i)
            {
                y = amal::max(y, nodes[i].y);
                if (y + rect_height_value > atlas_height) return false;

                const i32 used_width = amal::min(width_left, nodes[i].width);
                wasted_area += used_width * (y - nodes[i].y);
                width_left -= nodes[i].width;
                if (width_left <= 0) break;
                x = nodes[i].x + nodes[i].width;
                if (x + width_left > nodes.back().x + nodes.back().width) return false;
            }

            out_y = y;
            out_wasted_area = wasted_area;
            return true;
        }

        static SkylineCandidate find_skyline_candidate(const amal::irect &atlas_rect,
                                                       const acul::vector<SkylineNode> &nodes,
                                                       const amal::irect &source_rect,
                                                       SkylineHeuristic::enum_type heuristic)
        {
            SkylineCandidate best{};
            for (u32 i = 0; i < nodes.size(); ++i)
            {
                i32 y = 0;
                i32 wasted_area = 0;
                if (!skyline_rect_fits(nodes, i, source_rect.size.x, source_rect.size.y, atlas_rect.size.y, y,
                                       wasted_area))
                    continue;

                SkylineCandidate current{};
                current.valid = true;
                current.from_waste_map = false;
                current.rect = {nodes[i].x, y, source_rect.size.x, source_rect.size.y};
                current.node_index = i;

                if (heuristic == SkylineHeuristic::bottom_left)
                {
                    current.score_primary = y + source_rect.size.y;
                    current.score_secondary = nodes[i].x;
                }
                else
                {
                    current.score_primary = wasted_area;
                    current.score_secondary = y + source_rect.size.y;
                }

                if (!best.valid || current.score_primary < best.score_primary ||
                    (current.score_primary == best.score_primary && current.score_secondary < best.score_secondary) ||
                    (current.score_primary == best.score_primary && current.score_secondary == best.score_secondary &&
                     amal::get_rect_left(current.rect) < amal::get_rect_left(best.rect)))
                    best = current;
            }

            return best;
        }

        static SkylineCandidate find_waste_map_candidate(const acul::vector<amal::irect> &free_rects,
                                                         const amal::irect &source_rect)
        {
            SkylineCandidate best{};

            for (u32 i = 0; i < free_rects.size(); ++i)
            {
                if (source_rect.size.x > free_rects[i].size.x || source_rect.size.y > free_rects[i].size.y) continue;

                SkylineCandidate current{};
                current.valid = true;
                current.from_waste_map = true;
                current.rect = {amal::get_rect_left(free_rects[i]), amal::get_rect_top(free_rects[i]),
                                source_rect.size.x, source_rect.size.y};
                current.score_primary = amal::get_rect_area(free_rects[i]) - amal::get_rect_area(current.rect);
                current.score_secondary =
                    amal::min(free_rects[i].size.x - source_rect.size.x, free_rects[i].size.y - source_rect.size.y);

                if (!best.valid || current.score_primary < best.score_primary ||
                    (current.score_primary == best.score_primary && current.score_secondary < best.score_secondary) ||
                    (current.score_primary == best.score_primary && current.score_secondary == best.score_secondary &&
                     amal::get_rect_top(current.rect) < amal::get_rect_top(best.rect)) ||
                    (current.score_primary == best.score_primary && current.score_secondary == best.score_secondary &&
                     amal::get_rect_top(current.rect) == amal::get_rect_top(best.rect) &&
                     amal::get_rect_left(current.rect) < amal::get_rect_left(best.rect)))
                    best = current;
            }

            return best;
        }

        static i32 common_interval_length(i32 a0, i32 a1, i32 b0, i32 b1)
        {
            if (a1 <= b0 || b1 <= a0) return 0;
            return amal::min(a1, b1) - amal::max(a0, b0);
        }

        static i32 contact_point_score_node(const acul::vector<amal::irect> &used_rects, const amal::ivec2 &atlas_size,
                                            i32 x, i32 y, i32 width, i32 height)
        {
            i32 score = 0;

            if (x == 0 || x + width == atlas_size.x) score += height;
            if (y == 0 || y + height == atlas_size.y) score += width;

            for (u32 i = 0; i < used_rects.size(); ++i)
            {
                if (amal::get_rect_left(used_rects[i]) == x + width ||
                    amal::get_rect_right(used_rects[i]) == x)
                    score += common_interval_length(amal::get_rect_top(used_rects[i]),
                                                    amal::get_rect_bottom(used_rects[i]), y, y + height);
                if (amal::get_rect_top(used_rects[i]) == y + height ||
                    amal::get_rect_bottom(used_rects[i]) == y)
                    score += common_interval_length(amal::get_rect_left(used_rects[i]),
                                                    amal::get_rect_right(used_rects[i]), x, x + width);
            }

            return score;
        }

        static MaxRectsCandidate find_position_best_short_side_fit(const acul::vector<amal::irect> &free_rects,
                                                                   const amal::irect &source_rect, bool allow_flip)
        {
            MaxRectsCandidate best{};
            best.score_primary = std::numeric_limits<i32>::max();
            best.score_secondary = std::numeric_limits<i32>::max();

            const i32 width = source_rect.size.x;
            const i32 height = source_rect.size.y;

            for (u32 i = 0; i < free_rects.size(); ++i)
            {
                if (free_rects[i].size.x >= width && free_rects[i].size.y >= height)
                {
                    const i32 leftover_h = amal::abs(free_rects[i].size.x - width);
                    const i32 leftover_v = amal::abs(free_rects[i].size.y - height);
                    const i32 short_side = amal::min(leftover_h, leftover_v);
                    const i32 long_side = amal::max(leftover_h, leftover_v);
                    if (!best.valid || short_side < best.score_primary ||
                        (short_side == best.score_primary && long_side < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = false;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, width, height};
                        best.score_primary = short_side;
                        best.score_secondary = long_side;
                    }
                }

                if (allow_flip && free_rects[i].size.x >= height && free_rects[i].size.y >= width)
                {
                    const i32 leftover_h = amal::abs(free_rects[i].size.x - height);
                    const i32 leftover_v = amal::abs(free_rects[i].size.y - width);
                    const i32 short_side = amal::min(leftover_h, leftover_v);
                    const i32 long_side = amal::max(leftover_h, leftover_v);
                    if (!best.valid || short_side < best.score_primary ||
                        (short_side == best.score_primary && long_side < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = true;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, height, width};
                        best.score_primary = short_side;
                        best.score_secondary = long_side;
                    }
                }
            }

            return best;
        }

        static void insert_waste_rect(acul::vector<amal::irect> &waste_rects, const amal::irect &rect)
        {
            if (amal::is_rect_empty(rect)) return;

            for (u32 i = 0; i < waste_rects.size();)
            {
                if (amal::is_rect_contains(waste_rects[i], rect)) return;
                if (amal::is_rect_contains(rect, waste_rects[i]))
                {
                    waste_rects.erase(waste_rects.begin() + i);
                    continue;
                }
                ++i;
            }

            waste_rects.push_back(rect);
        }

        static void merge_skyline_nodes(acul::vector<SkylineNode> &nodes)
        {
            for (u32 i = 0; i + 1 < nodes.size();)
            {
                if (nodes[i].y == nodes[i + 1].y)
                {
                    nodes[i].width += nodes[i + 1].width;
                    nodes.erase(nodes.begin() + i + 1);
                    continue;
                }
                ++i;
            }
        }

        static void add_skyline_level(acul::vector<SkylineNode> &nodes, acul::vector<amal::irect> &waste_rects,
                                      const SkylineCandidate &candidate)
        {
            const auto &rect = candidate.rect;
            const i32 rect_left = amal::get_rect_left(rect);
            const i32 rect_right = amal::get_rect_right(rect);
            const i32 rect_top = amal::get_rect_top(rect);

            for (u32 i = candidate.node_index; i < nodes.size(); ++i)
            {
                const i32 node_left = nodes[i].x;
                const i32 node_right = nodes[i].x + nodes[i].width;
                if (node_left >= rect_right) break;

                const i32 overlap_left = amal::max(node_left, rect_left);
                const i32 overlap_right = amal::min(node_right, rect_right);
                if (overlap_left >= overlap_right) continue;

                if (nodes[i].y < rect_top)
                    insert_waste_rect(waste_rects,
                                      {overlap_left, nodes[i].y, overlap_right - overlap_left, rect_top - nodes[i].y});
            }

            nodes.insert(nodes.begin() + candidate.node_index, {rect_left, amal::get_rect_bottom(rect), rect.size.x});

            for (u32 i = candidate.node_index + 1; i < nodes.size();)
            {
                const i32 previous_right = nodes[i - 1].x + nodes[i - 1].width;
                if (nodes[i].x >= previous_right) break;

                const i32 shrink = previous_right - nodes[i].x;
                nodes[i].x += shrink;
                nodes[i].width -= shrink;
                if (nodes[i].width <= 0)
                {
                    nodes.erase(nodes.begin() + i);
                    continue;
                }
                break;
            }

            merge_skyline_nodes(nodes);
            prune_free_rects(waste_rects);
        }

        static MaxRectsCandidate find_position_best_long_side_fit(const acul::vector<amal::irect> &free_rects,
                                                                  const amal::irect &source_rect, bool allow_flip)
        {
            MaxRectsCandidate best{};
            best.score_primary = std::numeric_limits<i32>::max();
            best.score_secondary = std::numeric_limits<i32>::max();

            const i32 width = source_rect.size.x;
            const i32 height = source_rect.size.y;

            for (u32 i = 0; i < free_rects.size(); ++i)
            {
                if (free_rects[i].size.x >= width && free_rects[i].size.y >= height)
                {
                    const i32 leftover_h = amal::abs(free_rects[i].size.x - width);
                    const i32 leftover_v = amal::abs(free_rects[i].size.y - height);
                    const i32 short_side = amal::min(leftover_h, leftover_v);
                    const i32 long_side = amal::max(leftover_h, leftover_v);
                    if (!best.valid || long_side < best.score_primary ||
                        (long_side == best.score_primary && short_side < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = false;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, width, height};
                        best.score_primary = long_side;
                        best.score_secondary = short_side;
                    }
                }

                if (allow_flip && free_rects[i].size.x >= height && free_rects[i].size.y >= width)
                {
                    const i32 leftover_h = amal::abs(free_rects[i].size.x - height);
                    const i32 leftover_v = amal::abs(free_rects[i].size.y - width);
                    const i32 short_side = amal::min(leftover_h, leftover_v);
                    const i32 long_side = amal::max(leftover_h, leftover_v);
                    if (!best.valid || long_side < best.score_primary ||
                        (long_side == best.score_primary && short_side < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = true;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, height, width};
                        best.score_primary = long_side;
                        best.score_secondary = short_side;
                    }
                }
            }

            return best;
        }

        static MaxRectsCandidate find_position_best_area_fit(const acul::vector<amal::irect> &free_rects,
                                                             const amal::irect &source_rect, bool allow_flip)
        {
            MaxRectsCandidate best{};
            best.score_primary = std::numeric_limits<i32>::max();
            best.score_secondary = std::numeric_limits<i32>::max();

            const i32 width = source_rect.size.x;
            const i32 height = source_rect.size.y;

            for (u32 i = 0; i < free_rects.size(); ++i)
            {
                const i32 area_fit = free_rects[i].size.x * free_rects[i].size.y - width * height;

                if (free_rects[i].size.x >= width && free_rects[i].size.y >= height)
                {
                    const i32 leftover_h = amal::abs(free_rects[i].size.x - width);
                    const i32 leftover_v = amal::abs(free_rects[i].size.y - height);
                    const i32 short_side = amal::min(leftover_h, leftover_v);
                    if (!best.valid || area_fit < best.score_primary ||
                        (area_fit == best.score_primary && short_side < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = false;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, width, height};
                        best.score_primary = area_fit;
                        best.score_secondary = short_side;
                    }
                }

                if (allow_flip && free_rects[i].size.x >= height && free_rects[i].size.y >= width)
                {
                    const i32 leftover_h = amal::abs(free_rects[i].size.x - height);
                    const i32 leftover_v = amal::abs(free_rects[i].size.y - width);
                    const i32 short_side = amal::min(leftover_h, leftover_v);
                    if (!best.valid || area_fit < best.score_primary ||
                        (area_fit == best.score_primary && short_side < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = true;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, height, width};
                        best.score_primary = area_fit;
                        best.score_secondary = short_side;
                    }
                }
            }

            return best;
        }

        static MaxRectsCandidate find_position_bottom_left(const acul::vector<amal::irect> &free_rects,
                                                           const amal::irect &source_rect, bool allow_flip)
        {
            MaxRectsCandidate best{};
            best.score_primary = std::numeric_limits<i32>::max();
            best.score_secondary = std::numeric_limits<i32>::max();

            const i32 width = source_rect.size.x;
            const i32 height = source_rect.size.y;

            for (u32 i = 0; i < free_rects.size(); ++i)
            {
                if (free_rects[i].size.x >= width && free_rects[i].size.y >= height)
                {
                    const i32 top_y = free_rects[i].offset.y + height;
                    if (!best.valid || top_y < best.score_primary ||
                        (top_y == best.score_primary && free_rects[i].offset.x < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = false;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, width, height};
                        best.score_primary = top_y;
                        best.score_secondary = free_rects[i].offset.x;
                    }
                }

                if (allow_flip && free_rects[i].size.x >= height && free_rects[i].size.y >= width)
                {
                    const i32 top_y = free_rects[i].offset.y + width;
                    if (!best.valid || top_y < best.score_primary ||
                        (top_y == best.score_primary && free_rects[i].offset.x < best.score_secondary))
                    {
                        best.valid = true;
                        best.flipped = true;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, height, width};
                        best.score_primary = top_y;
                        best.score_secondary = free_rects[i].offset.x;
                    }
                }
            }

            return best;
        }

        static MaxRectsCandidate find_position_contact_point(const acul::vector<amal::irect> &free_rects,
                                                             const acul::vector<amal::irect> &used_rects,
                                                             const amal::ivec2 &atlas_size,
                                                             const amal::irect &source_rect, bool allow_flip)
        {
            MaxRectsCandidate best{};
            best.score_primary = std::numeric_limits<i32>::min();
            best.score_secondary = 0;

            const i32 width = source_rect.size.x;
            const i32 height = source_rect.size.y;

            for (u32 i = 0; i < free_rects.size(); ++i)
            {
                if (free_rects[i].size.x >= width && free_rects[i].size.y >= height)
                {
                    const i32 score =
                        contact_point_score_node(used_rects, atlas_size, free_rects[i].offset.x, free_rects[i].offset.y,
                                                 width, height);
                    if (!best.valid || score > best.score_primary)
                    {
                        best.valid = true;
                        best.flipped = false;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, width, height};
                        best.score_primary = score;
                    }
                }

                if (allow_flip && free_rects[i].size.x >= height && free_rects[i].size.y >= width)
                {
                    const i32 score =
                        contact_point_score_node(used_rects, atlas_size, free_rects[i].offset.x, free_rects[i].offset.y,
                                                 height, width);
                    if (!best.valid || score > best.score_primary)
                    {
                        best.valid = true;
                        best.flipped = true;
                        best.rect = {free_rects[i].offset.x, free_rects[i].offset.y, height, width};
                        best.score_primary = score;
                    }
                }
            }

            return best;
        }

        static MaxRectsCandidate find_max_rects_candidate(const acul::vector<amal::irect> &free_rects,
                                                          const acul::vector<amal::irect> &used_rects,
                                                          const amal::ivec2 &atlas_size,
                                                          const amal::irect &source_rect,
                                                          MaxRectsHeuristic::enum_type heuristic, bool allow_flip)
        {
            switch (heuristic)
            {
                case MaxRectsHeuristic::best_long_side_fit:
                    return find_position_best_long_side_fit(free_rects, source_rect, allow_flip);
                case MaxRectsHeuristic::best_area_fit:
                    return find_position_best_area_fit(free_rects, source_rect, allow_flip);
                case MaxRectsHeuristic::bottom_left_rule:
                    return find_position_bottom_left(free_rects, source_rect, allow_flip);
                case MaxRectsHeuristic::contact_point_rule:
                    return find_position_contact_point(free_rects, used_rects, atlas_size, source_rect, allow_flip);
                default:
                    return find_position_best_short_side_fit(free_rects, source_rect, allow_flip);
            }
        }

        static bool is_better_max_rects_candidate(const MaxRectsCandidate &candidate, const MaxRectsCandidate &best,
                                                  MaxRectsHeuristic::enum_type heuristic)
        {
            if (!candidate.valid) return false;
            if (!best.valid) return true;

            if (heuristic == MaxRectsHeuristic::contact_point_rule)
            {
                if (candidate.score_primary != best.score_primary)
                    return candidate.score_primary > best.score_primary;
                return candidate.score_secondary < best.score_secondary;
            }

            if (candidate.score_primary != best.score_primary) return candidate.score_primary < best.score_primary;
            return candidate.score_secondary < best.score_secondary;
        }

        static amal::irect pad_rect(const amal::irect &rect, i32 padding)
        {
            if (padding <= 0) return rect;
            return {rect.offset.x - padding, rect.offset.y - padding, rect.size.x + padding * 2,
                    rect.size.y + padding * 2};
        }

        static amal::irect make_padded_size_rect(const amal::irect &rect, i32 padding)
        {
            if (padding <= 0) return {{0}, rect.size};
            return {{0, 0}, {rect.size.x + padding * 2, rect.size.y + padding * 2}};
        }

        static amal::irect unpad_rect(const amal::irect &rect, i32 padding, const amal::ivec2 &content_size)
        {
            if (padding <= 0) return {rect.offset, content_size};
            return {{rect.offset.x + padding, rect.offset.y + padding}, content_size};
        }

        static amal::irect scale_rect_size(const amal::irect &rect, f32 scale)
        {
            if (scale >= 1.0f) return {{0}, rect.size};

            amal::irect out = rect;
            out.offset = {0, 0};
            out.size.x = amal::max(static_cast<i32>(rect.size.x * scale), 1);
            out.size.y = amal::max(static_cast<i32>(rect.size.y * scale), 1);
            return out;
        }

        static MaxRectsAttemptResult try_pack_max_rects(const amal::ivec2 &atlas_size, u32 locked_count,
                                                        const acul::vector<amal::irect> &input_rects,
                                                        acul::vector<amal::irect> &output_rects,
                                                        acul::vector<MaxRectsTransform> *transforms,
                                                        MaxRectsHeuristic::enum_type heuristic,
                                                        MaxRectsTransform allowed_transforms, f32 scale, i32 padding)
        {
            MaxRectsAttemptResult result{};
            const bool allow_flip = allowed_transforms & MaxRectsTransformBits::rotate;
            const bool mark_scale = scale < 1.0f;
            const amal::irect atlas_rect = {{0}, atlas_size};

            output_rects = input_rects;
            acul::vector<amal::irect> free_rects;
            free_rects.push_back(atlas_rect);
            acul::vector<amal::irect> used_rects;
            used_rects.reserve(input_rects.size());
            for (u32 i = 0; i < locked_count; ++i)
            {
                const amal::irect padded_locked = pad_rect(input_rects[i], padding);
                used_rects.push_back(padded_locked);
                update_free_rects_max_rects(free_rects, padded_locked);
            }

            if (transforms)
            {
                if (transforms->size() < input_rects.size()) transforms->resize(input_rects.size());
                for (u32 i = 0; i < input_rects.size(); ++i) (*transforms)[i] = MaxRectsTransformBits::none;
            }

            acul::vector<u32> remaining_indices;
            remaining_indices.reserve(input_rects.size() - locked_count);
            for (u32 i = locked_count; i < input_rects.size(); ++i)
                if (!amal::is_rect_empty(input_rects[i])) remaining_indices.push_back(i);

            while (!remaining_indices.empty())
            {
                MaxRectsCandidate best_candidate{};
                u32 best_input_index = 0;
                u32 best_remaining_index = 0;

                for (u32 remaining_index = 0; remaining_index < remaining_indices.size(); ++remaining_index)
                {
                    const u32 input_index = remaining_indices[remaining_index];
                    const amal::irect scaled_rect = scale_rect_size(input_rects[input_index], scale);
                    const amal::irect padded_rect = make_padded_size_rect(scaled_rect, padding);
                    const auto candidate =
                        find_max_rects_candidate(free_rects, used_rects, atlas_size, padded_rect, heuristic, allow_flip);
                    if (!is_better_max_rects_candidate(candidate, best_candidate, heuristic)) continue;

                    best_candidate = candidate;
                    best_input_index = input_index;
                    best_remaining_index = remaining_index;
                }

                if (!best_candidate.valid) break;

                const amal::irect scaled_rect = scale_rect_size(input_rects[best_input_index], scale);
                output_rects[best_input_index] = unpad_rect(best_candidate.rect, padding, scaled_rect.size);
                used_rects.push_back(best_candidate.rect);
                update_free_rects_max_rects(free_rects, best_candidate.rect);
                remaining_indices.erase(remaining_indices.begin() + best_remaining_index);
                ++result.packed_count;

                if (!transforms) continue;

                MaxRectsTransform flags = MaxRectsTransformBits::none;
                if (best_candidate.flipped) flags |= MaxRectsTransformBits::rotate;
                if (mark_scale) flags |= MaxRectsTransformBits::scale;
                (*transforms)[best_input_index] = flags;
            }

            result.packed = remaining_indices.empty();
            for (u32 i = 0; i < remaining_indices.size(); ++i)
                result.unpacked_area += static_cast<u64>(input_rects[remaining_indices[i]].size.x) *
                                        static_cast<u64>(input_rects[remaining_indices[i]].size.y);
            return result;
        }

        static void update_best_max_rects_attempt(u32 locked_count, const MaxRectsAttemptResult &attempt,
                                                  const acul::vector<amal::irect> &attempt_rects,
                                                  const acul::vector<MaxRectsTransform> *attempt_transforms,
                                                  MaxRectsPackResult &best_result, acul::vector<amal::irect> &best_rects,
                                                  acul::vector<MaxRectsTransform> &best_transforms, f32 scale)
        {
            const u32 attempt_packed_count = locked_count + attempt.packed_count;
            if (attempt_packed_count < best_result.packed_count) return;
            if (attempt_packed_count == best_result.packed_count)
            {
                if (attempt.unpacked_area > best_result.unpacked_area) return;
                if (attempt.unpacked_area == best_result.unpacked_area && scale <= best_result.scale) return;
            }

            best_result.packed = attempt.packed;
            best_result.scale = scale;
            best_result.packed_count = attempt_packed_count;
            best_result.unpacked_area = attempt.unpacked_area;
            best_rects = attempt_rects;
            if (attempt_transforms) best_transforms = *attempt_transforms;
        }

        SkylinePacker::SkylinePacker(const amal::ivec2 &atlas_size, i32 padding) { reset(atlas_size, padding); }

        void SkylinePacker::reset(const amal::ivec2 &atlas_size, i32 padding)
        {
            _atlas_size = atlas_size;
            _padding = amal::max(padding, 0);
            _skyline_nodes.clear();
            _waste_rects.clear();
            _used_rects.clear();
            if (atlas_size.x > 0 && atlas_size.y > 0) _skyline_nodes.push_back({0, 0, atlas_size.x});
        }

        bool SkylinePacker::add_locked(const amal::irect &rect)
        {
            if (amal::is_rect_empty(rect)) return false;
            const amal::irect padded_rect = pad_rect(rect, _padding);
            if (!amal::is_rect_contains(amal::irect{{0}, _atlas_size}, padded_rect)) return false;

            _used_rects.push_back(padded_rect);
            build_free_rects(amal::irect{{0}, _atlas_size}, _used_rects, _waste_rects);
            build_skyline_nodes(amal::irect{{0}, _atlas_size}, _used_rects, _skyline_nodes);
            if (_skyline_nodes.empty()) _skyline_nodes.push_back({0, 0, _atlas_size.x});
            return true;
        }

        bool SkylinePacker::pack_rect(amal::irect &rect, SkylineHeuristic::enum_type heuristic)
        {
            if (amal::is_rect_empty(rect)) return false;
            const amal::irect source_rect = rect;
            const amal::irect padded_rect = make_padded_size_rect(source_rect, _padding);

            const auto waste_candidate = find_waste_map_candidate(_waste_rects, padded_rect);
            if (waste_candidate.valid)
            {
                rect = unpad_rect(waste_candidate.rect, _padding, source_rect.size);
                _used_rects.push_back(waste_candidate.rect);
                update_free_rects_max_rects(_waste_rects, waste_candidate.rect);
                return true;
            }

            const auto skyline_candidate = find_skyline_candidate(amal::irect{{0}, _atlas_size}, _skyline_nodes,
                                                                  padded_rect, heuristic);
            if (!skyline_candidate.valid) return false;

            rect = unpad_rect(skyline_candidate.rect, _padding, source_rect.size);
            _used_rects.push_back(skyline_candidate.rect);
            add_skyline_level(_skyline_nodes, _waste_rects, skyline_candidate);
            return true;
        }

        bool SkylinePacker::pack_rects(acul::vector<amal::irect> &rects, u32 locked_count,
                                       SkylineHeuristic::enum_type heuristic)
        {
            reset(_atlas_size);
            for (u32 i = 0; i < locked_count; ++i)
                if (!add_locked(rects[i])) return false;
            for (u32 i = locked_count; i < rects.size(); ++i)
                if (!pack_rect(rects[i], heuristic)) return false;
            return true;
        }

        MaxRectsPacker::MaxRectsPacker(const amal::ivec2 &atlas_size, i32 padding) { reset(atlas_size, padding); }

        void MaxRectsPacker::reset(const amal::ivec2 &atlas_size, i32 padding)
        {
            _atlas_size = atlas_size;
            _padding = amal::max(padding, 0);
            _free_rects.clear();
            _used_rects.clear();
            if (atlas_size.x > 0 && atlas_size.y > 0) _free_rects.push_back({{0}, atlas_size});
        }

        bool MaxRectsPacker::add_locked(const amal::irect &rect)
        {
            if (amal::is_rect_empty(rect)) return false;
            const amal::irect padded_rect = pad_rect(rect, _padding);
            if (!amal::is_rect_contains(amal::irect{{0}, _atlas_size}, padded_rect)) return false;
            _used_rects.push_back(padded_rect);
            update_free_rects_max_rects(_free_rects, padded_rect);
            return true;
        }

        bool MaxRectsPacker::pack_rect(amal::irect &rect, MaxRectsTransform *transform,
                                       MaxRectsHeuristic::enum_type heuristic, MaxRectsTransform allowed_transforms)
        {
            if (amal::is_rect_empty(rect)) return false;
            const amal::irect source_rect = rect;
            const amal::irect padded_rect = make_padded_size_rect(rect, _padding);

            const bool allow_flip = allowed_transforms & MaxRectsTransformBits::rotate;
            const auto candidate =
                find_max_rects_candidate(_free_rects, _used_rects, _atlas_size, padded_rect, heuristic, allow_flip);
            if (!candidate.valid) return false;

            rect = unpad_rect(candidate.rect, _padding,
                              candidate.flipped ? amal::ivec2{source_rect.size.y, source_rect.size.x} : source_rect.size);
            _used_rects.push_back(candidate.rect);
            update_free_rects_max_rects(_free_rects, candidate.rect);

            if (transform)
            {
                *transform = MaxRectsTransformBits::none;
                if (candidate.flipped) *transform |= MaxRectsTransformBits::rotate;
            }

            return true;
        }

        MaxRectsPackResult MaxRectsPacker::pack_rects(acul::vector<amal::irect> &rects, u32 locked_count,
                                                      acul::vector<MaxRectsTransform> *transforms,
                                                      MaxRectsHeuristic::enum_type heuristic,
                                                      MaxRectsTransform allowed_transforms)
        {
            MaxRectsPackResult result{};
            reset(_atlas_size);

            if (transforms)
            {
                if (transforms->size() < rects.size()) transforms->resize(rects.size());
                for (u32 i = 0; i < rects.size(); ++i) (*transforms)[i] = MaxRectsTransformBits::none;
            }

            for (u32 i = 0; i < locked_count; ++i)
                if (!add_locked(rects[i]))
                {
                    result.packed_count = i;
                    return result;
                }

            for (u32 i = locked_count; i < rects.size(); ++i)
            {
                MaxRectsTransform transform = MaxRectsTransformBits::none;
                if (!pack_rect(rects[i], transforms ? &transform : nullptr, heuristic, allowed_transforms))
                {
                    result.packed_count = i;
                    for (u32 j = i; j < rects.size(); ++j)
                        result.unpacked_area += static_cast<u64>(rects[j].size.x) * static_cast<u64>(rects[j].size.y);
                    return result;
                }
                if (transforms) (*transforms)[i] = transform;
            }

            result.packed = true;
            result.packed_count = static_cast<u32>(rects.size());
            return result;
        }

        bool pack_skyline(const amal::ivec2 &atlas_size, u32 locked_count, acul::vector<amal::irect> &rects,
                          SkylineHeuristic::enum_type heuristic, i32 padding)
        {
            if (!validate_locked_rects(atlas_size, rects, locked_count)) return false;
            SkylinePacker packer(atlas_size, padding);
            return packer.pack_rects(rects, locked_count, heuristic);
        }

        MaxRectsPackResult pack_max_rects(const amal::ivec2 &atlas_size, u32 locked_count,
                                          acul::vector<amal::irect> &rects,
                                          acul::vector<MaxRectsTransform> *transforms,
                                          MaxRectsHeuristic::enum_type heuristic,
                                          MaxRectsTransform allowed_transforms, i32 padding)
        {
            MaxRectsPackResult result{};
            if (!validate_locked_rects(atlas_size, rects, locked_count))
            {
                result.packed_count = locked_count;
                return result;
            }

            acul::vector<amal::irect> best_rects = rects;
            acul::vector<MaxRectsTransform> best_transforms;
            acul::vector<MaxRectsTransform> attempt_transforms;
            acul::vector<amal::irect> attempt_rects;

            const auto first_attempt =
                try_pack_max_rects(atlas_size, locked_count, rects, attempt_rects, transforms ? &attempt_transforms : nullptr,
                                   heuristic, allowed_transforms, 1.0f, padding);
            if (first_attempt.packed)
            {
                rects = attempt_rects;
                if (transforms) *transforms = attempt_transforms;
                result.packed = true;
                result.scale = 1.0f;
                result.packed_count = static_cast<u32>(rects.size());
                return result;
            }

            update_best_max_rects_attempt(locked_count, first_attempt, attempt_rects,
                                          transforms ? &attempt_transforms : nullptr, result, best_rects,
                                          best_transforms, 1.0f);

            if (!(allowed_transforms & MaxRectsTransformBits::scale))
            {
                rects = best_rects;
                if (transforms && !best_transforms.empty()) *transforms = best_transforms;
                return result;
            }

            u64 unlocked_area = 0;
            for (u32 i = locked_count; i < rects.size(); ++i)
            {
                if (amal::is_rect_empty(rects[i])) continue;
                unlocked_area += static_cast<u64>(rects[i].size.x) * static_cast<u64>(rects[i].size.y);
            }
            if (unlocked_area == 0) return result;

            const u64 atlas_area = static_cast<u64>(atlas_size.x) * static_cast<u64>(atlas_size.y);
            u64 locked_area = 0;
            for (u32 i = 0; i < locked_count; ++i)
                locked_area += static_cast<u64>(rects[i].size.x) * static_cast<u64>(rects[i].size.y);
            if (locked_area >= atlas_area) return result;

            const u64 available_area = atlas_area - locked_area;
            f32 failed_scale = 1.0f;
            f32 success_scale = amal::sqrt(static_cast<f32>(available_area) / static_cast<f32>(unlocked_area));
            success_scale = amal::min(success_scale, 0.999f);
            if (success_scale <= 0.0f) return result;

            bool has_success_scale = false;
            for (u32 attempt = 0; attempt < 24; ++attempt)
            {
                const auto scaled_attempt =
                    try_pack_max_rects(atlas_size, locked_count, rects, attempt_rects,
                                       transforms ? &attempt_transforms : nullptr, heuristic, allowed_transforms,
                                       success_scale, padding);
                update_best_max_rects_attempt(locked_count, scaled_attempt, attempt_rects,
                                              transforms ? &attempt_transforms : nullptr, result, best_rects,
                                              best_transforms, success_scale);

                if (scaled_attempt.packed)
                {
                    has_success_scale = true;
                    break;
                }

                failed_scale = success_scale;
                success_scale *= 0.5f;
                if (success_scale <= 0.000001f) break;
            }

            if (!has_success_scale)
            {
                rects = best_rects;
                if (transforms && !best_transforms.empty()) *transforms = best_transforms;
                return result;
            }

            for (u32 attempt = 0; attempt < 10; ++attempt)
            {
                const f32 probe_scale = (failed_scale + success_scale) * 0.5f;
                const auto probe_attempt =
                    try_pack_max_rects(atlas_size, locked_count, rects, attempt_rects,
                                       transforms ? &attempt_transforms : nullptr, heuristic, allowed_transforms,
                                       probe_scale, padding);
                update_best_max_rects_attempt(locked_count, probe_attempt, attempt_rects,
                                              transforms ? &attempt_transforms : nullptr, result, best_rects,
                                              best_transforms, probe_scale);

                if (probe_attempt.packed)
                    success_scale = probe_scale;
                else
                    failed_scale = probe_scale;
            }

            rects = best_rects;
            if (transforms && !best_transforms.empty()) *transforms = best_transforms;
            return result;
        }
    } // namespace utils
} // namespace umbf
