#include <assets/asset.hpp>
#include <assets/image.hpp>
#include <assets/library.hpp>
#include <assets/material.hpp>
#include <assets/scene.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>

namespace assets
{
    namespace meta
    {
        HashMap<u32, Stream *> g_Streams;

        void addStream(u32 signature, Stream *stream)
        {
            auto [it, inserted] = g_Streams.try_emplace(signature, stream);
            if (!inserted)
            {
                logWarn("Stream 0x%08x already registered", signature);
                delete stream;
            }
        }

        void initStreams(const DArray<std::pair<u32, Stream *>>& streams)
        {
            g_Streams.insert(streams.begin(), streams.end());
        }

        void clearStreams()
        {
            for (auto &[id, stream] : g_Streams) delete stream;
            g_Streams.clear();
        }

        Stream *getStream(u32 signature)
        {
            auto it = g_Streams.find(signature);
            if (it == g_Streams.end())
            {
                logError("Failed to recognize meta stream signature: 0x%08x", signature);
                return nullptr;
            }
            return it->second;
        }

        /*********************************
         **
         ** Default metadata
         **
         *********************************/

        Block *ExternalStream::readFromStream(BinStream &stream)
        {
            ExternalBlock *block = new ExternalBlock;
            stream.read(block->dataSize);
            block->data = new char[block->dataSize];
            stream.read(block->data, block->dataSize);
            return block;
        }

        void ExternalStream::writeToStream(BinStream &stream, Block *content)
        {
            ExternalBlock *ext = static_cast<ExternalBlock *>(content);
            stream.write(ext->dataSize).write(ext->data, ext->dataSize);
        }
    } // namespace meta

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

    bool Asset::saveFile(const std::filesystem::path &path, BinStream &src, int compression)
    {
        BinStream dstStream;
        dstStream.write(sign_format_assets).write(_info);
        if (_info.compressed)
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
        _checksum = crc32(0, src.data(), src.size());
        return io::file::writeBinary(path.string(), dstStream.data(), dstStream.size());
    }

    bool Asset::loadFile(const std::filesystem::path &path, BinStream &dst, InfoHeader &info)
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
        sourceStream.read(info);
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
BinStream &BinStream::write(const assets::InfoHeader &src)
{
    u8 data = (static_cast<u8>(src.type) & 0x3F) | (static_cast<u8>(src.compressed) << 6);
    return write(data);
}

template <>
BinStream &BinStream::read(assets::InfoHeader &dst)
{
    u8 data;
    read(data);
    dst.type = static_cast<assets::Type>(data & 0x3F);
    dst.compressed = (data >> 6) & 0x1;
    return *this;
}

template <>
BinStream &BinStream::write(const ForwardList<std::shared_ptr<assets::meta::Block>> &meta)
{
    for (auto &block : meta)
    {
        auto *metaStream = assets::meta::getStream(block->signature());
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
BinStream &BinStream::read(ForwardList<std::shared_ptr<assets::meta::Block>> &meta)
{
    while (true)
    {
        assets::meta::Header header;
        read(header.blockSize);
        if (header.blockSize == 0) break;

        read(header.signature);
        auto *metaStream = assets::meta::getStream(header.signature);

        if (metaStream)
        {
            std::shared_ptr<assets::meta::Block> block(metaStream->readFromStream(*this));
            if (block) meta.push_front(std::move(block));
        }
        else
            shift(header.blockSize);
    }
    return *this;
}