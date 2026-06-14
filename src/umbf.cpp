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
    static constexpr u64 g_mapped_block_prefix_size = sizeof(u64) + sizeof(u32);
    static constexpr u64 g_raw_block_data_size_field = sizeof(u64);

    UMBF_EXPORT void attach_logger(acul::log::log_service *log_service, acul::log::logger_base *logger) noexcept
    {
        g_log.log_service = log_service;
        g_log.logger = logger;
    }

    UMBF_EXPORT void pack_header(const File::Header &src, File::Header::Pack &dst)
    {
        dst.vendor_sign = src.vendor_sign & 0xFFFFFF;
        dst.flags = src.flags;
        dst.vendor_version = src.vendor_version & 0xFFFFFF;
        dst.type_sign_low = static_cast<u8>(src.type_sign & 0x00FF);
        dst.type_sign_high = static_cast<u8>((src.type_sign >> 8) & 0x00FF);
        dst.spec_version = src.spec_version & 0xFFFFFF;
    }

    UMBF_EXPORT void unpack_header(const File::Header::Pack &src, File::Header &dst)
    {
        dst.vendor_sign = src.vendor_sign & 0xFFFFFF;
        dst.flags = static_cast<u8>(src.flags);
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
        if (file.header.flags & UMBF_COMPRESSION_PAYLOAD_BIT)
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
        if (header.flags & UMBF_COMPRESSION_PAYLOAD_BIT)
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

    static void close_library_map_fd(LibraryMapData &mapping)
    {
        if (mapping.fd) fclose(mapping.fd);
        mapping.fd = nullptr;
    }

    static bool ensure_library_map_fd(const LibraryMapData &mapping)
    {
        if (mapping.fd) return true;
        if (mapping.path.str().empty()) return false;

        FILE *fd = nullptr;
        const size_t file_size = acul::fs::read_binary_fd(mapping.path.str(), fd);
        if (file_size == 0 || !fd) return false;
        mapping.fd = fd;
        return true;
    }

    static u64 read_next_meta_block(acul::bin_stream &stream, acul::shared_ptr<Block> &block)
    {
        u64 block_size = 0;
        stream.read(block_size);
        if (block_size == 0ULL) return 0ULL;

        u32 signature;
        stream.read(signature);
        assert(umbf::streams::resolver);
        auto *meta_stream = umbf::streams::resolver->get_stream(signature);
        if (meta_stream)
        {
            block = acul::shared_ptr<Block>(meta_stream->read(stream));
            if (!block) UMBF_LOG_WARN("Failed to read umbf meta block: 0x%08x", signature);
        }
        else
            stream.shift(block_size);
        return block_size;
    }

    UMBF_EXPORT acul::op_result load_library_mapped(const acul::path &path, LibraryMapData &mapping)
    {
        try
        {
            close_library_map_fd(mapping);
            mapping.path = path;
            mapping.library.reset();
            mapping.payload_offset = 0;
            mapping.payload_size = 0;
            mapping.compressed = false;
            const size_t file_size = acul::fs::read_binary_fd(path, mapping.fd);
            if (file_size == 0) return acul::make_op_error(ACUL_OP_READ_ERROR);
            File::Header header;
            size_t header_section_size = sizeof(File::Header::Pack) + sizeof(u32);
            acul::vector<char> source_bytes(header_section_size);
            if (fread(source_bytes.data(), 1, header_section_size, mapping.fd) != header_section_size)
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }
            acul::bin_stream source_stream(std::move(source_bytes));
            if (!read_file_header(source_stream, header))
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }
            mapping.compressed = header.flags & UMBF_COMPRESSION_MAPPED_BIT;
            if (header.vendor_sign != UMBF_VENDOR_ID || header.type_sign != sign_block::format::library)
            {
                UMBF_LOG_ERROR("Invalid asset type");
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }

            const i64 first_block_offset = acul::fs::ftell(mapping.fd);
            if (first_block_offset < 0)
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }

            u64 library_block_size = 0;
            if (fread(&library_block_size, sizeof(library_block_size), 1, mapping.fd) != 1 || library_block_size == 0)
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }

            if (acul::fs::fseek(mapping.fd, static_cast<u64>(first_block_offset), SEEK_SET) != 0)
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }

            acul::vector<char> library_buffer(12 + library_block_size);
            if (fread(library_buffer.data(), 1, library_buffer.size(), mapping.fd) != library_buffer.size())
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }

            acul::bin_stream library_stream(std::move(library_buffer));
            acul::shared_ptr<umbf::Block> block;
            if (umbf::read_next_meta_block(library_stream, block) == 0ULL || !block ||
                block->signature() != sign_block::library)
            {
                UMBF_LOG_ERROR("First mapped block must be library");
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }

            mapping.library = acul::static_pointer_cast<Library>(block);
            const i64 raw_block_offset = acul::fs::ftell(mapping.fd);
            if (raw_block_offset < 0)
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }

            u64 raw_block_size = 0;
            if (fread(&raw_block_size, sizeof(raw_block_size), 1, mapping.fd) != 1 || raw_block_size < sizeof(u64))
            {
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_READ_ERROR);
            }

            mapping.payload_offset =
                static_cast<u64>(raw_block_offset) + g_mapped_block_prefix_size + g_raw_block_data_size_field;
            mapping.payload_size = raw_block_size - g_raw_block_data_size_field;

            if (mapping.payload_size == 0)
            {
                UMBF_LOG_ERROR("Mapped library metadata is incomplete");
                close_library_map_fd(mapping);
                return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }
        }
        catch (std::exception &e)
        {
            UMBF_LOG_ERROR("%s", e.what());
            close_library_map_fd(mapping);
            return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
        }
        return acul::make_op_success();
    }

    UMBF_EXPORT acul::op_result load_library_mapped_data(const LibraryMapData &mapping,
                                                        const acul::shared_ptr<Mapping> &node_mapping,
                                                        acul::vector<char> &dst)
    {
        if (!node_mapping) return acul::make_op_error(ACUL_OP_NULLPTR);
        if (!ensure_library_map_fd(mapping)) return acul::make_op_error(ACUL_OP_READ_ERROR);
        if (node_mapping->offset > mapping.payload_size ||
            node_mapping->size > mapping.payload_size - node_mapping->offset)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);

        const u64 offset = mapping.payload_offset + node_mapping->offset;
        if (acul::fs::fseek(mapping.fd, offset, SEEK_SET) != 0) return acul::make_op_error(ACUL_OP_READ_ERROR);

        acul::vector<char> src(static_cast<size_t>(node_mapping->size));
        if (fread(src.data(), 1, src.size(), mapping.fd) != src.size())
            return acul::make_op_error(ACUL_OP_READ_ERROR);

        if (mapping.compressed) return acul::fs::decompress(src.data(), src.size(), dst);

        dst = std::move(src);
        return acul::make_op_success();
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
            acul::shared_ptr<umbf::Block> block;
            u64 block_size = umbf::read_next_meta_block(*this, block);
            if (block_size == 0ULL) break;
            meta.push_back(block);
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
