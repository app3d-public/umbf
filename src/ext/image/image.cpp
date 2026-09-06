#include <acul/io/fs/file.hpp>
#include <cstring>
#include <umbf/ext/image/image.hpp>

namespace umbf::registry
{
    namespace
    {
        u64 pixel_data_size(const Image2D &image)
        {
            if (image.channels.size() > UINT8_MAX) throw acul::runtime_error("Too many image channel names");
            const u64 count = static_cast<u64>(image.width) * image.height;
            const u64 stride = static_cast<u64>(image.format.bytes_per_channel) * image.channels.size();
            if (stride && count > UINT64_MAX / stride) throw acul::runtime_error("Image byte size overflow");
            return count * stride;
        }

        void write_image_info(acul::bin_stream &stream, Image2D *image)
        {
            stream.write(image->width).write(image->height);
            const u8 channels_count = static_cast<u8>(image->channels.size());
            stream.write(channels_count);
            for (auto &channel : image->channels) stream.write(channel);
            stream.write(image->format.bytes_per_channel).write(image->format.type);
        }

        void write_image(acul::bin_stream &stream, umbf::Block *block)
        {
            auto *image = static_cast<Image2D *>(block);
            const u64 size = pixel_data_size(*image);
            if (size && !image->pixels) throw acul::runtime_error("Pixels cannot be null");
            write_image_info(stream, image);
            if (size) stream.write(static_cast<const char *>(image->pixels), size);
        }

        void read_image_info(acul::bin_stream &stream, Image2D *image)
        {
            u8 channels_count;
            stream.read(image->width).read(image->height).read(channels_count);
            image->channels.resize(channels_count);
            for (auto &channel : image->channels) stream.read(channel);
            stream.read(image->format.bytes_per_channel).read(image->format.type);
        }

        umbf::Block *read_image(acul::bin_stream &stream)
        {
            auto *image = acul::alloc<Image2D>();
            try
            {
                read_image_info(stream, image);
                const u64 size = pixel_data_size(*image);
                if (stream.pos() > stream.size() || size > stream.size() - stream.pos())
                    throw acul::runtime_error("Truncated image pixels");
                if (size)
                {
                    image->pixels = acul::alloc_n<char>(static_cast<size_t>(size));
                    if (!image->pixels) throw acul::runtime_error("Failed to allocate image pixels");
                    stream.read(static_cast<char *>(image->pixels), static_cast<size_t>(size));
                }
                return image;
            }
            catch (...)
            {
                image->release_pixels();
                acul::release(image);
                throw;
            }
        }

        void write_image_atlas(acul::bin_stream &stream, umbf::Block *block)
        {
            auto *atlas = static_cast<Atlas *>(block);
            stream.write(atlas->padding).write(static_cast<u16>(atlas->pack_data.size()));
            for (auto &rect : atlas->pack_data)
                stream.write(rect.size.x).write(rect.size.y).write(rect.offset.x).write(rect.offset.y);
        }

        umbf::Block *read_image_atlas(acul::bin_stream &stream)
        {
            auto *atlas = acul::alloc<Atlas>();
            u16 count;
            stream.read(atlas->padding).read(count);
            atlas->pack_data.resize(count);
            for (auto &rect : atlas->pack_data)
                stream.read(rect.size.x).read(rect.size.y).read(rect.offset.x).read(rect.offset.y);
            return atlas;
        }

        void write_image_list(acul::bin_stream &stream, umbf::Block *block)
        {
            const auto &images = static_cast<ImageList *>(block)->images;
            if (images.size() > UINT32_MAX) throw acul::runtime_error("Too many images");
            stream.write(static_cast<u32>(images.size()));
            for (const auto &entry : images)
                stream.write(entry.header.vendor_sign)
                    .write(entry.header.vendor_version)
                    .write(entry.header.spec_version)
                    .write(entry.header.type_sign)
                    .write(entry.blocks.first_block)
                    .write(entry.blocks.block_count);
        }

        umbf::Block *read_image_list(acul::bin_stream &stream)
        {
            auto *list = acul::alloc<ImageList>();
            u32 count = 0u;
            stream.read(count);
            list->images.resize(count);
            for (auto &entry : list->images)
                stream.read(entry.header.vendor_sign)
                    .read(entry.header.vendor_version)
                    .read(entry.header.spec_version)
                    .read(entry.header.type_sign)
                    .read(entry.blocks.first_block)
                    .read(entry.blocks.block_count);
            return list;
        }

        u8 paeth_predictor(u8 left, u8 up, u8 upper_left)
        {
            const int prediction = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(upper_left);
            const int left_distance = amal::abs(prediction - static_cast<int>(left));
            const int up_distance = amal::abs(prediction - static_cast<int>(up));
            const int upper_left_distance = amal::abs(prediction - static_cast<int>(upper_left));
            if (left_distance <= up_distance && left_distance <= upper_left_distance) return left;
            return up_distance <= upper_left_distance ? up : upper_left;
        }

        enum class ScanlineFilter : u8
        {
            none,
            sub,
            up,
            paeth,
            average,
            loco_i
        };

        struct ImagePayloadLayout
        {
            u64 pixel_offset = 0u;
            u64 scanline_size = 0u;
            u64 pixel_size = 0u;
            u64 bytes_per_pixel = 0u;
            u32 height = 0u;
        };

        bool read_image_payload_layout(const char *data, u64 size, bool filtered, ImagePayloadLayout &layout)
        {
            constexpr u64 fixed_prefix_size = sizeof(u32) * 2u + sizeof(u8);
            if (!data || size < fixed_prefix_size) return false;
            try
            {
                u32 width = 0u;
                u8 channel_count = 0u;
                acul::bin_stream stream(data, static_cast<u32>(fixed_prefix_size));
                stream.read(width).read(layout.height).read(channel_count);
                if (!width || !layout.height || !channel_count) return false;

                u64 offset = fixed_prefix_size;
                for (u8 channel = 0u; channel < channel_count; ++channel)
                {
                    if (offset >= size) return false;
                    const auto *terminator = static_cast<const char *>(
                        std::memchr(data + static_cast<size_t>(offset), '\0', static_cast<size_t>(size - offset)));
                    if (!terminator) return false;
                    offset = static_cast<u64>(terminator - data) + 1u;
                }

                if (offset > size || sizeof(u8) * 2u > size - offset) return false;
                const u8 bytes_per_channel = static_cast<u8>(data[offset]);
                if (!bytes_per_channel) return false;

                layout.bytes_per_pixel = static_cast<u64>(channel_count) * bytes_per_channel;
                if (width > UINT64_MAX / layout.bytes_per_pixel) return false;
                layout.scanline_size = static_cast<u64>(width) * layout.bytes_per_pixel;
                if (layout.height > UINT64_MAX / layout.scanline_size) return false;
                layout.pixel_size = static_cast<u64>(layout.height) * layout.scanline_size;
                layout.pixel_offset = offset + sizeof(u8) * 2u;
            }
            catch (...)
            {
                return false;
            }
            const u64 filter_size = filtered ? layout.height : 0u;
            if (layout.pixel_size > UINT64_MAX - filter_size || layout.pixel_offset > size ||
                layout.pixel_size + filter_size != size - layout.pixel_offset)
                return false;
            return true;
        }

        u8 filter_prediction(ScanlineFilter filter, u8 left, u8 up, u8 upper_left)
        {
            switch (filter)
            {
                case ScanlineFilter::none:
                    return 0u;
                case ScanlineFilter::sub:
                    return left;
                case ScanlineFilter::up:
                    return up;
                case ScanlineFilter::paeth:
                    return paeth_predictor(left, up, upper_left);
                case ScanlineFilter::average:
                    return static_cast<u8>((static_cast<u16>(left) + up) / 2u);
                case ScanlineFilter::loco_i:
                    if (upper_left >= amal::max(left, up)) return amal::min(left, up);
                    if (upper_left <= amal::min(left, up)) return amal::max(left, up);
                    return static_cast<u8>(static_cast<int>(left) + static_cast<int>(up) - upper_left);
            }
            return 0u;
        }

        struct ScanlineAnalysis
        {
            u64 horizontal_error = 0u;
            u64 vertical_error = 0u;
        };

        ScanlineAnalysis analyze_scanline(const u8 *current, const u8 *previous, u64 scanline_size, u64 bytes_per_pixel)
        {
            ScanlineAnalysis result;
            for (u64 x = 0u; x < scanline_size; ++x)
            {
                const int value = current[x];
                const int left = x >= bytes_per_pixel ? current[x - bytes_per_pixel] : 0;
                const int up = previous ? previous[x] : 0;
                result.horizontal_error += static_cast<u64>(amal::abs(value - left));
                result.vertical_error += static_cast<u64>(amal::abs(value - up));
            }
            return result;
        }

        void select_scanline_candidates(const ScanlineAnalysis &analysis, u64 scanline_size,
                                        ScanlineFilter (&candidates)[3])
        {
            const bool horizontal_dominates = analysis.horizontal_error <= UINT64_MAX / 2u &&
                                              analysis.horizontal_error * 2u < analysis.vertical_error;
            const bool vertical_dominates =
                analysis.vertical_error <= UINT64_MAX / 2u && analysis.vertical_error * 2u < analysis.horizontal_error;
            const u64 smooth_threshold = scanline_size <= UINT64_MAX / 8u ? scanline_size * 8u : UINT64_MAX;
            if (horizontal_dominates)
            {
                candidates[0] = ScanlineFilter::sub;
                candidates[1] = ScanlineFilter::paeth;
                candidates[2] = ScanlineFilter::loco_i;
            }
            else if (vertical_dominates)
            {
                candidates[0] = ScanlineFilter::up;
                candidates[1] = ScanlineFilter::paeth;
                candidates[2] = ScanlineFilter::loco_i;
            }
            else if (analysis.horizontal_error < smooth_threshold && analysis.vertical_error < smooth_threshold)
            {
                candidates[0] = ScanlineFilter::paeth;
                candidates[1] = ScanlineFilter::average;
                candidates[2] = ScanlineFilter::loco_i;
            }
            else
            {
                candidates[0] = ScanlineFilter::none;
                candidates[1] = ScanlineFilter::paeth;
                candidates[2] = ScanlineFilter::loco_i;
            }
        }

        u64 filter_scanline(ScanlineFilter filter, const u8 *current, const u8 *previous, u64 scanline_size,
                            u64 bytes_per_pixel, acul::vector<u8> &dst)
        {
            u64 score = 0u;
            for (u64 x = 0u; x < scanline_size; ++x)
            {
                const u8 left = x >= bytes_per_pixel ? current[x - bytes_per_pixel] : 0u;
                const u8 up = previous ? previous[x] : 0u;
                const u8 upper_left = previous && x >= bytes_per_pixel ? previous[x - bytes_per_pixel] : 0u;
                const u8 residual = static_cast<u8>(current[x] - filter_prediction(filter, left, up, upper_left));
                dst[static_cast<size_t>(x)] = residual;
                const int signed_residual = residual < 128u ? residual : static_cast<int>(residual) - 256;
                score += static_cast<u64>(amal::abs(signed_residual));
            }
            return score;
        }

        bool filter_image_payload(const char *data, u64 size, acul::vector<char> &dst)
        {
            ImagePayloadLayout layout;
            if (!read_image_payload_layout(data, size, false, layout) || size > UINT64_MAX - layout.height)
                return false;

            dst.assign(data, data + static_cast<size_t>(layout.pixel_offset));
            dst.reserve(static_cast<size_t>(size + layout.height));
            acul::vector<u8> candidate(static_cast<size_t>(layout.scanline_size));
            acul::vector<u8> best(static_cast<size_t>(layout.scanline_size));
            const auto *pixels = reinterpret_cast<const u8 *>(data + static_cast<size_t>(layout.pixel_offset));
            for (u64 y = 0u; y < layout.height; ++y)
            {
                const auto *current = pixels + y * layout.scanline_size;
                const auto *previous = y == 0u ? nullptr : current - layout.scanline_size;
                ScanlineFilter candidates[3];
                select_scanline_candidates(
                    analyze_scanline(current, previous, layout.scanline_size, layout.bytes_per_pixel),
                    layout.scanline_size, candidates);
                u64 best_score = UINT64_MAX;
                ScanlineFilter best_filter = ScanlineFilter::none;
                for (const auto filter : candidates)
                {
                    const u64 score = filter_scanline(filter, current, previous, layout.scanline_size,
                                                      layout.bytes_per_pixel, candidate);
                    if (score < best_score)
                    {
                        best_score = score;
                        best_filter = filter;
                        best = candidate;
                    }
                }
                dst.push_back(static_cast<char>(best_filter));
                dst.insert(dst.end(), reinterpret_cast<const char *>(best.data()),
                           reinterpret_cast<const char *>(best.data() + best.size()));
            }
            return true;
        }

        bool unfilter_image_payload(const char *data, u64 size, acul::vector<char> &dst)
        {
            ImagePayloadLayout layout;
            if (!read_image_payload_layout(data, size, true, layout) ||
                layout.pixel_size > UINT64_MAX - layout.pixel_offset)
                return false;

            dst.assign(data, data + static_cast<size_t>(layout.pixel_offset));
            dst.resize(static_cast<size_t>(layout.pixel_offset + layout.pixel_size));
            auto *pixels = reinterpret_cast<u8 *>(dst.data() + static_cast<size_t>(layout.pixel_offset));
            u64 source_offset = layout.pixel_offset;
            for (u64 y = 0u; y < layout.height; ++y)
            {
                if (source_offset >= size) return false;
                const auto filter = static_cast<ScanlineFilter>(static_cast<u8>(data[source_offset++]));
                if (filter > ScanlineFilter::loco_i || layout.scanline_size > size - source_offset) return false;
                const auto *filtered_scanline = reinterpret_cast<const u8 *>(data + static_cast<size_t>(source_offset));
                auto *current = pixels + y * layout.scanline_size;
                const auto *previous = y == 0u ? nullptr : current - layout.scanline_size;
                for (u64 x = 0u; x < layout.scanline_size; ++x)
                {
                    const u8 left = x >= layout.bytes_per_pixel ? current[x - layout.bytes_per_pixel] : 0u;
                    const u8 up = previous ? previous[x] : 0u;
                    const u8 upper_left =
                        previous && x >= layout.bytes_per_pixel ? previous[x - layout.bytes_per_pixel] : 0u;
                    current[x] =
                        static_cast<u8>(filtered_scanline[x] + filter_prediction(filter, left, up, upper_left));
                }
                source_offset += layout.scanline_size;
            }
            return source_offset == size;
        }

        bool transform_image_blocks(const acul::vector<char> &src, u32 block_count, bool inverse,
                                    acul::vector<char> &dst)
        {
            constexpr u64 block_header_size = (sizeof(u32) * 2u + sizeof(u64));
            acul::bin_stream output;
            bool transformed = false;
            u64 input_offset = 0u;
            output.reserve(src.size());
            try
            {
                for (u32 index = 0u; index < block_count; ++index)
                {
                    if (input_offset > src.size() || block_header_size > src.size() - input_offset) return false;
                    acul::bin_stream header(src.data() + static_cast<size_t>(input_offset),
                                            static_cast<u32>(block_header_size));
                    u32 signature = 0u;
                    u64 size = 0u;
                    u32 tag = 0u;
                    header.read(signature).read(tag).read(size);
                    input_offset += block_header_size;
                    if (!signature || size > src.size() - input_offset) return false;
                    const char *payload_data = src.data() + static_cast<size_t>(input_offset);
                    acul::vector<char> payload;
                    if (signature == sign_block::image)
                    {
                        const bool success = inverse ? unfilter_image_payload(payload_data, size, payload)
                                                     : filter_image_payload(payload_data, size, payload);
                        if (!success) return false;
                        transformed = true;
                    }
                    else payload.assign(payload_data, payload_data + static_cast<size_t>(size));
                    output.write(signature)
                        .write(tag)
                        .write(static_cast<u64>(payload.size()))
                        .write(payload.data(), payload.size());
                    input_offset += size;
                }
            }
            catch (...)
            {
                return false;
            }
            if (!transformed || input_offset != src.size()) return false;
            dst.assign(output.begin(), output.end());
            return true;
        }

        acul::op_result encode_compressed_image_segment(const SegmentBuffer &segment, u32 block_count, int compression,
                                                        acul::vector<char> &dst)
        {
            constexpr u8 transform_none = 0u;
            constexpr u8 transform_scanline_filters = 1u;

            acul::vector<char> compressed;
            ACUL_TRY(acul::fs::compress(segment.data(), segment.size(), compressed, compression));

            acul::vector<char> predicted;
            const bool can_predict = transform_image_blocks(segment, block_count, false, predicted);
            acul::vector<char> predicted_compressed;
            if (can_predict)
                ACUL_TRY(acul::fs::compress(predicted.data(), predicted.size(), predicted_compressed, compression));

            const bool use_predictor = can_predict && predicted_compressed.size() < compressed.size();
            const auto &selected = use_predictor ? predicted_compressed : compressed;
            dst.clear();
            dst.reserve(selected.size() + sizeof(u8));
            dst.push_back(static_cast<char>(use_predictor ? transform_scanline_filters : transform_none));
            dst.insert(dst.end(), selected.begin(), selected.end());
            return acul::make_op_success();
        }

        acul::op_result decode_compressed_image_segment(const char *data, u64 size, u32 block_count,
                                                        acul::vector<char> &dst)
        {
            constexpr u8 transform_none = 0u;
            constexpr u8 transform_scanline_filters = 1u;
            if (!data || size <= sizeof(u8)) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
            const auto transform = static_cast<u8>(data[0]);
            if (transform != transform_none && transform != transform_scanline_filters)
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            ACUL_TRY(acul::fs::decompress(data + sizeof(u8), static_cast<size_t>(size - sizeof(u8)), dst));
            if (transform == transform_scanline_filters)
            {
                acul::vector<char> restored;
                if (!transform_image_blocks(dst, block_count, true, restored))
                {
                    dst.clear();
                    return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
                }
                dst = std::move(restored);
            }
            return acul::make_op_success();
        }

    } // namespace

    const BlockStream image{read_image, write_image};
    const BlockStream image_atlas{read_image_atlas, write_image_atlas};
    const BlockStream image_list{read_image_list, write_image_list};
    const SegmentCodec segment_image_codec{encode_compressed_image_segment, decode_compressed_image_segment};
} // namespace umbf::registry
