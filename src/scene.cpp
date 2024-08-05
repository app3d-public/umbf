#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <assets/scene.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

namespace assets
{
    HashMap<u32, Object::MetaStream *> g_MetaStreams;

    void addObjectMetaStream(u32 signature, Object::MetaStream *stream)
    {
        auto [it, inserted] = g_MetaStreams.try_emplace(signature, stream);
        if (!inserted)
        {
            logWarn("Stream 0x%08x already registered", signature);
            delete stream;
        }
    }

    void clearMetaStreams()
    {
        for (auto &[id, stream] : g_MetaStreams) delete stream;
        g_MetaStreams.clear();
    }

    Object::MetaStream *getMetaStream(u32 signature)
    {
        auto it = g_MetaStreams.find(signature);
        if (it == g_MetaStreams.end())
        {
            logError("Failed to recognize meta stream signature: 0x%08x", signature);
            return nullptr;
        }
        return it->second;
    }

    bool Scene::writeToStream(BinStream &stream)
    {
        // Objects
        stream.write(static_cast<u16>(objects.size()));
        for (const auto &object : objects)
        {
            // Transform
            stream.write(object->transform.position).write(object->transform.rotation).write(object->transform.scale);

            // Material
            stream.write(object->matID);

            // Meta
            Object::MetaStream *metaStream = getMetaStream(object->meta->signature());
            if (!metaStream)
            {
                u8 invalidFlag = META_FLAG_INVALID;
                stream.write(invalidFlag).write(object->meta->signature()).write(0ULL);
            }
            else
                metaStream->writeToStream(stream, object->meta);
        }

        // Textures
        writeTexturesToStream(stream, textures);

        // Materials
        stream.write(static_cast<u16>(materials.size()));
        for (auto &node : materials)
        {
            stream.write(node.name);
            BinStream materialStream{};
            if (node.asset->writeToStream(materialStream))
                stream.write(node.asset->info()).write(materialStream.data(), materialStream.size());
            else
            {
                InfoHeader invalid{Type::Invalid, false};
                stream.write(invalid);
            }
        }
        return true;
    }

    bool Scene::save(const std::filesystem::path &path, int compression)
    {
        BinStream stream{};
        if (!writeToStream(stream)) return false;
        return saveFile(path, stream, compression);
    }

    std::shared_ptr<Scene> Scene::readFromStream(InfoHeader &assetInfo, BinStream &stream)
    {
        // Project info
        MetaInfo meta;
        stream.read(meta.info).read(meta.author).read(meta.appVersion);

        // Objects
        u16 objectSize;
        stream.read(objectSize);
        DArray<std::shared_ptr<Object>> objects;
        for (int i = 0; i < objectSize; ++i)
        {
            auto object = std::make_shared<Object>();
            // Transform
            stream.read(object->transform.position).read(object->transform.rotation).read(object->transform.scale);

            // Material
            stream.read(object->matID);

            // Meta
            Object::MetaHeader header;
            stream.read(header.flags).read(header.signature).read(header.blockSize);
            if (header.flags == META_FLAG_INVALID)
                logError("Broken meta block");
            else
            {
                Object::MetaStream *metaStream = getMetaStream(header.signature);
                if (!metaStream)
                    stream.shift(header.blockSize);
                else
                {
                    object->meta = metaStream->readFromStream(stream);
                    objects.push_back(object);
                }
            }
        }
        DArray<std::shared_ptr<Asset>> textures;
        readTexturesFromStream(stream, textures);

        u16 materialSize;
        stream.read(materialSize);

        DArray<MaterialNode> materials(materialSize);
        for (auto &node : materials)
        {
            InfoHeader materialInfo;
            stream.read(node.name).read(materialInfo);
            switch (materialInfo.type)
            {
                case Type::Material:
                    if (auto material = Material::readFromStream(materialInfo, stream)) node.asset = material;
                    break;
                case Type::Target:
                    if (auto target = Target::readFromStream(materialInfo, stream)) node.asset = target;
                    break;
                default:
                    node.asset = std::make_shared<InvalidAsset>();
                    break;
            }
        }
        u32 checksum = crc32(0, stream.data(), stream.size());
        auto scene = std::make_shared<Scene>(assetInfo, meta, objects, textures, materials, checksum);
        if (!scene) return nullptr;
        Asset::readMeta(stream, scene->meta);
        return scene;
    }

    std::shared_ptr<Scene> Scene::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader assetInfo;
            if (!loadFile(path, stream, assetInfo)) return nullptr;

            if (assetInfo.type != Type::Scene)
            {
                logError("Invalid asset call. Asset type must be scene. TypeID: #%hhu",
                         static_cast<u8>(assetInfo.type));
                return nullptr;
            }
            auto asset = readFromStream(assetInfo, stream);
            return asset;
        }
        catch (const std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }
} // namespace assets