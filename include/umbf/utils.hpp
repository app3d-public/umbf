#pragma once

#include <acul/enum.hpp>
#include <amal/rect.hpp>
#include "umbf.hpp"

namespace umbf::utils
{
    // Create transparent pixel depending on image format
    UMBF_EXPORT acul::unique_ptr<void> make_clear_pixel(const umbf::ImageFormat &format, size_t channel_count);

    /**
     * @brief Fills the pixel data of an image with a specified color based on the image format.
     *
     * This function determines the appropriate data type for the pixel values based on
     * the image format and calls the corresponding template function to fill the pixel data.
     *
     * @param color The color to fill the image with, represented as a acul::vec4.
     * @param image The ImageInfo structure containing the image details, including the format.
     */
    UMBF_EXPORT void fill_color_pixels(void *color, Image2D &image);

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
    UMBF_EXPORT void copy_pixels_to_area(const Image2D &src, Image2D &dst, const amal::irect &rect);

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
    UMBF_EXPORT void *convert_buffer(const void *source, size_t source_size, const ImageFormat &src_format,
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

    UMBF_EXPORT void filter_mat_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignes,
                                            size_t face_count, u64 default_id,
                                            acul::vector<acul::shared_ptr<MaterialRange>> &dst);

    namespace mesh
    {
        using namespace umbf::mesh;
        UMBF_EXPORT void fill_vertex_groups(const Model &model, acul::vector<VertexGroup> &groups);
    } // namespace mesh

    struct SkylineHeuristic
    {
        enum enum_type : u8
        {
            bottom_left,
            min_waste_fit
        };
    };

    namespace detail
    {
        struct SkylineNode
        {
            i32 x = 0;
            i32 y = 0;
            i32 width = 0;
        };
    } // namespace detail

    class SkylinePacker
    {
    public:
        SkylinePacker() = default;
        UMBF_EXPORT explicit SkylinePacker(const amal::ivec2 &atlas_size, i32 padding = 0);

        UMBF_EXPORT void reset(const amal::ivec2 &atlas_size, i32 padding = 0);
        UMBF_EXPORT bool add_locked(const amal::irect &rect);
        UMBF_EXPORT bool pack_rect(amal::irect &rect,
                                   SkylineHeuristic::enum_type heuristic = SkylineHeuristic::bottom_left);
        UMBF_EXPORT bool pack_rects(acul::vector<amal::irect> &rects, u32 locked_count,
                                    SkylineHeuristic::enum_type heuristic = SkylineHeuristic::bottom_left);

    private:
        amal::ivec2 _atlas_size{0, 0};
        i32 _padding = 0;
        acul::vector<detail::SkylineNode> _skyline_nodes;
        acul::vector<amal::irect> _waste_rects;
        acul::vector<amal::irect> _used_rects;
    };

    UMBF_EXPORT bool pack_skyline(const amal::ivec2 &atlas_size, u32 locked_count, acul::vector<amal::irect> &rects,
                                  SkylineHeuristic::enum_type heuristic = SkylineHeuristic::bottom_left,
                                  i32 padding = 0);

    struct MaxRectsHeuristic
    {
        enum enum_type : u8
        {
            best_short_side_fit,
            best_long_side_fit,
            best_area_fit,
            bottom_left_rule,
            contact_point_rule
        };
    };

    struct MaxRectsTransformBits
    {
        enum enum_type : u8
        {
            none = 0x0,
            rotate = 0x1,
            scale = 0x2
        };
        using flag_bitmask = std::true_type;
    };
    using MaxRectsTransform = acul::flags<MaxRectsTransformBits>;

    struct MaxRectsPackResult
    {
        bool packed = false;
        f32 scale = 1.0f;
        u32 packed_count = 0;
        u64 unpacked_area = 0;
    };

    namespace detail
    {
        struct MaxRectsCandidate
        {
            bool valid = false;
            bool flipped = false;
            amal::irect rect{};
            i32 score_primary = 0;
            i32 score_secondary = 0;
        };
    } // namespace detail

    class MaxRectsPacker
    {
    public:
        MaxRectsPacker() = default;
        UMBF_EXPORT explicit MaxRectsPacker(const amal::ivec2 &atlas_size, i32 padding = 0);

        UMBF_EXPORT void reset(const amal::ivec2 &atlas_size, i32 padding = 0);
        UMBF_EXPORT bool add_locked(const amal::irect &rect);
        UMBF_EXPORT bool pack_rect(amal::irect &rect, MaxRectsTransform *transform = nullptr,
                                   MaxRectsHeuristic::enum_type heuristic = MaxRectsHeuristic::best_short_side_fit,
                                   MaxRectsTransform allowed_transforms = MaxRectsTransformBits::none);
        UMBF_EXPORT MaxRectsPackResult pack_rects(
            acul::vector<amal::irect> &rects, u32 locked_count, acul::vector<MaxRectsTransform> *transforms = nullptr,
            MaxRectsHeuristic::enum_type heuristic = MaxRectsHeuristic::best_short_side_fit,
            MaxRectsTransform allowed_transforms = MaxRectsTransformBits::none);

    private:
        amal::ivec2 _atlas_size{0, 0};
        i32 _padding = 0;
        acul::vector<amal::irect> _free_rects;
        acul::vector<amal::irect> _used_rects;
    };

    UMBF_EXPORT MaxRectsPackResult
    pack_max_rects(const amal::ivec2 &atlas_size, u32 locked_count, acul::vector<amal::irect> &rects,
                   acul::vector<MaxRectsTransform> *transforms = nullptr,
                   MaxRectsHeuristic::enum_type heuristic = MaxRectsHeuristic::best_short_side_fit,
                   MaxRectsTransform allowed_transforms = MaxRectsTransformBits::none, i32 padding = 0);

    inline MaxRectsPackResult
    pack_max_rects(const amal::ivec2 &atlas_size, u32 locked_count, acul::vector<amal::irect> &rects,
                   MaxRectsHeuristic::enum_type heuristic = MaxRectsHeuristic::best_short_side_fit,
                   MaxRectsTransform allowed_transforms = MaxRectsTransformBits::none, i32 padding = 0)
    {
        return pack_max_rects(atlas_size, locked_count, rects, nullptr, heuristic, allowed_transforms, padding);
    }

    inline MaxRectsPackResult
    pack_max_rects(const amal::ivec2 &atlas_size, u32 locked_count, acul::vector<amal::irect> &rects,
                   acul::vector<MaxRectsTransform> &transforms,
                   MaxRectsHeuristic::enum_type heuristic = MaxRectsHeuristic::best_short_side_fit,
                   MaxRectsTransform allowed_transforms = MaxRectsTransformBits::none, i32 padding = 0)
    {
        return pack_max_rects(atlas_size, locked_count, rects, &transforms, heuristic, allowed_transforms, padding);
    }
} // namespace umbf::utils