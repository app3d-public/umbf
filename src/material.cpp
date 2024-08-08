#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

template <>
BinStream &BinStream::write(const assets::MaterialNode &src)
{
    u16 data = src.textured ? ((1 << 15) | (src.textureID & 0x7FFF)) : 0;
    return write(src.rgb).write(data);
}

template <>
BinStream &BinStream::read(assets::MaterialNode &dst)
{
    u16 data;
    read(dst.rgb).read(data);
    dst.textured = data >> 15;
    dst.textureID = dst.textured ? (data & 0x7FFF) : 0;
    return *this;
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
                stream.write(texture->info()).write(textureStream.data(), textureStream.size());
            else
            {
                InfoHeader invalid{Type::Invalid, false};
                stream.write(invalid);
            }
        }
    }

    bool Material::writeToStream(BinStream &stream)
    {
        writeTexturesToStream(stream, textures);
        stream.write(info.albedo).write(meta);
        return true;
    }

    bool Material::save(const std::filesystem::path &path, int compression)
    {
        BinStream stream{};
        if (!writeToStream(stream)) return false;
        return saveFile(path, stream, compression);
    }

    void readTexturesFromStream(BinStream &stream, DArray<std::shared_ptr<Asset>> &textures)
    {
        u16 texSize{0};
        stream.read(texSize);
        textures.resize(texSize);
        for (auto &asset : textures)
        {
            InfoHeader textureInfo;
            stream.read(textureInfo);
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
        stream.read(material.albedo);
        u32 checksum = crc32(0, stream.data(), stream.size());
        auto asset = std::make_shared<Material>(assetInfo, textures, material, checksum);
        if (!asset) return nullptr;
        stream.read(asset->meta);
        return asset;
    }

    std::shared_ptr<Material> Material::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader assetInfo;
            if (!loadFile(path, stream, assetInfo) || assetInfo.type != Type::Material) return nullptr;
            return readFromStream(assetInfo, stream);
        }
        catch (std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }
} // namespace assets