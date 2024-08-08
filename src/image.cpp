#include <assets/image.hpp>
#include <assets/utils.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>
#include <memory>

namespace assets
{
    std::shared_ptr<Image> Image::readFromStream(InfoHeader &assetInfo, BinStream &stream)
    {
        std::shared_ptr<ImageStream> streamInfo;
        ImageTypeFlags flags;
        stream.read(flags);
        if (assetInfo.type == Type::Image)
        {
            if (flags & ImageTypeFlagBits::tAtlas)
                streamInfo = Atlas::readFromStream(stream);
            else if (flags & ImageTypeFlagBits::t2D)
                streamInfo = Image2D::readFromStream(stream);
            else
            {
                logError("Read Error: Invalid texture flags: %d", flags.getMask());
                return nullptr;
            }
        }
        else
        {
            logError("Invalid image asset type: %s", toString(assetInfo.type).c_str());
            return nullptr;
        }
        auto image = std::make_shared<Image>(assetInfo, flags, streamInfo, crc32(0, stream.data(), stream.size()));
        if (!image) return nullptr;
        stream.read(image->meta);
        return image;
    }

    std::shared_ptr<Image> Image::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader assetInfo;
            if (!loadFile(path, stream, assetInfo)) return nullptr;
            auto image = readFromStream(assetInfo, stream);
            if (!image) return nullptr;
            return image;
        }
        catch (std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }

    ImageInfo readImageMetaFromStream(BinStream &stream)
    {
        ImageInfo image{};
        stream.read(image.width).read(image.height).read(reinterpret_cast<char *>(&image.channelCount), sizeof(u16));
        u8 channelListSize;
        stream.read(channelListSize);
        for (size_t i = 0; i < channelListSize; i++)
        {
            std::string chan;
            stream.read(chan);
            image.channelNames.push_back(chan);
        }
        stream.read(image.bytesPerChannel);
        u8 imageFormat;
        stream.read(reinterpret_cast<char *>(&imageFormat), sizeof(u8));
        image.imageFormat = static_cast<vk::Format>(imageFormat);
        return image;
    }

    void writeBodyToStream(BinStream &stream, const ImageInfo &image, ImageTypeFlags flags)
    {
        stream.write(flags.getMask())
            .write(image.width)
            .write(image.height)
            .write(static_cast<u16>(image.channelCount));
        u8 channelNamesSize = image.channelNames.size();
        stream.write(reinterpret_cast<char *>(&channelNamesSize), sizeof(u8));
        for (const auto &str : image.channelNames) stream.write(str);
        stream.write(image.bytesPerChannel).write(static_cast<u8>(image.imageFormat));
    }

    bool Image2D::writeToStream(BinStream &stream)
    {
        writeBodyToStream(stream, *this, ImageTypeFlagBits::t2D);
        if (!pixels)
        {
            logWarn("Pixels cannot be null");
            return false;
        }
        stream.write(reinterpret_cast<char *>(pixels), imageSize());
        return true;
    }

    std::shared_ptr<Image2D> Image2D::readFromStream(BinStream &stream)
    {
        ImageInfo image = readImageMetaFromStream(stream);
        char *pixels = (char *)scalable_malloc(image.imageSize());
        stream.read(pixels, image.imageSize());
        image.pixels = (void *)pixels;
        return std::make_shared<Image2D>(image);
    }

    bool Atlas::writeToStream(BinStream &stream)
    {
        writeBodyToStream(stream, *this, ImageTypeFlagBits::t2D | ImageTypeFlagBits::tAtlas);
        stream.write(_discardStep).write(static_cast<u16>(_packData.size()));
        utils::fillColorPixels(glm::vec4(0.0f), *this);
        for (size_t i = 0; i < _packData.size(); i++)
        {
            if (!_images[i]->pixels)
            {
                logWarn("Pixels cannot be null");
                return false;
            }
            stream.write(_packData[i].w).write(_packData[i].h).write(_packData[i].x).write(_packData[i].y);
            utils::copyPixelsToArea(*_images[i], *this, _packData[i]);
        }
        stream.write(reinterpret_cast<char *>(pixels), imageSize());
        return true;
    }

    std::shared_ptr<Atlas> Atlas::readFromStream(BinStream &stream)
    {

        ImageInfo image = readImageMetaFromStream(stream);
        u16 discardStep, packSize;
        stream.read(discardStep).read(packSize);
        std::vector<Rect> packData(packSize);
        for (u16 i = 0; i < packSize; i++)
        {
            ImageInfo textureInfo = image;
            Atlas::Rect rect;
            stream.read(rect.w).read(rect.h).read(rect.x).read(rect.y);
            textureInfo.width = rect.w;
            textureInfo.height = rect.h;
            packData[i] = rect;
        }
        char *pixels = (char *)scalable_malloc(image.imageSize());
        stream.read(pixels, image.imageSize());
        image.pixels = (void *)pixels;
        return std::make_shared<Atlas>(image, discardStep, DArray<std::shared_ptr<Image2D>>{}, packData);
    }

    bool packAtlas(size_t maxSize, int discardStep, rectpack2D::flipping_option flip, std::vector<Atlas::Rect> &dst)
    {
        rectpack2D::callback_result packResult{rectpack2D::callback_result::CONTINUE_PACKING};
        static std::function<rectpack2D::callback_result(Atlas::Rect &)> reportSuccessfull = [](Atlas::Rect &) {
            return rectpack2D::callback_result::CONTINUE_PACKING;
        };
        static std::function<rectpack2D::callback_result(Atlas::Rect &)> reportUnsuccessfull =
            [&packResult, maxSize](Atlas::Rect &) {
                packResult = rectpack2D::callback_result::ABORT_PACKING;
                logInfo("Failed to pack atlas. Max size: %zu", maxSize);
                return rectpack2D::callback_result::ABORT_PACKING;
            };

        rectpack2D::find_best_packing<Atlas::Spaces>(
            dst, rectpack2D::make_finder_input(maxSize, discardStep, reportSuccessfull, reportUnsuccessfull, flip));
        return packResult != rectpack2D::callback_result::ABORT_PACKING;
    }
} // namespace assets