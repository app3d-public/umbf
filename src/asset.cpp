#include <assets/asset.hpp>
#include <core/hash.hpp>
#include <core/io/file.hpp>
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

    bool saveAsset(Asset &asset, const std::filesystem::path &path, BinStream &src, int compression)
    {
        BinStream dstStream;
        dstStream.write(sign_format_assets).write(asset.header);
        if (asset.header.compressed)
        {
            DArray<char> compressed;
            if (!io::file::compress(src.data() + src.pos(), src.size() - src.pos(), compressed, compression))
            {
                logError("Failed to compress: %s", path.string().c_str());
                return false;
            }

            dstStream.write(compressed.data(), compressed.size());
        }
        else
            dstStream.write(src.data() + src.pos(), src.size() - src.pos());
        asset.checksum = crc32(0, src.data(), src.size());
        return io::file::writeBinary(path.string(), dstStream.data(), dstStream.size());
    }

    bool Asset::save(const std::filesystem::path &path, int compression)
    {
        BinStream stream{};
        stream.write(blocks);
        return saveAsset(*this, path, stream, compression);
    }

    bool loadAsset(const std::filesystem::path &path, BinStream &dst, Asset::Header &header)
    {
        DArray<char> source;
        if (io::file::readBinary(path.string(), source) != io::file::ReadState::Success) return false;

        BinStream sourceStream(std::move(source));
        u32 sign_file_format;
        sourceStream.read(sign_file_format);
        if (sign_file_format != sign_format_assets)
        {
            logError("Invalid file signature");
            return false;
        }
        sourceStream.read(header);
        if (header.compressed)
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

    std::shared_ptr<Asset> Asset::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            auto asset = std::make_shared<Asset>();
            if (!loadAsset(path, stream, asset->header)) return nullptr;
            stream.read(asset->blocks);
            if (asset->blocks.begin() == asset->blocks.end()) return nullptr;
            asset->checksum = crc32(0, stream.data(), stream.size());
            return asset;
        }
        catch (std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }

    void writeImageInfo(BinStream &stream, Image2D *image2D)
    {
        stream.write(image2D->width).write(image2D->height).write(static_cast<u16>(image2D->channelCount));
        u8 channelNamesSize = image2D->channelNames.size();
        stream.write(reinterpret_cast<char *>(&channelNamesSize), sizeof(u8));
        for (const auto &str : image2D->channelNames) stream.write(str);
        stream.write(image2D->bytesPerChannel).write(static_cast<u8>(image2D->imageFormat));
    }

    void Image2DStream::writeToStream(BinStream &stream, meta::Block *block)
    {
        auto image = static_cast<Image2D *>(block);
        writeImageInfo(stream, image);
        if (!image->pixels) throw std::runtime_error("Pixels cannot be null");
        stream.write(reinterpret_cast<char *>(image->pixels), image->imageSize());
    }

    void readImageInfo(BinStream &stream, Image2D *image2D)
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

    meta::Block *Image2DStream::readFromStream(BinStream &stream)
    {
        Image2D *image = new Image2D();
        readImageInfo(stream, image);
        char *pixels = (char *)scalable_malloc(image->imageSize());
        stream.read(pixels, image->imageSize());
        image->pixels = (void *)pixels;
        return image;
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

    void MaterialStream::writeToStream(BinStream &stream, meta::Block *block)
    {
        auto material = static_cast<Material *>(block);
        stream.write(material->textures).write(material->albedo);
    }

    meta::Block *MaterialStream::readFromStream(BinStream &stream)
    {
        Material *material = new Material();
        stream.read(material->textures).read(material->albedo);
        return material;
    }

    void SceneStream::writeToStream(BinStream &stream, meta::Block *block)
    {
        auto scene = static_cast<Scene *>(block);
        // Objects
        stream.write(static_cast<u16>(scene->objects.size()));
        for (const auto &object : scene->objects)
        {
            stream.write(object->name);
            // Transform
            stream.write(object->transform.position).write(object->transform.rotation).write(object->transform.scale);

            // Meta
            stream.write(object->meta);
        }

        // Textures
        stream.write(scene->textures).write(scene->materials);
    }

    meta::Block *SceneStream::readFromStream(BinStream &stream)
    {
        Scene *scene = new Scene();
        u16 objectCount;
        stream.read(objectCount);
        scene->objects.resize(objectCount);
        for (auto &object : scene->objects)
        {
            object = std::make_shared<Object>();
            stream.read(object->name);
            // Transform
            stream.read(object->transform.position).read(object->transform.rotation).read(object->transform.scale);
            // Meta
            stream.read(object->meta);
        }
        stream.read(scene->textures).read(scene->materials);
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

    meta::Block *MaterialInfoStream::readFromStream(BinStream &stream)
    {
        MaterialInfo *block = new MaterialInfo();
        u32 assignSize;
        stream.read(block->name).read(assignSize);
        block->assignments.resize(assignSize);
        stream.read(block->assignments.data(), assignSize);
        return block;
    }

    void MatRangeAssignStream::writeToStream(BinStream &stream, meta::Block *block)
    {
        MatRangeAssignAtrr *assignment = static_cast<MatRangeAssignAtrr *>(block);
        stream.write(assignment->matID)
            .write(assignment->faces.size())
            .write(assignment->faces.data(), assignment->faces.size());
    }

    meta::Block *MatRangeAssignStream::readFromStream(BinStream &stream)
    {
        MatRangeAssignAtrr *block = new MatRangeAssignAtrr();
        u32 faceSize;
        stream.read(block->matID).read(faceSize);
        block->faces.resize(faceSize);
        stream.read(block->faces.data(), faceSize);
        return block;
    }

    void TargetStream::writeToStream(BinStream &stream, meta::Block *block)
    {
        auto target = static_cast<Target *>(block);
        u8 headerData = (static_cast<u8>(target->header.type) & 0x3F) |                             // Type
                        (static_cast<u8>(target->header.compressed) << 6) |                         // Compressed
                        (static_cast<u8>(target->addr.proto == Target::Addr::Proto::Network) << 7); // Proto
        stream.write(headerData).write(target->addr.url).write(target->checksum);
    }

    meta::Block *TargetStream::readFromStream(BinStream &stream)
    {
        Target *target = new Target();
        u8 headerData;
        stream.read(headerData);
        target->header.type = static_cast<assets::Type>(headerData & 0x3F);
        target->header.compressed = (headerData >> 6) & 0x1;
        target->addr.proto = (headerData & 0x80) ? Target::Addr::Proto::Network : Target::Addr::Proto::File; // Proto
        target->checksum = crc32(0, stream.data(), stream.size());
        return target;
    }

    void LibraryStream::writeToStream(BinStream &stream, meta::Block *block)
    {
        auto library = static_cast<Library *>(block);
        stream.write(library->fileTree);
    }

    meta::Block *LibraryStream::readFromStream(BinStream &stream)
    {
        Library *library = new Library();
        stream.read(library->fileTree);
        return library;
    }

    Library::Node *Library::getNode(const std::filesystem::path &path)
    {
        Node *currentNode = &fileTree;
        for (const auto &it : path)
        {
            auto childIt = std::find_if(currentNode->children.begin(), currentNode->children.end(),
                                        [&it](const Node &node) { return node.name == it.string(); });

            if (childIt != currentNode->children.end())
                currentNode = &(*childIt);
            else
            {
                logError("Path not found in the library: %s", path.string().c_str());
                return nullptr;
            }
        }
        return currentNode;
    }

    void Registry::init(const std::filesystem::path &path)
    {
        if (!std::filesystem::exists(path)) throw std::runtime_error("Asset folder not found: " + path.string());
        for (const auto &entry : std::filesystem::directory_iterator(path))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".a3d")
            {
                try
                {
                    auto asset = Asset::readFromFile(entry.path());
                    if (!asset || asset->header.type != Type::Library) continue;
                    auto library = std::dynamic_pointer_cast<Library>(asset->blocks.front());
                    if (library) _libraries.insert({entry.path().stem().string(), library});
                }
                catch (const std::exception &e)
                {
                    logWarn("Failed to load library %s: %s", entry.path().string().c_str(), e.what());
                    continue;
                }
            }
        }
    }
} // namespace assets

template <>
BinStream &BinStream::read(assets::Asset::Header &dst)
{
    u8 data;
    read(data);
    dst.type = static_cast<assets::Type>(data & 0x3F);
    dst.compressed = (data >> 6) & 0x1;
    return *this;
}

template <>
BinStream &BinStream::write(const DArray<std::shared_ptr<meta::Block>> &meta)
{
    for (auto &block : meta)
    {
        auto *metaStream = meta::getStream(block->signature());
        if (metaStream)
        {
            BinStream tmp{};
            metaStream->writeToStream(tmp, block.get());
            u64 blockSize = tmp.size();
            write(blockSize).write(block->signature()).write(tmp.data(), blockSize);
        }
    }
    return write(0ULL);
}

template <>
BinStream &BinStream::read(DArray<std::shared_ptr<meta::Block>> &meta)
{
    while (_pos < _data.size())
    {
        meta::Header header;
        read(header.blockSize);
        if (header.blockSize == 0) break;

        read(header.signature);
        auto *metaStream = meta::getStream(header.signature);
        if (metaStream)
        {
            std::shared_ptr<meta::Block> block(metaStream->readFromStream(*this));
            if (block) meta.push_back(std::move(block));
        }
        else
            shift(header.blockSize);
    }
    return *this;
}

template <>
BinStream &BinStream::read(DArray<assets::Asset> &dst)
{
    u16 size;
    read(size);
    dst.resize(size);
    for (auto &asset : dst) read(asset);
    return *this;
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

template <>
BinStream &BinStream::write(const assets::Library::Node &node)
{
    write(node.name);
    u16 childCount = static_cast<u16>(node.children.size());
    write(childCount);
    if (childCount > 0)
        for (const auto &child : node.children) write(child);
    else
    {
        write(node.isFolder);
        if (!node.isFolder)
        {
            if (!node.asset) throw std::runtime_error("Asset is null. Possible corrupted file structure");
            write(node.asset);
        }
    }
    return *this;
}

template <>
BinStream &BinStream::read(assets::Library::Node &node)
{
    read(node.name);
    u16 childCount;
    read(childCount);
    if (childCount > 0)
    {
        for (u16 i = 0; i < childCount; ++i)
        {
            assets::Library::Node child;
            read(child);
            node.children.push_back(child);
        }
    }
    else
    {
        read(node.isFolder);
        if (!node.isFolder)
        {
            read(node.asset);
            if (!node.asset) throw std::runtime_error("Asset is null. Possible corrupted file structure");
        }
    }
    return *this;
}