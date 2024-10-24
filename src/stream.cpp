#include <assets/asset.hpp>
#include <assets/utils.hpp>

namespace assets
{
    namespace streams
    {
        void writeImageInfo(astl::bin_stream &stream, Image2D *image2D)
        {
            stream.write(image2D->width).write(image2D->height).write(static_cast<u16>(image2D->channelCount));
            u8 channelNamesSize = image2D->channelNames.size();
            stream.write(reinterpret_cast<char *>(&channelNamesSize), sizeof(u8));
            for (const auto &str : image2D->channelNames) stream.write(str);
            stream.write(image2D->bytesPerChannel).write(static_cast<u8>(image2D->imageFormat));
        }

        void writeImage2D(astl::bin_stream &stream, meta::Block *block)
        {
            auto image = static_cast<Image2D *>(block);
            writeImageInfo(stream, image);
            if (!image->pixels) throw std::runtime_error("Pixels cannot be null");
            stream.write(reinterpret_cast<char *>(image->pixels), image->imageSize());
        }

        void readImageInfo(astl::bin_stream &stream, Image2D *image2D)
        {
            stream.read(image2D->width)
                .read(image2D->height)
                .read(reinterpret_cast<char *>(&image2D->channelCount), sizeof(u16));
            u8 channelListSize;
            stream.read(channelListSize);
            for (size_t i = 0; i < channelListSize; i++)
            {
                std::string chan;
                stream.read(chan);
                image2D->channelNames.push_back(chan);
            }
            stream.read(image2D->bytesPerChannel);
            u8 imageFormat;
            stream.read(reinterpret_cast<char *>(&imageFormat), sizeof(u8));
            image2D->imageFormat = static_cast<vk::Format>(imageFormat);
        }

        meta::Block *readImage2D(astl::bin_stream &stream)
        {
            Image2D *image = astl::alloc<Image2D>();
            readImageInfo(stream, image);
            char *pixels = astl::alloc_n<char>(image->imageSize());
            stream.read(pixels, image->imageSize());
            image->pixels = (void *)pixels;
            return image;
        }

        void writeImageAtlas(astl::bin_stream &stream, meta::Block *block)
        {
            auto image = static_cast<Image2D *>(block);
            writeImageInfo(stream, image);
            auto atlas = static_cast<Atlas *>(block);
            stream.write(atlas->discardStep).write(static_cast<u16>(atlas->packData.size()));
            utils::fillColorPixels(glm::vec4(0.0f), *image);
            for (size_t i = 0; i < atlas->packData.size(); i++)
            {
                if (!atlas->images[i]->pixels) throw std::runtime_error("Pixels cannot be null");
                stream.write(atlas->packData[i].w)
                    .write(atlas->packData[i].h)
                    .write(atlas->packData[i].x)
                    .write(atlas->packData[i].y);
                utils::copyPixelsToArea(*atlas->images[i], *image, atlas->packData[i]);
            }
            stream.write(reinterpret_cast<char *>(atlas->pixels), atlas->imageSize());
        }

        meta::Block *readImageAtlas(astl::bin_stream &stream)
        {
            auto *atlas = astl::alloc<Atlas>();
            readImageInfo(stream, atlas);
            u16 packDataSize;
            stream.read(atlas->discardStep).read(packDataSize);
            atlas->packData.resize(packDataSize);
            for (size_t i = 0; i < packDataSize; i++)
            {
                stream.read(atlas->packData[i].w)
                    .read(atlas->packData[i].h)
                    .read(atlas->packData[i].x)
                    .read(atlas->packData[i].y);
            }
            char *pixels = astl::alloc_n<char>(atlas->imageSize());
            stream.read(pixels, atlas->imageSize());
            atlas->pixels = (void *)pixels;
            return atlas;
        }

        void writeMaterial(astl::bin_stream &stream, meta::Block *block)
        {
            auto material = static_cast<Material *>(block);
            stream.write(material->textures).write(material->albedo);
        }

        meta::Block *readMaterial(astl::bin_stream &stream)
        {
            Material *material = astl::alloc<Material>();
            stream.read(material->textures).read(material->albedo);
            return material;
        }

        void writeScene(astl::bin_stream &stream, meta::Block *block)
        {
            auto scene = static_cast<Scene *>(block);
            // Objects
            stream.write(static_cast<u16>(scene->objects.size()));
            for (const auto &object : scene->objects) stream.write(object.id).write(object.name).write(object.meta);

            // Textures
            stream.write(scene->textures).write(scene->materials);
        }

        meta::Block *readScene(astl::bin_stream &stream)
        {
            Scene *scene = astl::alloc<Scene>();
            u16 objectCount;
            stream.read(objectCount);
            scene->objects.resize(objectCount);
            for (auto &object : scene->objects) stream.read(object.id).read(object.name).read(object.meta);
            stream.read(scene->textures).read(scene->materials);
            return scene;
        }

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

        astl::vector<u64> pack(const astl::vector<mesh::bary::Vertex> &barycentric)
        {
            size_t packSize = (barycentric.size() * 3 + 63) / 64;
            astl::vector<u64> pack64(packSize, 0);
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
        astl::vector<glm::vec3> unpack(const astl::vector<u64> &src, size_t size)
        {
            astl::vector<glm::vec3> barycentric(size);
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

        void writeMesh(astl::bin_stream &stream, meta::Block *block)
        {

            mesh::MeshBlock *mesh = static_cast<mesh::MeshBlock *>(block);
            auto &model = mesh->model;

            // Sizes
            stream.write(static_cast<u32>(model.vertices.size()))
                .write(static_cast<u32>(model.groups.size()))
                .write(static_cast<u32>(model.faces.size()))
                .write(static_cast<u32>(model.indices.size()));

            // Groups
            for (auto &group : model.groups)
            {
                stream.write(model.vertices[group.vertices.front()].pos).write(static_cast<u32>(group.vertices.size()));
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
            auto barycentric = pack(mesh->baryVertices);
            stream.write(barycentric.data(), barycentric.size());

            // AABB
            stream.write(model.aabb.min).write(model.aabb.max);

            // Transform
            stream.write(mesh->transform.position).write(mesh->transform.rotation).write(mesh->transform.scale);
        }

        meta::Block *readMesh(astl::bin_stream &stream)
        {
            mesh::MeshBlock *mesh = astl::alloc<mesh::MeshBlock>();
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
            astl::vector<u64> baryPack((iCount * 3 + 63) / 64);
            stream.read(reinterpret_cast<char *>(baryPack.data()), baryPack.size() * sizeof(u64));
            astl::vector<glm::vec3> barycentric = unpack(baryPack, iCount);
            for (int i = 0; i < iCount; i++)
                mesh->baryVertices[i] = {model.vertices[model.indices[i]].pos, barycentric[i]};

            // AABB
            stream.read(model.aabb.min).read(model.aabb.max);

            return mesh;
        }

        meta::Block *readMaterialInfo(astl::bin_stream &stream)
        {
            MaterialInfo *block = astl::alloc<MaterialInfo>();
            u32 assignSize;
            stream.read(block->id).read(block->name).read(assignSize);
            block->assignments.resize(assignSize);
            stream.read(block->assignments.data(), assignSize);
            return block;
        }

        void writeMatRangeAssign(astl::bin_stream &stream, meta::Block *block)
        {
            MatRangeAssignAtrr *assignment = static_cast<MatRangeAssignAtrr *>(block);
            stream.write(assignment->matID)
                .write(static_cast<u32>(assignment->faces.size()))
                .write(assignment->faces.data(), assignment->faces.size());
        }

        meta::Block *readMatRangeAssign(astl::bin_stream &stream)
        {
            MatRangeAssignAtrr *block = astl::alloc<MatRangeAssignAtrr>();
            u32 faceSize;
            stream.read(block->matID).read(faceSize);
            block->faces.resize(faceSize);
            stream.read(block->faces.data(), faceSize);
            return block;
        }

        void writeTarget(astl::bin_stream &stream, meta::Block *block)
        {
            auto target = static_cast<Target *>(block);
            u8 headerData = (static_cast<u8>(target->header.type) & 0x3F) |                             // Type
                            (static_cast<u8>(target->header.compressed) << 6) |                         // Compressed
                            (static_cast<u8>(target->addr.proto == Target::Addr::Proto::Network) << 7); // Proto
            stream.write(headerData).write(target->addr.url).write(target->checksum);
        }

        meta::Block *readTarget(astl::bin_stream &stream)
        {
            Target *target = astl::alloc<Target>();
            u8 headerData;
            stream.read(headerData);
            target->header.type = static_cast<assets::Type>(headerData & 0x3F);
            target->header.compressed = (headerData >> 6) & 0x1;
            target->addr.proto =
                (headerData & 0x80) ? Target::Addr::Proto::Network : Target::Addr::Proto::File; // Proto
            stream.read(target->addr.url).read(target->checksum);
            return target;
        }

        void writeLibrary(astl::bin_stream &stream, meta::Block *block)
        {
            auto library = static_cast<Library *>(block);
            stream.write(library->fileTree);
        }

        meta::Block *readLibrary(astl::bin_stream &stream)
        {
            Library *library = astl::alloc<Library>();
            stream.read(library->fileTree);
            return library;
        }
    } // namespace streams
} // namespace assets