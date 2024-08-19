#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <assets/scene.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

namespace assets
{
    bool Scene::writeToStream(BinStream &stream)
    {
        // Objects
        stream.write(static_cast<u16>(objects.size()));
        for (const auto &object : objects)
        {
            stream.write(object->name);
            // Transform
            stream.write(object->transform.position).write(object->transform.rotation).write(object->transform.scale);

            // Meta
            stream.write(object->meta);
        }

        // Textures
        writeTexturesToStream(stream, textures);

        // Materials
        stream.write(static_cast<u16>(materials.size()));
        for (auto &mat : materials)
        {
            BinStream materialStream{};
            if (mat->writeToStream(materialStream))
                stream.write(mat->info()).write(materialStream.data(), materialStream.size());
            else
            {
                InfoHeader invalid{Type::Invalid, false};
                stream.write(invalid);
            }
        }
        stream.write(meta);
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
        // Objects
        u16 objectSize;
        stream.read(objectSize);
        DArray<std::shared_ptr<Object>> objects;
        for (int i = 0; i < objectSize; ++i)
        {
            auto object = std::make_shared<Object>();

            stream.read(object->name);
            // Transform
            stream.read(object->transform.position).read(object->transform.rotation).read(object->transform.scale);

            // Meta
            ForwardList<std::shared_ptr<meta::Block>> objectMeta;
            stream.read(objectMeta);
        }
        DArray<std::shared_ptr<Asset>> textures;
        readTexturesFromStream(stream, textures);

        u16 materialSize;
        stream.read(materialSize);

        DArray<std::shared_ptr<Asset>> materials(materialSize);
        for (auto &mat : materials)
        {
            InfoHeader materialInfo;
            stream.read(materialInfo);
            switch (materialInfo.type)
            {
                case Type::Material:
                    if (auto material = Material::readFromStream(materialInfo, stream)) mat = material;
                    break;
                case Type::Target:
                    if (auto target = Target::readFromStream(materialInfo, stream)) mat = target;
                    break;
                default:
                    mat = std::make_shared<InvalidAsset>();
                    break;
            }
        }
        u32 checksum = crc32(0, stream.data(), stream.size());
        auto scene = std::make_shared<Scene>(assetInfo, objects, textures, materials, checksum);
        if (!scene) return nullptr;
        stream.read(scene->meta);
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

    namespace meta
    {
        void SceneInfoStream::writeToStream(BinStream &stream, meta::Block *block)
        {
            SceneInfo *scene = static_cast<SceneInfo *>(block);
            stream.write(scene->author).write(scene->info).write(scene->version);
        }

        meta::Block *SceneInfoStream::readFromStream(BinStream &stream)
        {
            SceneInfo *scene = new SceneInfo();
            stream.read(scene->author).read(scene->info).read(scene->version);
            return scene;
        }

        namespace mesh
        {
            namespace bary
            {

                inline u64 encode(const glm::vec3 &bary)
                {
                    u64 binary = 0;
                    binary |= (static_cast<uint64_t>(bary.x != 0) << 2);
                    binary |= (static_cast<uint64_t>(bary.y != 0) << 1);
                    binary |= (static_cast<uint64_t>(bary.z != 0));
                    return binary;
                }

                inline void decode(u8 binary, glm::vec3 &bary)
                {
                    bary.x = static_cast<f32>((binary >> 2) & 0x1);
                    bary.y = static_cast<f32>((binary >> 1) & 0x1);
                    bary.z = static_cast<f32>(binary & 0x1);
                }

                DArray<u64> pack(const DArray<Vertex> &barycentric)
                {
                    size_t packSize = (barycentric.size() * 3 + 63) / 64;
                    DArray<u64> pack64(packSize, 0);
                    int bid = 0, offset = 0, i = 0;
                    while (bid < packSize)
                    {
                        if (offset != 0) pack64[bid] |= encode(barycentric[i++].barycentric) << (64 - offset);
                        while (offset < 61 && i < barycentric.size())
                        {
                            pack64[bid] |= encode(barycentric[i++].barycentric) << (61 - offset);
                            offset += 3;
                        }
                        if (i < barycentric.size() && offset < 64)
                        {
                            offset = 3 - (64 - offset);
                            pack64[bid] |= encode(barycentric[i].barycentric) >> offset;
                            if (offset == 0) ++i;
                        }
                        ++bid;
                    }
                    return pack64;
                }
                DArray<glm::vec3> unpack(const DArray<u64> &src, size_t size)
                {
                    DArray<glm::vec3> barycentric(size);
                    int bid = 0, offset = 0, i = 0;
                    while (bid < size)
                    {
                        while (offset < 61 && i < size)
                        {
                            decode(src[bid] >> (61 - offset), barycentric[i++]);
                            offset += 3;
                        }
                        if (i < size && offset != 64)
                        {
                            if (offset == 61)
                            {
                                decode(src[bid++] & 0x7, barycentric[i++]);
                                offset = 0;
                            }
                            else
                            {
                                u64 tmp = src[bid] & ((1LL << (64 - offset)) - 1);
                                offset = 3 - (64 - offset);
                                decode((tmp << offset) | (src[++bid] >> (64 - offset)), barycentric[i++]);
                            }
                        }
                        else
                            ++bid;
                    }
                    return barycentric;
                }
            } // namespace bary

            void MeshStream::writeToStream(BinStream &stream, meta::Block *block)
            {

                MeshBlock *mesh = static_cast<MeshBlock *>(block);
                auto &model = mesh->model;

                // Sizes
                stream.write(static_cast<u32>(model.vertices.size()))
                    .write(static_cast<u32>(model.groups.size()))
                    .write(static_cast<u32>(model.faces.size()))
                    .write(static_cast<u32>(model.indices.size()));

                // Groups
                for (auto &group : model.groups)
                {
                    stream.write(model.vertices[group.vertices.front()].pos)
                        .write(static_cast<u32>(group.vertices.size()));
                    stream.write(group.vertices.data(), group.vertices.size());
                    stream.write(static_cast<u32>(group.faces.size()));
                    stream.write(group.faces.data(), group.faces.size());
                }

                // Vertices
                for (auto &vertex : model.vertices) stream.write(vertex.uv).write(vertex.normal);

                // Faces
                for (auto &face : model.faces)
                {
                    stream.write(static_cast<u32>(face.vertices.size()))
                        .write(face.vertices.data(), face.vertices.size())
                        .write(face.normal)
                        .write(face.indexCount);
                    for (int i = face.startID; i < face.startID + face.indexCount; i++) stream.write(model.indices[i]);
                }

                // Barycentrics
                auto barycentric = bary::pack(mesh->baryVertices);
                stream.write(barycentric.data(), barycentric.size());

                // AABB
                stream.write(model.aabb.min).write(model.aabb.max);
            }

            meta::Block *MeshStream::readFromStream(BinStream &stream)
            {
                MeshBlock *mesh = new MeshBlock();

                // Model
                auto &model = mesh->model;

                // Sizes
                u32 vCount, vgCount, fCount, iCount;
                stream.read(vCount).read(vgCount).read(fCount).read(iCount);
                model.vertices.resize(vCount);
                model.groups.resize(vgCount);
                model.faces.resize(fCount);
                model.indices.resize(iCount);
                mesh->baryVertices.resize(iCount);

                // Groups
                for (auto &group : model.groups)
                {
                    glm::vec3 pos;
                    stream.read(pos);
                    u32 grVCount;
                    stream.read(grVCount);
                    group.vertices.resize(grVCount);
                    stream.read(group.vertices.data(), grVCount);
                    u32 grFCount;
                    stream.read(grFCount);
                    group.faces.resize(grFCount);
                    stream.read(group.faces.data(), grFCount);
                }

                // Vertices
                for (auto &vertex : model.vertices) stream.read(vertex.uv).read(vertex.normal);

                // Faces
                size_t indexID = 0;
                for (auto &face : model.faces)
                {
                    u32 fvCount;
                    stream.read(fvCount);
                    face.vertices.resize(fvCount);
                    stream.read(face.vertices.data(), fvCount).read(face.normal).read(face.indexCount);
                    for (int i = 0; i < face.indexCount; i++) stream.read(model.indices[indexID + i]);
                    face.startID = indexID;
                    indexID += face.indexCount;
                }

                // Barycentrics
                DArray<u64> baryPack((iCount * 3 + 63) / 64);
                stream.read(reinterpret_cast<char *>(baryPack.data()), baryPack.size() * sizeof(u64));
                DArray<glm::vec3> barycentric = bary::unpack(baryPack, iCount);
                for (int i = 0; i < iCount; i++)
                    mesh->baryVertices[i] = {model.vertices[model.indices[i]].pos, barycentric[i]};

                // AABB
                stream.read(model.aabb.min).read(model.aabb.max);

                return mesh;
            }
        } // namespace mesh
    } // namespace meta
} // namespace assets