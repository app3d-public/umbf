
#pragma once

#include <acul/memory/smart_ptr.hpp>
#include <acul/string/utils.hpp>
#include <acul/vector.hpp>
#include <amal/rect.hpp>
#include <umbf/symbol_export.h>
#include <umbf/umbf.hpp>

namespace umbf
{
    namespace sign_block
    {
        namespace format
        {
            enum : u32
            {
                image = 0x0A5B6E8F,
                images_list = 0xB1E7C51A,
            };
        }

        enum : u32
        {
            image = 0x7684573F,
            image_atlas = 0xA3903A92,
        };
    } // namespace sign_block

    struct ImageFormat
    {
        struct Type
        {
            enum enum_type : u8
            {
                none,
                uint,
                sfloat,
            };
        };
        u8 type;
        u8 bytes_per_channel;

        constexpr bool operator==(const ImageFormat &rhs) const
        {
            return type == rhs.type && bytes_per_channel == rhs.bytes_per_channel;
        }

#if !(defined(__cpp_impl_three_way_comparison) && __cpp_impl_three_way_comparison >= 201907)
        constexpr bool operator!=(const ImageFormat &rhs) const { return !(*this == rhs); }
#endif
    };

    // Represents a 2D image asset block.
    struct Image2D : Block
    {
    public:
        u32 width;                           //< Width of the image in pixels.
        u32 height;                          //< Height of the image in pixels.
        acul::vector<acul::string> channels; //< Names of the channels (e.g., "Red", "Green", "Blue").
        ImageFormat format;                  //< Format of the image data.
        void *pixels = nullptr;              //< Pointer to the raw pixel data.

        /**
         * @brief Calculates the size of the image in bytes.
         *
         * This function computes the total size of the image based on its dimensions,
         * channel count, and bytes per channel.
         *
         * @return The total size of the image in bytes.
         */
        size_t size() const { return width * height * format.bytes_per_channel * channels.size(); }

        u32 signature() const noexcept override { return sign_block::image; }

        /**
         * @brief Returns the signature of the block.
         *
         * Provides a unique signature for the `Image2D` block type to distinguish it
         * from other types of asset blocks.
         *
         * @return The signature of the block.
         */
    };

    /**
     * @brief Represents an atlas image block, derived from `Image2D`.
     *
     * The `Atlas` structure extends `Image2D` to support texture atlases. It includes
     * additional fields to manage atlas-specific data such as discarded steps, images
     * within the atlas, and packing data.
     */
    struct Atlas : Block
    {
        acul::vector<amal::irect> pack_data; //< Data about the placement of images within the atlas.
        i16 padding;                         //< Padding between images in the atlas.

        u32 signature() const noexcept override { return sign_block::image_atlas; }

        /**
         * @brief Returns the signature of the atlas block.
         *
         * Provides a unique signature for the `Atlas` block type to distinguish it
         * from other types of asset blocks, including `Image2D`.
         *
         * @return The signature of the block.
         */
    };

    namespace registry
    {
        extern UMBF_EXPORT const BlockStream image;
        extern UMBF_EXPORT const BlockStream image_atlas;
        extern UMBF_EXPORT const SegmentCodec segment_image_codec;
    } // namespace registry

    inline void insert_image_streams(registry::HashResolver &resolver)
    {
        resolver.block_streams[sign_block::image] = &registry::image;
        resolver.block_streams[sign_block::image_atlas] = &registry::image_atlas;
    }

    inline void insert_image_codecs(registry::HashResolver &resolver)
    {
        resolver.segment_codecs[UMBF_SEGMENT_COMPRESSED_IMAGE] = &registry::segment_image_codec;
    }
} // namespace umbf

namespace acul
{
    inline string to_string(umbf::ImageFormat::Type::enum_type type)
    {
        switch (type)
        {
            case umbf::ImageFormat::Type::uint:
                return "uint";
            case umbf::ImageFormat::Type::sfloat:
                return "sfloat";
            default:
                return "unknown";
        }
    }

    inline string to_string(umbf::ImageFormat format)
    {
        return acul::format("%dbit %s", format.bytes_per_channel * 8,
                            to_string((umbf::ImageFormat::Type::enum_type)format.type).c_str());
    }
} // namespace acul
