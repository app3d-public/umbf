#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

template <>
BinStream &writeToStream(BinStream &stream, const assets::MaterialNode &src)
{
    u16 data = src.textured ? ((1 << 15) | (src.textureID & 0x7FFF)) : 0;
    stream.write(src.rgb).write(data);
    return stream;
}

template <>
BinStream &readFromStream(BinStream &stream, assets::MaterialNode &dst)
{
    u16 data;
    stream.read(dst.rgb).read(data);
    dst.textured = data >> 15;
    dst.textureID = dst.textured ? (data & 0x7FFF) : 0;
    return stream;
}

namespace assets
{
    void writeTexturesToStream(BinStream &stream, const DArray<std::shared_ptr<Asset>> &textures)
    {
        stream.write(static_cast<u16>(textures.size()));
        for (auto &texture : textures)
        {
            BinStream textureStream{};
            if (texture->writeToStream(textureStream))
                ::writeToStream(stream, texture->info()).write(textureStream.data(), textureStream.size());
            else
            {
                InfoHeader invalid{Type::Invalid, false};
                ::writeToStream(stream, invalid);
            }
        }
    }

    bool Material::writeToStream(BinStream &stream)
    {
        writeTexturesToStream(stream, textures);
        ::writeToStream(stream, info.albedo);
        return true;
    }

    bool Material::save(const std::filesystem::path &path)
    {
        BinStream stream{};
        if (!writeToStream(stream)) return false;
        return saveFile(path, stream);
    }

    void readTexturesFromStream(BinStream &stream, DArray<std::shared_ptr<Asset>> &textures)
    {
        u16 texSize{0};
        stream.read(texSize);
        textures.resize(texSize);
        for (auto &asset : textures)
        {
            InfoHeader textureInfo;
            ::readFromStream(stream, textureInfo);
            switch (textureInfo.type)
            {
                case Type::Image:
                    if (auto texture = Image::readFromStream(textureInfo, stream)) asset = texture;
                    break;
                case Type::Target:
                    if (auto target = Target::readFromStream(textureInfo, stream)) asset = target;
                    break;
                default:
                    asset = std::make_shared<InvalidAsset>();
                    break;
            }
        }
    }

    std::shared_ptr<Material> Material::readFromStream(InfoHeader &assetInfo, BinStream &stream)
    {
        DArray<std::shared_ptr<Asset>> textures;
        readTexturesFromStream(stream, textures);
        MaterialInfo material;
        ::readFromStream(stream, material.albedo);
        u32 checksum = crc32(0, stream.data(), stream.size());
        return std::make_shared<Material>(assetInfo, textures, material, checksum);
    }

    std::shared_ptr<Material> Material::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader assetInfo;
            if (!loadFile(path, stream, assetInfo)) return nullptr;

            if (assetInfo.type == Type::Material)
                return readFromStream(assetInfo, stream);
            else
                return nullptr;
        }
        catch (std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }
} // namespace assets