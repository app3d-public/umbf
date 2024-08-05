#include <assets/library.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

namespace assets
{
    io::file::ReadState Target::fetchToCache(const std::filesystem::path &relativeRoot,
                                             const std::filesystem::path &cachePath,
                                             std::filesystem::copy_options options)
    {
        switch (_targetInfo.proto)
        {
            case TargetInfo::Proto::File:
            {
                if (std::filesystem::exists(cachePath) && options == std::filesystem::copy_options::skip_existing)
                    return io::file::ReadState::Cancelled;

                std::filesystem::path url = _targetInfo.url;
                if (url.is_relative()) url = relativeRoot / url;
                if (url == cachePath) return io::file::ReadState::Cancelled;
                logInfo("Fetching target '%s' from local repo to cache", _targetInfo.url.c_str());
                if (::io::file::copyFile(url, cachePath, options))
                    return io::file::ReadState::Success;
                else
                    return io::file::ReadState::Error;
            }
            default:
                logError("The target has unsupported transport protocol");
                return io::file::ReadState::Error;
        }
    }

    bool Target::writeToStream(BinStream &stream)
    {
        u8 headerData = (static_cast<u8>(_targetInfo.type) & 0x3F) |                             // Type
                        (static_cast<u8>(_targetInfo.compressed) << 6) |                         // Compressed
                        (static_cast<u8>(_targetInfo.proto == TargetInfo::Proto::Network) << 7); // Proto
        stream.write(headerData).write(_targetInfo.url).write(_targetChecksum);
        return true;
    }

    std::shared_ptr<Target> Target::readFromStream(InfoHeader &assetInfo, BinStream &stream)
    {
        TargetInfo targetInfo;
        u32 targetChecksum;
        u8 headerData;
        stream.read(headerData).read(targetInfo.url).read(targetChecksum);
        targetInfo.type = static_cast<Type>(headerData & 0x3F);                                        // Type
        targetInfo.compressed = (headerData >> 6) & 0x1;                                               // Compressed
        targetInfo.proto = (headerData & 0x80) ? TargetInfo::Proto::Network : TargetInfo::Proto::File; // Proto
        u32 checksum = crc32(0, stream.data(), stream.size());
        auto target = std::make_shared<Target>(assetInfo, targetInfo, targetChecksum, checksum);
        if (!target) return nullptr;
        Asset::readMeta(stream, target->meta);
        return target;
    }

    bool Target::save(const std::filesystem::path &path, int compression)
    {
        BinStream stream{};
        writeToStream(stream);
        return saveFile(path, stream, compression);
    }

    std::shared_ptr<Target> Target::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader targetInfo;
            if (!loadFile(path, stream, targetInfo)) return nullptr;
            return Target::readFromStream(targetInfo, stream);
        }
        catch (const std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }

    bool Library::writeToStream(const FileNode &node, BinStream &stream)
    {
        stream.write(node.name);
        u16 childCount = static_cast<u16>(node.children.size());
        stream.write(childCount);
        if (childCount > 0)
        {
            for (const auto &child : node.children) writeToStream(child, stream);
        }
        else
        {
            stream.write(node.isFolder);
            if (!node.isFolder)
            {
                if (!node.asset) return false;
                stream.write(node.asset->info());
                if (!node.asset->writeToStream(stream)) return false;
            }
        }
        return true;
    }

    bool Library::save(const std::filesystem::path &path, int compression)
    {
        BinStream stream{};
        if (!writeToStream(stream)) return false;
        return saveFile(path, stream, compression);
    }

    bool Library::readFromStream(FileNode &node, BinStream &stream)
    {
        stream.read(node.name);
        u16 childCount;
        stream.read(childCount);
        if (childCount > 0)
        {
            for (u16 i = 0; i < childCount; ++i)
            {
                FileNode child;
                readFromStream(child, stream);
                node.children.push_back(child);
            }
        }
        else
        {
            stream.read(node.isFolder);
            if (!node.isFolder)
            {
                InfoHeader info;
                stream.read(info);
                node.asset = Asset::readFromStream(info, stream);
                if (!node.asset) return false;
            }
        }
        return true;
    }

    std::shared_ptr<Library> Library::readFromStream(InfoHeader &assetInfo, BinStream &stream)
    {
        FileNode fileTree{};
        readFromStream(fileTree, stream);
        u32 checksum = crc32(0, stream.data(), stream.size());
        auto library = std::make_shared<Library>(assetInfo, fileTree, checksum);
        if (!library) return nullptr;
        Asset::readMeta(stream, library->meta);
        return library;
    }

    std::shared_ptr<Library> Library::readFromFile(const std::filesystem::path &path)
    {
        try
        {
            BinStream stream{};
            InfoHeader assetInfo;
            if (!loadFile(path, stream, assetInfo)) return nullptr;

            if (assetInfo.type != Type::Library)
            {
                logError("Invalid asset type: %s", toString(assetInfo.type).c_str());
                return nullptr;
            }
            return readFromStream(assetInfo, stream);
        }
        catch (const std::exception &e)
        {
            logError("%s", e.what());
            return nullptr;
        }
    }

    FileNode *Library::getFileNode(const std::filesystem::path &path)
    {
        FileNode *currentNode = &_fileTree;
        for (const auto &it : path)
        {
            auto childIt = std::find_if(currentNode->children.begin(), currentNode->children.end(),
                                        [&it](const FileNode &node) { return node.name == it.string(); });

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

    void Manager::init(const std::filesystem::path &path)
    {
        if (!std::filesystem::exists(path)) throw std::runtime_error("Asset folder not found: " + path.string());
        for (const auto &entry : std::filesystem::directory_iterator(path))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".a3d")
            {
                try
                {
                    std::shared_ptr<Library> library = Library::readFromFile(entry.path());
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