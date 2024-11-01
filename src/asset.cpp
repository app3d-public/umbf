#include <assets/asset.hpp>
#include <assets/utils.hpp>
#include <astl/hash.hpp>
#include <core/log.hpp>
#include <io/file.hpp>

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

    bool saveAsset(Asset &asset, const std::filesystem::path &path, astl::bin_stream &src, int compression)
    {
        astl::bin_stream dstStream;
        dstStream.write(sign_format_assets).write(asset.header);
        if (asset.header.compressed)
        {
            astl::vector<char> compressed;
            if (!io::file::compress(src.data() + src.pos(), src.size() - src.pos(), compressed, compression))
            {
                logError("Failed to compress: %s", path.string().c_str());
                return false;
            }

            dstStream.write(compressed.data(), compressed.size());
        }
        else
            dstStream.write(src.data() + src.pos(), src.size() - src.pos());
        asset.checksum = astl::crc32(0, src.data(), src.size());
        return io::file::writeBinary(path.string(), dstStream.data(), dstStream.size());
    }

    bool Asset::save(const std::filesystem::path &path, int compression)
    {
        try
        {
            astl::bin_stream stream{};
            stream.write(blocks);
            return saveAsset(*this, path, stream, compression);
        }
        catch (const std::exception &e)
        {
            logError("Asset write error: %s", e.what());
            return false;
        }
    }

    bool loadAsset(const std::filesystem::path &path, astl::bin_stream &dst, Asset::Header &header)
    {
        astl::vector<char> source;
        if (io::file::readBinary(path.string(), source) != io::file::ReadState::Success) return false;

        astl::bin_stream sourceStream(std::move(source));
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
            astl::vector<char> decompressed;
            if (!io::file::decompress(sourceStream.data() + sourceStream.pos(),
                                      sourceStream.size() - sourceStream.pos(), decompressed))
            {
                logError("Failed to decompress: %s", path.string().c_str());
                return false;
            }
            dst = astl::bin_stream(std::move(decompressed));
        }
        else
            dst = std::move(sourceStream);
        return true;
    }

    astl::shared_ptr<Asset> Asset::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            astl::bin_stream stream{};
            auto asset = astl::make_shared<Asset>();
            if (!loadAsset(path, stream, asset->header)) return nullptr;
            stream.read(asset->blocks);
            if (asset->blocks.begin() == asset->blocks.end()) return nullptr;
            asset->checksum = astl::crc32(0, stream.data(), stream.size());
            return asset;
        }
        catch (std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
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
                    auto library = astl::dynamic_pointer_cast<Library>(asset->blocks.front());
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

namespace astl
{
    template <>
    bin_stream &bin_stream::read(assets::Asset::Header &dst)
    {
        u8 data;
        read(data);
        dst.type = static_cast<assets::Type>(data & 0x3F);
        dst.compressed = (data >> 6) & 0x1;
        return *this;
    }

    template <>
    bin_stream &bin_stream::write(const astl::vector<astl::shared_ptr<meta::Block>> &meta)
    {
        for (auto &block : meta)
        {
            assert(block);
            auto *metaStream = meta::getStream(block->signature());
            if (metaStream)
            {
                bin_stream tmp{};
                metaStream->write(tmp, block.get());
                u64 blockSize = tmp.size();
                write(blockSize).write(block->signature()).write(tmp.data(), blockSize);
            }
        }
        return write(0ULL);
    }

    template <>
    bin_stream &bin_stream::read(astl::vector<astl::shared_ptr<meta::Block>> &meta)
    {
        while (_pos < _data.size())
        {
            meta::Header header;
            read(header.blockSize);
            if (header.blockSize == 0ULL) break;

            read(header.signature);
            auto *metaStream = meta::getStream(header.signature);
            if (metaStream)
            {
                astl::shared_ptr<meta::Block> block(metaStream->read(*this));
                if (block) meta.push_back(block);
            }
            else
                shift(header.blockSize);
        }
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(astl::vector<assets::Asset> &dst)
    {
        u16 size;
        read(size);
        dst.resize(size);
        for (auto &asset : dst) read(asset);
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(assets::MaterialNode &dst)
    {
        u16 data;
        read(dst.rgb).read(data);
        dst.textured = data >> 15;
        dst.textureID = dst.textured ? (data & 0x7FFF) : 0;
        return *this;
    }

    template <>
    bin_stream &bin_stream::write(const assets::Library::Node &node)
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
                if (node.asset.header.type == assets::Type::Invalid)
                    throw std::runtime_error("Asset is invalid. Possible corrupted file structure");
                write(node.asset);
            }
        }
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(assets::Library::Node &node)
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
                if (node.asset.header.type == assets::Type::Invalid)
                    throw std::runtime_error("Asset is invalid. Possible corrupted file structure");
            }
        }
        return *this;
    }
} // namespace astl