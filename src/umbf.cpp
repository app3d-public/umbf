#include <acul/io/fs/file.hpp>
#include <acul/io/fs/path.hpp>
#include <acul/log.hpp>
#include <inttypes.h>
#include <umbf/umbf.hpp>
#include <umbf/utils.hpp>

struct LogContext
{
    acul::log::log_service *log_service;
    acul::log::logger_base *logger;
} g_log{nullptr, nullptr};

#define UMBF_LOG_DEFAULT(level, ...) acul::log::write(g_log.log_service, g_log.logger, level, __VA_ARGS__)
#define UMBF_LOG_INFO(...)           UMBF_LOG_DEFAULT(acul::log::level::info, __VA_ARGS__)
#define UMBF_LOG_DEBUG(...)          UMBF_LOG_DEFAULT(acul::log::level::debug, __VA_ARGS__)
#define UMBF_LOG_TRACE(...)          UMBF_LOG_DEFAULT(acul::log::level::trace, __VA_ARGS__)
#define UMBF_LOG_WARN(...)           UMBF_LOG_DEFAULT(acul::log::level::warn, __VA_ARGS__)
#define UMBF_LOG_ERROR(...)          UMBF_LOG_DEFAULT(acul::log::level::error, __VA_ARGS__)
#define UMBF_LOG_FATAL(...)          UMBF_LOG_DEFAULT(acul::log::level::fatal, __VA_ARGS__)

namespace umbf
{
    APPLIB_API void attach_logger(acul::log::log_service *log_service, acul::log::logger_base *logger) noexcept
    {
        g_log.log_service = log_service;
        g_log.logger = logger;
    }

    APPLIB_API void pack_header(const File::Header &src, File::Header::Pack &dst)
    {
        dst.vendor_sign = src.vendor_sign & 0xFFFFFF;
        dst.compressed = static_cast<u8>(src.compressed);
        dst.vendor_version = src.vendor_version & 0xFFFFFF;
        dst.type_sign_low = static_cast<u8>(src.type_sign & 0x00FF);
        dst.type_sign_high = static_cast<u8>((src.type_sign >> 8) & 0x00FF);
        dst.spec_version = src.spec_version & 0xFFFFFF;
    }

    APPLIB_API void unpack_header(const File::Header::Pack &src, File::Header &dst)
    {
        dst.vendor_sign = src.vendor_sign & 0xFFFFFF;
        dst.compressed = src.compressed != 0;
        dst.vendor_version = src.vendor_version & 0xFFFFFF;
        dst.type_sign = static_cast<u16>((src.type_sign_high << 8) | src.type_sign_low);
        dst.spec_version = src.spec_version & 0xFFFFFF;
    }

    bool save_file(File &file, const acul::path &path, acul::bin_stream &src, int compression)
    {
        acul::bin_stream dst_stream;
        File::Header::Pack pack;
        pack_header(file.header, pack);
        dst_stream.write(UMBF_MAGIC).write(pack);
        if (file.header.compressed)
        {
            acul::vector<char> compressed;
            auto cr = acul::fs::compress(src.data() + src.pos(), src.size() - src.pos(), compressed, compression);
            if (!cr.success())
            {
                UMBF_LOG_ERROR("Failed to compress file. Error code: 0x%016" PRIx64, static_cast<u64>(cr));
                return false;
            }

            dst_stream.write(compressed.data(), compressed.size());
        }
        else
            dst_stream.write(src.data() + src.pos(), src.size() - src.pos());
        file.checksum = acul::crc32(0, src.data(), src.size());
        return acul::fs::write_binary(path.str(), dst_stream.data(), dst_stream.size());
    }

    bool File::save(const acul::string &path, int compression)
    {
        try
        {
            acul::bin_stream stream{};
            stream.write(blocks);
            return save_file(*this, path, stream, compression);
        }
        catch (const std::exception &e)
        {
            UMBF_LOG_ERROR("UMBF write error: %s", e.what());
            return false;
        }
    }

    static bool read_file_header(acul::bin_stream &source, File::Header &header)
    {
        u32 sign_file_format;
        source.read(sign_file_format);
        if (sign_file_format != UMBF_MAGIC)
        {
            UMBF_LOG_ERROR("Invalid file signature");
            return false;
        }
        File::Header::Pack pack;
        source.read(pack);
        unpack_header(pack, header);
        return true;
    }

    bool load_file(acul::bin_stream &source, acul::bin_stream &dst, File::Header &header)
    {
        if (!read_file_header(source, header)) return false;
        if (header.compressed)
        {
            acul::vector<char> decompressed;
            auto dr = acul::fs::decompress(source.data() + source.pos(), source.size() - source.pos(), decompressed);
            if (!dr.success())
            {
                UMBF_LOG_ERROR("Failed to decompress file. Error code: 0x%016" PRIx64, static_cast<u64>(dr));
                return false;
            }
            dst = acul::bin_stream(std::move(decompressed));
        }
        else
            dst = std::move(source);
        return true;
    }

    acul::shared_ptr<File> File::read_from_bytes(acul::bin_stream &bytes)
    {
        try
        {
            acul::bin_stream stream{};
            auto asset = acul::make_shared<File>();
            if (!load_file(bytes, stream, asset->header)) return nullptr;
            auto offset = stream.pos();
            stream.read(asset->blocks);
            if (asset->blocks.begin() == asset->blocks.end()) UMBF_LOG_WARN("UMBF meta data not found");
            asset->checksum = acul::crc32(0, stream.data() + offset, stream.size() - offset);
            return asset;
        }
        catch (std::exception &e)
        {
            UMBF_LOG_ERROR("%s", e.what());
            return nullptr;
        }
    }

    acul::op_result File::read_from_disk(const acul::string &path, acul::shared_ptr<File> &dst)
    {
        acul::vector<char> source_bytes;
        if (!acul::fs::read_binary(path, source_bytes)) return acul::make_op_error(ACUL_OP_READ_ERROR);
        acul::bin_stream source_stream(std::move(source_bytes));
        dst = read_from_bytes(source_stream);
        return dst ? acul::make_op_success() : acul::make_op_error(ACUL_OP_ERROR_GENERIC);
    }

    static bool read_bytes(FILE *fd, void *dst, size_t size)
    {
        return size == 0 || (fd && fread(dst, 1, size, fd) == size);
    }

    APPLIB_API acul::op_result load_library_mapped(const acul::path &path, LibraryMapData &mapping)
    {
        try
        {
            mapping.library.reset();
            mapping.payload_offset = 0;
            mapping.payload_size = 0;
            size_t file_size = acul::fs::read_binary_fd(path, mapping.fd);
            if (file_size == 0) return acul::make_op_error(ACUL_OP_READ_ERROR);
            File::Header header;
            size_t header_section_size = sizeof(File::Header::Pack) + sizeof(u32);
            acul::vector<char> source_bytes(header_section_size);
            if (!read_bytes(mapping.fd, source_bytes.data(), header_section_size))
            {
                fclose(mapping.fd);
                mapping.fd = nullptr;
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }
            acul::bin_stream source_stream(std::move(source_bytes));
            if (!read_file_header(source_stream, header))
            {
                fclose(mapping.fd);
                mapping.fd = nullptr;
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }
            if (header.compressed)
            {
                UMBF_LOG_ERROR("Mapped library must contains uncomressed payload");
                fclose(mapping.fd);
                mapping.fd = nullptr;
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }
            if (header.vendor_sign != UMBF_VENDOR_ID || header.type_sign != sign_block::format::library)
            {
                UMBF_LOG_ERROR("Invalid asset type");
                fclose(mapping.fd);
                mapping.fd = nullptr;
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }

            while (true)
            {
                u64 block_size = 0;
                if (!read_bytes(mapping.fd, &block_size, sizeof(block_size)))
                {
                    fclose(mapping.fd);
                    mapping.fd = nullptr;
                    return acul::make_op_error(ACUL_OP_READ_ERROR);
                }

                if (block_size == 0)
                {
                    const long payload_offset = ftell(mapping.fd);
                    if (payload_offset < 0)
                    {
                        fclose(mapping.fd);
                        mapping.fd = nullptr;
                        return acul::make_op_error(ACUL_OP_READ_ERROR);
                    }
                    mapping.payload_offset = static_cast<u64>(payload_offset);
                    mapping.payload_size = file_size >= mapping.payload_offset ? file_size - mapping.payload_offset : 0;
                    break;
                }

                u32 signature = 0;
                if (!read_bytes(mapping.fd, &signature, sizeof(signature)))
                {
                    fclose(mapping.fd);
                    mapping.fd = nullptr;
                    return acul::make_op_error(ACUL_OP_READ_ERROR);
                }

                acul::vector<char> block_bytes(block_size);
                if (!read_bytes(mapping.fd, block_bytes.data(), block_size))
                {
                    fclose(mapping.fd);
                    mapping.fd = nullptr;
                    return acul::make_op_error(ACUL_OP_READ_ERROR);
                }

                const auto *meta_stream =
                    umbf::streams::resolver ? umbf::streams::resolver->get_stream(signature) : nullptr;
                if (!meta_stream) continue;

                acul::bin_stream block_stream(std::move(block_bytes));
                acul::shared_ptr<Block> block(meta_stream->read(block_stream));
                if (!block) continue;

                if (block->signature() == sign_block::library)
                    mapping.library = acul::static_pointer_cast<Library>(block);
            }

            if (!mapping.library)
            {
                UMBF_LOG_ERROR("Mapped library block not found in the metadata");
                fclose(mapping.fd);
                mapping.fd = nullptr;
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }
        }
        catch (std::exception &e)
        {
            UMBF_LOG_ERROR("%s", e.what());
            if (mapping.fd) fclose(mapping.fd);
            mapping.fd = nullptr;
            return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
        }
        return acul::make_op_success();
    }

    APPLIB_API const Library::Node *get_library_mapped_node(const LibraryMapData &mapping, const acul::path &path,
                                                            u64 &offset, u64 &size)
    {
        offset = 0;
        size = 0;
        if (!mapping.library)
        {
            UMBF_LOG_ERROR("Mapped library is not loaded");
            return nullptr;
        }

        const auto *node = mapping.library->get_node(path);
        if (!node || node->is_folder)
        {
            UMBF_LOG_ERROR("Invalid node recived for mapping: %s", path.str().c_str());
            return nullptr;
        }

        for (const auto &block : node->asset.blocks)
        {
            if (!block || block->signature() != sign_block::mapping) continue;
            auto *range = static_cast<const Mapping *>(block.get());
            if (range->offset > mapping.payload_size || range->size > mapping.payload_size - range->offset)
            {
                UMBF_LOG_ERROR("Mapped node is outside payload bounds: %s", path.str().c_str());
                return nullptr;
            }
            offset = mapping.payload_offset + range->offset;
            size = range->size;
            return node;
        }

        UMBF_LOG_ERROR("Mapping block not found for node: %s", path.str().c_str());
        return nullptr;
    }

    void fill_atlas_pixels(const acul::shared_ptr<Image2D> &image, const acul::shared_ptr<Atlas> &atlas,
                           const acul::vector<acul::shared_ptr<Image2D>> &src)
    {
        const size_t pixel_size = image->format.bytes_per_channel * image->channels.size();
        acul::vector<std::byte> color(pixel_size, std::byte{0});
        utils::fill_color_pixels(color.data(), *image);
        for (size_t i = 0; i < atlas->pack_data.size(); i++)
        {
            if (!src[i]->pixels) throw acul::runtime_error("Pixels cannot be null");
            utils::copy_pixels_to_area(*(src[i]), *image, atlas->pack_data[i]);
        }
    }

    Library::Node *Library::get_node(const acul::path &path)
    {
        Node *current_node = &file_tree;
        for (const auto &it : path)
        {
            auto child_it = std::find_if(current_node->children.begin(), current_node->children.end(),
                                         [&it](const Node &node) { return node.name == it; });

            if (child_it != current_node->children.end())
                current_node = &(*child_it);
            else
            {
                UMBF_LOG_ERROR("Path not found in the umbf library: %s", path.str().c_str());
                return nullptr;
            }
        }
        return current_node;
    }

    const Library::Node *Library::get_node(const acul::path &path) const
    {
        return const_cast<Library *>(this)->get_node(path);
    }

    void Registry::init(const acul::path &path)
    {
        acul::vector<acul::string> files;
        auto lr = acul::fs::list_files(path, files);
        if (!lr.success())
            throw acul::runtime_error(
                acul::format("Failed to list files. Error code: 0x%016" PRIx64, static_cast<u64>(lr)));
        for (const auto &entry : files)
        {
            if (acul::fs::get_extension(entry) == ".umlib")
            {
                try
                {
                    UMBF_LOG_INFO("Loading umbf library: %s", entry.c_str());
                    acul::shared_ptr<File> asset;
                    auto res = File::read_from_disk(entry, asset);
                    if (!res.success())
                    {
                        UMBF_LOG_WARN("Failed to load umbf library %s. Error code: 0x%16" PRIx64, entry.c_str(),
                                      static_cast<u64>(res));
                        continue;
                    }
                    if (asset->header.type_sign != sign_block::format::library)
                    {
                        UMBF_LOG_WARN("Failed to load umbf library %s. Wrong file type: 0x%x", entry.c_str(),
                                      asset->header.type_sign);
                        continue;
                    }
                    auto library = acul::static_pointer_cast<Library>(asset->blocks.front());
                    _libraries.emplace(library->file_tree.name, library);
                    asset->blocks.clear();
                }
                catch (const std::exception &e)
                {
                    UMBF_LOG_WARN("Failed to load umbf library. Error: %s", e.what());
                    continue;
                }
            }
        }
    }
} // namespace umbf

namespace acul
{
    template <>
    bin_stream &bin_stream::write(const vector<acul::shared_ptr<umbf::Block>> &meta)
    {
        for (auto &block : meta)
        {
            assert(block);
            auto *meta_stream = umbf::streams::resolver->get_stream(block->signature());
            if (meta_stream)
            {
                bin_stream tmp{};
                meta_stream->write(tmp, block.get());
                u64 block_size = tmp.size();
                write(block_size).write(block->signature()).write(tmp.data(), block_size);
            }
        }
        return write(0ULL);
    }

    template <>
    bin_stream &bin_stream::read(vector<acul::shared_ptr<umbf::Block>> &meta)
    {
        while (_pos < _data.size())
        {
            struct Header
            {
                u32 signature;
                u64 block_size;
            } header;
            read(header.block_size);
            if (header.block_size == 0ULL) break;

            read(header.signature);
            assert(umbf::streams::resolver);
            auto *meta_stream = umbf::streams::resolver->get_stream(header.signature);
            if (meta_stream)
            {
                acul::shared_ptr<umbf::Block> block(meta_stream->read(*this));
                if (block)
                    meta.push_back(block);
                else
                    UMBF_LOG_WARN("Failed to read umbf meta block: 0x%08x", header.signature);
            }
            else
                shift(header.block_size);
        }
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(vector<umbf::File> &dst)
    {
        u16 size;
        read(size);
        dst.resize(size);
        for (auto &asset : dst) read(asset);
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(umbf::MaterialNode &dst)
    {
        u16 data;
        read(dst.rgb).read(data);
        dst.textured = data >> 15;
        dst.texture_id = dst.textured ? (data & 0x7FFF) : 0;
        return *this;
    }

    template <>
    bin_stream &bin_stream::write(const umbf::Library::Node &node)
    {
        write(node.name).write(node.is_folder);
        u16 child_count = static_cast<u16>(node.children.size());
        write(child_count);
        if (child_count > 0)
            for (const auto &child : node.children) write(child);
        else
        {
            if (!node.is_folder)
            {
                if (node.asset.header.type_sign == umbf::sign_block::format::none)
                    throw acul::runtime_error("Umbf asset is invalid. Possible corrupted file structure");
                write(node.asset);
            }
        }
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(umbf::Library::Node &node)
    {
        read(node.name).read(node.is_folder);
        u16 child_count;
        read(child_count);
        if (child_count > 0)
        {
            node.children.resize(child_count);
            for (auto &child : node.children) read(child);
        }
        else if (!node.is_folder)
        {
            read(node.asset);
            if (node.asset.header.type_sign == umbf::sign_block::format::none)
                throw acul::runtime_error("Umbf file is invalid. Possible corrupted file structure");
        }
        return *this;
    }

} // namespace acul
