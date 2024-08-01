#include <assets/asset.hpp>
#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <assets/scene.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

namespace assets
{
    std::string toString(Type type)
    {
        switch (type)
        {
            case Type::Material:
                return "material";
            case Type::Image:
                return "image";
            case Type::Scene:
                return "scene";
            case Type::Target:
                return "target";
            case Type::Library:
                return "library";
            default:
                return "invalid";
        }
    }

    bool Asset::saveFile(const std::filesystem::path &path, BinStream &src)
    {
        BinStream dstStream;

        dstStream.write(SIGNATURE[0]).write(SIGNATURE[1]).write(SIGNATURE[2]).write(SIGNATURE[3]);
        ::writeToStream(dstStream, _info);

        if (_info.compressed)
        {
            DArray<char> compressed;
            if (!io::file::compress(src.data() + src.pos(), src.size() - src.pos(), compressed, 5))
            {
                logError("Failed to compress: %s", path.string().c_str());
                return false;
            }

            dstStream.write(compressed.data(), compressed.size());
        }
        else
            dstStream.write(src.data() + src.pos(), src.size() - src.pos());
        _checksum = crc32(0, src.data(), src.size());
        return io::file::writeBinary(path.string(), dstStream.data(), dstStream.size());
    }

    bool Asset::loadFile(const std::filesystem::path &path, BinStream &dst, InfoHeader &info)
    {
        DArray<char> source;
        if (io::file::readBinary(path.string(), source) != io::file::ReadState::Success) return false;

        BinStream sourceStream(std::move(source));

        for (int i = 0; i < 4; i++)
        {
            u8 byte;
            sourceStream.read(byte);
            if (byte != SIGNATURE[i])
            {
                logError("Invalid file signature");
                return false;
            }
        }

        ::readFromStream(sourceStream, info);
        if (info.compressed)
        {
            DArray<char> decompressed;
            if (!io::file::decompress(sourceStream.data() + sourceStream.pos(),
                                      sourceStream.size() - sourceStream.pos(), decompressed))
            {
                logError("Failed to decompress: %s", path.string().c_str());
                return false;
            }
            dst = BinStream(std::move(decompressed));
        }
        else
            dst = std::move(sourceStream);
        return true;
    }

    std::shared_ptr<Asset> Asset::readFromStream(InfoHeader &assetInfo, BinStream &stream)
    {
        switch (assetInfo.type)
        {
            case Type::Image:
                return Image::readFromStream(assetInfo, stream);
            case Type::Material:
                return Material::readFromStream(assetInfo, stream);
            case Type::Scene:
                return Scene::readFromStream(assetInfo, stream);
            case Type::Target:
                return Target::readFromStream(assetInfo, stream);
            case Type::Library:
                return Library::readFromStream(assetInfo, stream);
            default:
                return nullptr;
        }
    }

    std::shared_ptr<Asset> Asset::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader assetInfo;
            if (!loadFile(path, stream, assetInfo)) { return nullptr; }
            return readFromStream(assetInfo, stream);
        }
        catch (std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }
} // namespace assets

template <>
BinStream &writeToStream(BinStream &stream, const assets::InfoHeader &src)
{
    u8 data = (static_cast<u8>(src.type) & 0x3F) | (static_cast<u8>(src.compressed) << 6);
    return stream.write(data);
}

template <>
BinStream &readFromStream(BinStream &stream, assets::InfoHeader &dst)
{
    u8 data;
    stream.read(data);
    dst.type = static_cast<assets::Type>(data & 0x3F);
    dst.compressed = (data >> 6) & 0x1;
    return stream;
}