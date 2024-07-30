#pragma once

#include <core/std/enum.hpp>
#include <finders_interface.h>
#include <vulkan/vulkan.hpp>
#include "asset.hpp"

namespace assets
{
    struct ImageInfo
    {
    public:
        u16 width;
        u16 height;
        int channelCount;
        DArray<std::string> channelNames;
        u16 bytesPerChannel;
        void *pixels{nullptr};
        vk::Format imageFormat;
        u32 mipLevels;

        vk::DeviceSize imageSize() const { return width * height * bytesPerChannel * channelCount; }
    };

    enum class ImageTypeFlagBits
    {
        tUndefined = 0x0,
        t2D = 0x1,
        tAtlas = 0x2,
        t3D = 0x4,
        tGenerated = 0x8
    };

    using ImageTypeFlags = Flags<ImageTypeFlagBits>;

    /// Base class for texture asset purposes
    class ImageStream
    {
    public:
        virtual ~ImageStream() = default;

        /**
         * @brief Writes the asset to a binary stream.
         * @param stream The binary stream to which the asset will be written.
         * @return Returns true if the write operation was successful, false otherwise.
         */
        virtual bool writeToStream(BinStream &stream) = 0;
    };

    /// \brief Represents a texture in any texture type
    class APPLIB_API Image : public Asset
    {
    public:
        Image(const InfoHeader &assetInfo, ImageTypeFlags flags, std::shared_ptr<ImageStream> streamInfo,
              u32 checksum = 0)
            : Asset(assetInfo, checksum), _flags(flags), _streamInfo(streamInfo)
        {
        }

        /// \brief Get the texture flags
        ImageTypeFlags flags() const { return _flags; }

        /// \brief Get the texture info class
        std::shared_ptr<ImageStream> &stream() { return _streamInfo; }

        virtual ~Image() = default;

        /**
         * @brief Saves the asset to a file.
         * @param path The path to the destination file.
         * @return Returns true if the save operation was successful, false otherwise.
         */
        bool save(const std::filesystem::path &path) override;

        /**
         * @brief Writes the asset to a binary stream.
         * @param stream The binary stream to which the asset will be written.
         * @return Returns true if the write operation was successful, false otherwise.
         **/
        bool writeToStream(BinStream &stream) override { return _streamInfo->writeToStream(stream); }

        /**
         * @brief Reads a Texture instance from a binary stream.
         * @param assetInfo Information header for the asset.
         * @param stream Binary stream to read from.
         * @return Shared pointer to the loaded Texture.
         */
        static APPLIB_API std::shared_ptr<Image> readFromStream(InfoHeader &assetInfo, BinStream &stream);

        /**
         * @brief Reads and creates an asset from a binary stream.
         * @param path The path to the asset.
         * @return A shared pointer to the created asset.
         **/
        static APPLIB_API std::shared_ptr<Image> readFromFile(const std::filesystem::path &path);

    protected:
        ImageTypeFlags _flags;
        std::shared_ptr<ImageStream> _streamInfo;
    };

    /// \brief Represents a 2D texture class. Usable as texture info in Texture asset class
    class APPLIB_API Image2D : public ImageStream, public ImageInfo
    {
    public:
        /**
         * @brief Constructor for the Texture2D class
         * @param image The image info
         */
        explicit Image2D(const ImageInfo &image) : ImageInfo(image) {}

        /**
         * Reads the texture info for 2D textures by binary stream
         * @param stream The binary stream from which the texture info will be read.
         * @return A shared pointer to the texture info
         */
        static std::shared_ptr<Image2D> readFromStream(BinStream &stream);

        virtual bool writeToStream(BinStream &stream) override;
    };

    class APPLIB_API Atlas : public ImageStream, public ImageInfo
    {
    public:
        using Spaces = rectpack2D::empty_spaces<false, rectpack2D::default_empty_spaces>;
        using Rect = rectpack2D::output_rect_t<Spaces>;

        /**
         * @brief Constructor for the Atlas class
         * @param image The image info
         * @param discardStep The atlas pack precision
         * @param images The list of images
         * @param packData The pack data (info about rectangles)
         */
        explicit Atlas(const ImageInfo &image, int discardStep, const DArray<std::shared_ptr<Image2D>> &images,
                       const std::vector<Rect> &packData)
            : ImageInfo(image), _discardStep(discardStep), _images(images), _packData(packData)
        {
        }

        /**
         * Reads the texture info for 2D textures by binary stream
         * @param stream The binary stream from which the texture info will be read.
         * @param device The Backend device used for asset creation.
         * @return A shared pointer to the texture info
         */
        static std::shared_ptr<Atlas> readFromStream(BinStream &stream);

        virtual bool writeToStream(BinStream &stream) override;

        DArray<std::shared_ptr<Image2D>> &images() { return _images; }

        std::vector<Rect> packData() const { return _packData; }

        int discardStep() const { return _discardStep; }

    private:
        u16 _discardStep;
        DArray<std::shared_ptr<Image2D>> _images;
        std::vector<Rect> _packData;
    };

    /// @brief Pack atlas
    /// @return Returns true on success otherwise returns false
    APPLIB_API bool packAtlas(size_t maxSize, int discardStep, rectpack2D::flipping_option flip,
                              std::vector<Atlas::Rect> &dst);
} // namespace assets

template <>
struct FlagTraits<assets::ImageTypeFlagBits>
{
    static constexpr bool isBitmask = true;
    static constexpr assets::ImageTypeFlags allFlags =
        assets::ImageTypeFlagBits::tUndefined | assets::ImageTypeFlagBits::t2D | assets::ImageTypeFlagBits::t3D |
        assets::ImageTypeFlagBits::tAtlas | assets::ImageTypeFlagBits::tGenerated;
};