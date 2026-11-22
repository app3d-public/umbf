#include <acul/io/fs/file.hpp>
#include <acul/io/fs/path.hpp>
#include <acul/log.hpp>
#include <cstdlib>
#include <inttypes.h>
#include <umbf/umbf.hpp>
#include "read.hpp"

LogContext g_log{nullptr, nullptr};

namespace umbf
{
    namespace
    {
        class ScopedStreamSize
        {
        public:
            ScopedStreamSize(acul::bin_stream &stream, size_t size) : _stream(stream), _size(stream.size())
            {
                _stream.size(size);
            }

            ~ScopedStreamSize() noexcept { _stream.size(_size); }

            ScopedStreamSize(const ScopedStreamSize &) = delete;
            ScopedStreamSize &operator=(const ScopedStreamSize &) = delete;

        private:
            acul::bin_stream &_stream;
            size_t _size;
        };

        bool read_block_header(const char *data, u64 available, u32 &signature, u32 &tag, u64 &size)
        {
            if (!data || available < detail::block_header_size) return false;
            acul::bin_stream stream(data, static_cast<u32>(detail::block_header_size));
            stream.read(signature).read(tag).read(size);
            return signature != 0u || (tag == 0u && size == 0u);
        }

        bool read_block_view(const SegmentBuffer &segment, u32 block_count, u32 requested, PayloadView &view,
                             u64 *header_offset = nullptr, u64 *block_end = nullptr)
        {
            if (requested >= block_count) return false;
            u64 offset = 0u;
            for (u32 index = 0u; index < block_count; ++index)
            {
                const u64 current_header = offset;
                u32 signature = 0u;
                u32 tag = 0u;
                u64 size = 0u;
                if (offset > segment.size() || detail::block_header_size > segment.size() - offset ||
                    !read_block_header(segment.data() + offset, segment.size() - offset, signature, tag, size))
                    return false;
                offset += detail::block_header_size;
                if (size > segment.size() - offset) return false;
                if (index == requested)
                {
                    view = {signature, tag, segment.data() + offset, size};
                    if (header_offset) *header_offset = current_header;
                    if (block_end) *block_end = offset + size;
                    return true;
                }
                offset += size;
            }
            return false;
        }

        bool validate_segment_data(const acul::vector<char> &bytes, u32 block_count)
        {
            u64 offset = 0u;
            for (u32 index = 0u; index < block_count; ++index)
            {
                u32 signature = 0u;
                u32 tag = 0u;
                u64 size = 0u;
                if (offset > bytes.size() || detail::block_header_size > bytes.size() - offset ||
                    !read_block_header(bytes.data() + offset, bytes.size() - offset, signature, tag, size))
                    return false;
                offset += detail::block_header_size;
                if (size > bytes.size() - offset) return false;
                offset += size;
            }
            return offset == bytes.size();
        }

        acul::op_result encode_compressed_segment(const SegmentBuffer &segment, u32, int compression,
                                                  acul::vector<char> &dst)
        {
            return acul::fs::compress(segment.data(), segment.size(), dst, compression);
        }

        acul::op_result decode_compressed_segment(const char *data, u64 size, u32, acul::vector<char> &dst)
        {
            if (size != 0u && !data) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
            return acul::fs::decompress(data, static_cast<size_t>(size), dst);
        }

        bool locate_block(const File &file, u64 index, u32 &segment_index, u32 &block_index)
        {
            for (u32 segment = 0u; segment < file.sg_table.size(); ++segment)
            {
                const u32 count = file.sg_table[segment].block_count;
                if (index < count)
                {
                    segment_index = segment;
                    block_index = static_cast<u32>(index);
                    return true;
                }
                index -= count;
            }
            return false;
        }

        void write_file_header(acul::bin_stream &stream, const File &file, bool with_magic)
        {
            if (with_magic) stream.write(static_cast<u32>(UMBF_MAGIC));
            stream.write(file.vendor_sign).write(file.vendor_version).write(file.spec_version).write(file.type_sign);
        }

        void write_segment_table(acul::bin_stream &stream, const File &file)
        {
            for (const auto &segment : file.sg_table)
                stream.write(segment.offset)
                    .write(segment.size)
                    .write(segment.block_count)
                    .write(segment.signature)
                    .write(segment.checksum);
        }

    } // namespace

    UMBF_EXPORT void attach_logger(acul::log::log_service *log_service, acul::log::logger_base *logger) noexcept
    {
        g_log.log_service = log_service;
        g_log.logger = logger;
    }

    static bool write_bytes(FILE *fd, const void *data, size_t size)
    {
        return size == 0u || (data && std::fwrite(data, 1u, size, fd) == size);
    }

    static bool write_layout(WriteDescriptor &file, u64 table_size, u64 payload_size, i64 restore)
    {
        if (!file.fd || acul::fs::fseek(file.fd, file.file_offset + sizeof(Header), SEEK_SET) != 0) return false;
        acul::bin_stream layout;
        layout.write(table_size).write(payload_size);
        return write_bytes(file.fd, layout.data(), layout.size()) && acul::fs::fseek(file.fd, restore, SEEK_SET) == 0;
    }

    static bool finalize_segment(WriteDescriptor &file)
    {
        if (!file.has_active_segment) return true;
        FILE *fd = file.fd;
        if (!fd || !file.file) return false;
        if (file.active_segment.block_count == 0u)
        {
            file.active_segment = {};
            file.active_segment_data.clear();
            file.has_active_segment = false;
            return true;
        }
        Segment descriptor;
        descriptor.signature = file.active_segment.signature;
        descriptor.block_count = file.active_segment.block_count;
        descriptor.checksum = file.active_segment.checksum;
        descriptor.offset = file.active_segment_offset - file.payload_offset;
        if (descriptor.signature == UMBF_SEGMENT_RAW)
        {
            const i64 end = acul::fs::ftell(fd);
            if (end < 0 || static_cast<u64>(end) < file.active_segment_offset) return false;
            descriptor.size = static_cast<u64>(end) - file.active_segment_offset;
        }
        else
        {
            if (!validate_segment_data(file.active_segment_data, descriptor.block_count) || !registry::resolver)
                return false;
            const auto *codec = registry::resolver->get_segment_codec(descriptor.signature);
            if (!codec) return false;
            acul::vector<char> encoded;
            if (!codec->encode(file.active_segment_data, descriptor.block_count, file.compression, encoded).success())
                return false;
            descriptor.size = encoded.size();
            if (!write_bytes(fd, encoded.data(), encoded.size())) return false;
        }
        file.file->sg_table.push_back(descriptor);
        file.active_segment = {};
        file.active_segment_data.clear();
        file.has_active_segment = false;
        return true;
    }

    static FILE *open_write_file(const acul::string &path, bool readback)
    {
#ifdef _WIN32
        const auto wide_path = acul::utf8_to_utf16(path);
        return _wfopen(reinterpret_cast<const wchar_t *>(wide_path.c_str()), readback ? L"wb+" : L"wb");
#else
        return std::fopen(path.c_str(), readback ? "wb+" : "wb");
#endif
    }

    static acul::op_result initialize_write_descriptor(FILE *fd, const acul::path &path, const Header &header,
                                                       WriteDescriptor &file, int compression, bool with_magic,
                                                       bool external_table = false)
    {
        close_write_descriptor(file);
        file.fd = fd;
        if (!fd) return acul::make_op_error(ACUL_OP_WRITE_ERROR);
        file.path = path;
        file.file = acul::make_shared<File>();
        static_cast<Header &>(*file.file) = header;
        file.compression = compression;
        file.finalized = false;
        file.external_table = external_table;
        acul::bin_stream stream;
        write_file_header(stream, *file.file, with_magic);
        stream.write(external_table ? 0u : static_cast<u64>(UMBF_INVALID_SIZE))
            .write(external_table ? 0u : static_cast<u64>(UMBF_INVALID_SIZE));
        if (!write_bytes(fd, stream.data(), stream.size()))
        {
            close_write_descriptor(file);
            return acul::make_op_error(ACUL_OP_WRITE_ERROR);
        }
        file.file_offset = with_magic ? sizeof(u32) : 0u;
        file.payload_offset = stream.size();
        return acul::make_op_success();
    }

    acul::op_result create_write_descriptor(const Header &header, WriteDescriptor &file, int compression)
    {
        close_write_descriptor(file);
        return initialize_write_descriptor(std::tmpfile(), {}, header, file, compression, false);
    }

    acul::op_result create_write_descriptor(const acul::path &path, const Header &header, WriteDescriptor &file,
                                            int compression)
    {
        close_write_descriptor(file);
        return initialize_write_descriptor(open_write_file(path.str(), true), path, header, file, compression, true);
    }

    acul::op_result create_external_write_descriptor(const acul::path &path, const Header &header,
                                                      WriteDescriptor &file, int compression)
    {
        close_write_descriptor(file);
        if (!acul::fs::exists(path.str().c_str()))
            return initialize_write_descriptor(open_write_file(path.str(), true), path, header, file, compression,
                                               true, true);

        ReadDescriptor existing;
        ACUL_TRY(create_read_descriptor(path, existing));
        if (!existing.file || existing.table_size != 0u || existing.file->vendor_sign != header.vendor_sign ||
            existing.file->vendor_version != header.vendor_version || existing.file->spec_version != header.spec_version ||
            existing.file->type_sign != header.type_sign)
            return acul::make_op_error(ACUL_OP_READ_ERROR);
        const u64 payload_size = existing.payload_size;
        close_read_descriptor(existing);
#ifdef _WIN32
        const auto wide_path = acul::utf8_to_utf16(path.str());
        FILE *fd = _wfopen(reinterpret_cast<const wchar_t *>(wide_path.c_str()), L"rb+");
#else
        FILE *fd = std::fopen(path.str().c_str(), "rb+");
#endif
        if (!fd) return acul::make_op_error(ACUL_OP_WRITE_ERROR);
        file.fd = fd;
        file.path = path;
        file.file = acul::make_shared<File>();
        static_cast<Header &>(*file.file) = header;
        file.file_offset = sizeof(u32);
        file.payload_offset = sizeof(u32) + sizeof(Header) + detail::layout_header_size;
        file.payload_size = payload_size;
        file.table_size = 0u;
        file.file_size = sizeof(Header) + detail::layout_header_size + payload_size;
        file.compression = compression;
        file.external_table = true;
        if (acul::fs::fseek(fd, file.payload_offset + payload_size, SEEK_SET) != 0)
        {
            close_write_descriptor(file);
            return acul::make_op_error(ACUL_OP_SEEK_ERROR);
        }
        return acul::make_op_success();
    }

    SegmentWriter *add_segment(WriteDescriptor &file, u32 signature)
    {
        if (file.finalized || (file.external_table && file.has_active_segment) || !finalize_segment(file)) return nullptr;
        FILE *fd = file.fd;
        if (!fd) return nullptr;
        const i64 offset = acul::fs::ftell(fd);
        if (offset < 0) return nullptr;
        file.active_segment = {signature, 0u, 0u};
        file.active_segment_offset = static_cast<u64>(offset);
        file.has_active_segment = true;
        return &file.active_segment;
    }

    u32 add_payload(WriteDescriptor &file, u32 signature, const void *data, u64 size, SegmentWriter *segment, u32 tag)
    {
        if ((!signature && (tag != 0u || size != 0u || data)) || (size != 0u && !data))
            throw acul::runtime_error("Invalid UMBF payload");
        if (!segment) segment = add_segment(file, file.default_segment_signature);
        if (!segment || segment != &file.active_segment || !file.has_active_segment)
            throw acul::runtime_error("Invalid UMBF segment writer");
        acul::bin_stream header;
        header.write(signature).write(tag).write(size);
        const u32 index = segment->block_count++;
        if (segment->signature == UMBF_SEGMENT_RAW)
        {
            FILE *fd = file.fd;
            if (!fd || !write_bytes(fd, header.data(), header.size()) ||
                !write_bytes(fd, data, static_cast<size_t>(size)))
                throw acul::runtime_error("Failed to write UMBF block");
            segment->checksum = acul::crc32(segment->checksum, header.data(), header.size());
            if (size != 0u)
                segment->checksum =
                    acul::crc32(segment->checksum, static_cast<const char *>(data), static_cast<size_t>(size));
        }
        else
        {
            segment->checksum = acul::crc32(segment->checksum, header.data(), header.size());
            if (size != 0u)
                segment->checksum =
                    acul::crc32(segment->checksum, static_cast<const char *>(data), static_cast<size_t>(size));
            file.active_segment_data.insert(file.active_segment_data.end(), header.begin(), header.end());
            if (size != 0u)
            {
                const auto *src = static_cast<const char *>(data);
                file.active_segment_data.insert(file.active_segment_data.end(), src, src + static_cast<size_t>(size));
            }
        }
        return index;
    }

    bool add_block(WriteDescriptor &file, Block *block, SegmentWriter *segment)
    {
        const u32 signature = block ? block->signature() : 0u;
        try
        {
            if (!block)
            {
                add_payload(file, 0u, nullptr, 0u, segment, 0u);
                return true;
            }
            assert(registry::resolver && "Registry is not initialized");
            const auto *block_stream = registry::resolver->get_block_stream(signature);
            if (!block_stream) throw acul::runtime_error("No UMBF writer for block");
            acul::bin_stream stream;
            block_stream->write(stream, block);
            add_payload(file, signature, stream.data(), stream.size(), segment, block->tag);
            return true;
        }
        catch (const std::exception &e)
        {
            UMBF_LOG_ERROR("failed to serialize UMBF block 0x%08x: %s", signature, e.what());
        }
        catch (...)
        {
            UMBF_LOG_ERROR("failed to serialize UMBF block 0x%08x", signature);
        }
        return false;
    }

    u32 add_file(WriteDescriptor &file, WriteDescriptor &nested, SegmentWriter *segment)
    {
        if (!nested.file || !nested.file->type_sign || !finalize_file(nested))
            throw acul::runtime_error("Invalid nested UMBF file");
        if (!segment) segment = add_segment(file, UMBF_SEGMENT_RAW);
        if (!segment || segment != &file.active_segment || segment->signature != UMBF_SEGMENT_RAW)
            throw acul::runtime_error("Nested UMBF files require a raw parent segment");
        acul::bin_stream header;
        header.write(nested.file->type_sign).write(u32{0}).write(nested.file_size);
        FILE *fd = file.fd;
        FILE *nested_fd = nested.fd;
        if (!fd || !nested_fd || !write_bytes(fd, header.data(), header.size()) ||
            acul::fs::fseek(nested_fd, nested.file_offset, SEEK_SET) != 0)
            throw acul::runtime_error("Failed to write nested UMBF header");
        segment->checksum = acul::crc32(segment->checksum, header.data(), header.size());
        char buffer[64 * 1024];
        u64 remaining = nested.file_size;
        while (remaining != 0u)
        {
            const size_t chunk = static_cast<size_t>(remaining < sizeof(buffer) ? remaining : sizeof(buffer));
            if (std::fread(buffer, 1u, chunk, nested_fd) != chunk || !write_bytes(fd, buffer, chunk))
                throw acul::runtime_error("Failed to copy nested UMBF file");
            segment->checksum = acul::crc32(segment->checksum, buffer, chunk);
            remaining -= chunk;
        }
        return segment->block_count++;
    }

    acul::op_result commit_segment(WriteDescriptor &file, Segment &segment)
    {
        if (!file.external_table || file.finalized || !file.has_active_segment)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        if (!finalize_segment(file) || file.file->sg_table.empty())
            return acul::make_op_error(ACUL_OP_WRITE_ERROR);
        segment = file.file->sg_table.back();
        file.file->sg_table.clear();
        const i64 end = acul::fs::ftell(file.fd);
        if (end < 0 || static_cast<u64>(end) < file.payload_offset)
            return acul::make_op_error(ACUL_OP_SEEK_ERROR);
        file.payload_size = static_cast<u64>(end) - file.payload_offset;
        file.file_size = sizeof(Header) + detail::layout_header_size + file.payload_size;
        if (!write_layout(file, 0u, file.payload_size, end) || std::fflush(file.fd) != 0)
            return acul::make_op_error(ACUL_OP_WRITE_ERROR);
        return acul::make_op_success();
    }

    bool finalize_file(WriteDescriptor &file)
    {
        FILE *fd = file.fd;
        if (!fd || !file.file) return false;
        if (file.finalized) return true;
        if (file.external_table && file.has_active_segment) return false;
        if (!finalize_segment(file)) return false;
        if (file.external_table)
        {
            const i64 end = acul::fs::ftell(fd);
            if (end < 0 || static_cast<u64>(end) < file.payload_offset) return false;
            file.payload_size = static_cast<u64>(end) - file.payload_offset;
            file.table_size = 0u;
            file.file_size = sizeof(Header) + detail::layout_header_size + file.payload_size;
            file.file->sg_table.clear();
            if (!write_layout(file, 0u, file.payload_size, end) || std::fflush(fd) != 0) return false;
            file.finalized = true;
            return true;
        }
        const i64 table_position = acul::fs::ftell(fd);
        if (table_position < 0 || static_cast<u64>(table_position) < file.payload_offset)
            return false;
        acul::bin_stream table;
        write_segment_table(table, *file.file);
        if (!write_bytes(fd, table.data(), table.size())) return false;
        const i64 end = acul::fs::ftell(fd);
        if (end < 0) return false;
        file.file_size = static_cast<u64>(end) - file.file_offset;
        file.payload_size = static_cast<u64>(table_position) - file.payload_offset;
        file.table_size = table.size();
        file.file->checksum = calculate_checksum(file.file->sg_table);
        if (!write_layout(file, file.table_size, file.payload_size, end)) return false;
        if (std::fflush(fd) != 0) return false;
        file.finalized = true;
        return true;
    }

    bool save_file(WriteDescriptor &file, const acul::string &path)
    {
        if (!finalize_file(file)) return false;
        if (file.file_offset == sizeof(u32) && file.path.str() == acul::path(path).str()) return true;
        FILE *fd = file.fd;
        if (!fd || acul::fs::fseek(fd, file.file_offset, SEEK_SET) != 0) return false;
        FILE *output = open_write_file(path, false);
        if (!output) return false;
        const u32 magic = UMBF_MAGIC;
        bool success = write_bytes(output, &magic, sizeof(magic));
        char buffer[64 * 1024];
        u64 remaining = file.file_size;
        while (success && remaining != 0u)
        {
            const size_t chunk = static_cast<size_t>(remaining < sizeof(buffer) ? remaining : sizeof(buffer));
            success = std::fread(buffer, 1u, chunk, fd) == chunk && write_bytes(output, buffer, chunk);
            remaining -= chunk;
        }
        return std::fclose(output) == 0 && success;
    }

    void close_read_descriptor(ReadDescriptor &view) noexcept
    {
        view.mapping.reset();
        static_cast<FileDescriptorBase &>(view) = {};
    }

    void close_write_descriptor(WriteDescriptor &view) noexcept
    {
        if (view.fd) std::fclose(view.fd);
        view.fd = nullptr;
        static_cast<FileDescriptorBase &>(view) = {};
        view.default_segment_signature = UMBF_SEGMENT_RAW;
        view.compression = 5;
        view.finalized = false;
        view.external_table = false;
        view.has_active_segment = false;
        view.active_segment_offset = 0u;
        view.active_segment = {};
        view.active_segment_data.clear();
    }

    WriteDescriptor::~WriteDescriptor() noexcept { close_write_descriptor(*this); }

    void WriteDescriptor::swap(WriteDescriptor &other) noexcept
    {
        using std::swap;
        swap(static_cast<FileDescriptorBase &>(*this), static_cast<FileDescriptorBase &>(other));
        swap(fd, other.fd);
        swap(default_segment_signature, other.default_segment_signature);
        swap(compression, other.compression);
        swap(finalized, other.finalized);
        swap(external_table, other.external_table);
        swap(has_active_segment, other.has_active_segment);
        swap(active_segment_offset, other.active_segment_offset);
        swap(active_segment, other.active_segment);
        swap(active_segment_data, other.active_segment_data);
    }

    WriteDescriptor::WriteDescriptor(WriteDescriptor &&other) noexcept { swap(other); }

    WriteDescriptor &WriteDescriptor::operator=(WriteDescriptor &&other) noexcept
    {
        if (this != &other)
        {
            close_write_descriptor(*this);
            swap(other);
        }
        return *this;
    }

    static acul::op_result create_read_descriptor_at(acul::shared_ptr<acul::fs::mapped_file> mapping, acul::path path,
                                                     u64 offset, u64 size, bool magic, ReadDescriptor &view)
    {
        close_read_descriptor(view);
        const u64 header_size = magic ? 20u : 16u;
        if (!mapping || offset > mapping->size() || size > mapping->size() - offset ||
            size < header_size + detail::layout_header_size)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        ReadDescriptor parsed;
        parsed.mapping = std::move(mapping);
        parsed.path = std::move(path);
        parsed.file = acul::make_shared<File>();
        const char *data = parsed.mapping->data();
        detail::Layout layout;
        ACUL_TRY(detail::parse_header({data + offset, header_size}, magic, *parsed.file));
        ACUL_TRY(detail::parse_layout({data + offset + header_size, detail::layout_header_size}, offset, size, magic,
                                      layout));
        ACUL_TRY(detail::parse_table({data + layout.table_offset, layout.table_size}, *parsed.file, layout));
        parsed.file_offset = offset;
        parsed.file_size = layout.file_size;
        parsed.payload_offset = layout.payload_offset;
        parsed.payload_size = layout.payload_size;
        parsed.table_size = layout.table_size;
        parsed.file->checksum = calculate_checksum(parsed.file->sg_table);
        view = std::move(parsed);
        return acul::make_op_success();
    }

    acul::op_result create_read_descriptor(const acul::path &path, ReadDescriptor &view)
    {
        auto mapping = acul::make_shared<acul::fs::mapped_file>();
        const auto result = mapping->open(path.str());
        if (!result.success())
        {
            close_read_descriptor(view);
            return result;
        }
        const u64 size = mapping->size();
        return create_read_descriptor_at(std::move(mapping), path, 0, size, true, view);
    }

    acul::op_result create_read_descriptor(const ReadDescriptor &parent, const BlockIterator &block,
                                           ReadDescriptor &view, u32 type_sign)
    {
        if (&parent == &view || block._source != &parent || !block._current || !parent.file || !parent.mapping ||
            block._segment_index >= parent.file->sg_table.size())
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        const auto &segment = parent.file->sg_table[block._segment_index];
        if (segment.signature != UMBF_SEGMENT_RAW || block._payload_begin > segment.size ||
            block._current.size > segment.size - block._payload_begin ||
            (type_sign && block._current.signature != type_sign))
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        const u64 offset = parent.payload_offset + segment.offset + block._payload_begin;
        ACUL_TRY(create_read_descriptor_at(parent.mapping, parent.path, offset, block._current.size, false, view));
        if (view.file->type_sign != block._current.signature || (type_sign && view.file->type_sign != type_sign))
        {
            close_read_descriptor(view);
            return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
        }
        return acul::make_op_success();
    }

    acul::op_result read_segment(const ReadDescriptor &view, u32 segment_index, acul::vector<char> &dst)
    {
        dst.clear();
        if (!view.file || !view.mapping || segment_index >= view.file->sg_table.size())
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        return read_segment(view, view.file->sg_table[segment_index], dst);
    }

    acul::op_result set_segment_table(ReadDescriptor &view, const acul::vector<Segment> &segments)
    {
        if (!view.file || !view.mapping) return acul::make_op_error(ACUL_OP_NULLPTR);
        acul::bin_stream bytes;
        for (const auto &segment : segments)
            bytes.write(segment.offset)
                .write(segment.size)
                .write(segment.block_count)
                .write(segment.signature)
                .write(segment.checksum);
        detail::Layout layout{view.file_offset, view.file_size, view.payload_offset, view.payload_size, 0u,
                              static_cast<u64>(bytes.size())};
        File parsed = *view.file;
        ACUL_TRY(detail::parse_table({bytes.data(), bytes.size()}, parsed, layout));
        view.file->sg_table = std::move(parsed.sg_table);
        view.file->checksum = calculate_checksum(view.file->sg_table);
        return acul::make_op_success();
    }

    acul::op_result read_segment(const ReadDescriptor &view, const Segment &segment, acul::vector<char> &dst)
    {
        dst.clear();
        if (!view.file || !view.mapping || segment.offset > view.payload_size ||
            segment.size > view.payload_size - segment.offset)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        const u64 size = view.mapping->size();
        if (view.payload_offset > size || segment.offset > size - view.payload_offset ||
            segment.size > size - view.payload_offset - segment.offset)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        const ByteView bytes{view.mapping->data() + view.payload_offset + segment.offset, segment.size};
        return detail::decode_segment(bytes, segment, dst);
    }

    u32 calculate_checksum(const acul::vector<Segment> &segments) noexcept
    {
        u32 checksum = 0u;
        for (const auto &segment : segments)
            checksum = acul::crc32(checksum, reinterpret_cast<const char *>(&segment.checksum),
                                   sizeof(segment.checksum));
        return checksum;
    }

    bool get_block_view(const SegmentBuffer &segment, u32 block_count, u32 block_index, PayloadView &block) noexcept
    {
        block = {};
        return read_block_view(segment, block_count, block_index, block);
    }

    acul::op_result read_block(const ReadDescriptor &view, u32 segment_index, u32 block_index, acul::vector<char> &data,
                               PayloadView &block)
    {
        block = {};
        if (!view.file) return acul::make_op_error(ACUL_OP_NULLPTR);
        if (segment_index >= view.file->sg_table.size() ||
            block_index >= view.file->sg_table[segment_index].block_count)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        ACUL_TRY(read_segment(view, segment_index, data));
        if (!read_block_view(data, view.file->sg_table[segment_index].block_count, block_index, block))
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        return acul::make_op_success();
    }

    acul::op_result read_block(const ReadDescriptor &view, u64 index, acul::vector<char> &data, PayloadView &block)
    {
        if (!view.file) return acul::make_op_error(ACUL_OP_NULLPTR);
        u32 segment_index = 0u;
        u32 block_index = 0u;
        if (!locate_block(*view.file, index, segment_index, block_index))
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        return read_block(view, segment_index, block_index, data, block);
    }

    BlockIterator::BlockIterator(const ReadDescriptor &descriptor, u64 first_block) { reset(descriptor, first_block); }

    acul::op_result BlockIterator::reset(const ReadDescriptor &descriptor, u64 first_block)
    {
        set_end();
        _result = acul::make_op_success();
        if (!descriptor.file)
        {
            _result = acul::make_op_error(ACUL_OP_NULLPTR);
            return _result;
        }
        _source = &descriptor;

        u32 local_block = 0u;
        if (!locate_block(*descriptor.file, first_block, _segment_index, local_block))
        {
            u64 count = 0u;
            for (const auto &segment : descriptor.file->sg_table) count += segment.block_count;
            if (first_block != count)
            {
                _result = acul::make_op_error(ACUL_OP_INVALID_SIZE);
                set_end();
                return _result;
            }
            _segment_index = static_cast<u32>(descriptor.file->sg_table.size());
            return _result;
        }
        const u32 requested_segment = _segment_index;
        _result = load_segment();
        if (!_source) return _result;
        if (_segment_index != requested_segment) local_block = 0;
        if (!_result.success())
        {
            set_end();
            return _result;
        }
        if (!prepare_current())
        {
            _result = acul::make_op_error(ACUL_OP_INVALID_SIZE);
            set_end();
            return _result;
        }
        for (u32 index = 0u; index < local_block; ++index)
        {
            ++(*this);
            if (!_current)
            {
                if (_result.success()) _result = acul::make_op_error(ACUL_OP_INVALID_SIZE);
                return _result;
            }
        }
        return _result;
    }

    acul::op_result BlockIterator::load_segment()
    {
        if (!_source || !_source->file || _segment_index >= _source->file->sg_table.size())
            return acul::make_op_error(ACUL_OP_OUT_OF_BOUNDS);
        const auto &segments = _source->file->sg_table;
        while (_segment_index < segments.size())
        {
            acul::vector<char> data;
            const auto result = read_segment(*_source, _segment_index, data);
            if (!result.success())
                detail::log_segment_error(_segment_index, segments[_segment_index].signature, result);
            else if (segments[_segment_index].block_count)
            {
                _stream = acul::make_shared<acul::bin_stream>(std::move(data));
                _block_index = 0;
                return acul::make_op_success();
            }
            ++_segment_index;
        }
        set_end();
        return acul::make_op_success();
    }

    bool BlockIterator::prepare_current()
    {
        _current = {};
        return _stream && detail::next_block(*_stream, _current, _payload_begin, _payload_end);
    }

    void BlockIterator::set_end() noexcept
    {
        _source = nullptr;
        _segment_index = 0u;
        _block_index = 0u;
        _stream.reset();
        _current = {};
        _payload_begin = 0u;
        _payload_end = 0u;
    }

    BlockIterator::reference BlockIterator::operator*() const noexcept { return _current; }

    BlockIterator::pointer BlockIterator::operator->() const noexcept { return &_current; }

    BlockIterator &BlockIterator::operator++()
    {
        if (!_current) return *this;
        _stream->pos(_payload_end);
        ++_block_index;

        const auto &segments = _source->file->sg_table;
        while (_segment_index < segments.size() && _block_index >= segments[_segment_index].block_count)
        {
            ++_segment_index;
            if (_segment_index >= segments.size())
            {
                set_end();
                return *this;
            }
            _result = load_segment();
            if (!_source) return *this;
            if (!_result.success())
            {
                set_end();
                return *this;
            }
        }

        if (!prepare_current())
        {
            _result = acul::make_op_error(ACUL_OP_INVALID_SIZE);
            set_end();
        }
        return *this;
    }

    BlockIterator BlockIterator::operator++(int)
    {
        BlockIterator previous = *this;
        ++(*this);
        return previous;
    }

    bool BlockIterator::operator==(const BlockIterator &other) const noexcept
    {
        if (!_current || !other._current) return !_current && !other._current;
        return _source == other._source && _segment_index == other._segment_index && _block_index == other._block_index;
    }

    bool BlockIterator::read(u32 expected_signature, Block *&block) const
    {
        block = nullptr;
        return _stream && _current.signature == expected_signature &&
               detail::read_block(*_stream, _current, _payload_begin, _payload_end, block);
    }

    acul::unique_ptr<Block> BlockIterator::read() const
    {
        Block *data = nullptr;
        if (!read(_current.signature, data)) return nullptr;
        auto block = acul::unique_ptr<Block>(data);
        if (block->signature() != _current.signature) return nullptr;
        return block;
    }

    BlockIterator ReadDescriptor::begin() const { return BlockIterator(*this); }

    BlockIterator ReadDescriptor::end() const { return {}; }

    acul::unique_ptr<Block> get_block(const BlockIterator &iterator) { return iterator.read(); }

    Library::Node *Library::get_node(const acul::path &path)
    {
        Node *current_node = &file_tree;
        for (const auto &it : path)
        {
            auto child_it = std::find_if(current_node->children.begin(), current_node->children.end(),
                                         [&it](const Node &node) { return node.name == it; });

            if (child_it != current_node->children.end()) current_node = &(*child_it);
            else
            {
                UMBF_LOG_ERROR("path not found in the umbf library: %s", path.str().c_str());
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
        UMBF_LOG_INFO("initializing registry: %s", path.str().c_str());
        auto files = acul::fs::list_files(path.str());
        const auto &lr = files.result();
        if (!lr.success())
            throw acul::runtime_error(
                acul::format("Failed to list files. Error code: 0x%016" PRIx64, static_cast<u64>(lr)));
        for (const auto &entry : files)
        {
            if (acul::fs::get_extension(entry) == ".umlib")
            {
                try
                {
                    UMBF_LOG_INFO("loading umbf library: %s", entry.c_str());
                    ReadDescriptor asset;
                    auto res = create_read_descriptor(entry, asset);
                    if (!res.success())
                    {
                        UMBF_LOG_WARN("failed to load umbf library %s. Error code: 0x%16" PRIx64, entry.c_str(),
                                      static_cast<u64>(res));
                        continue;
                    }
                    if (asset.file->type_sign != sign_block::format::library)
                    {
                        UMBF_LOG_WARN("failed to load umbf library %s. Wrong file type: 0x%x", entry.c_str(),
                                      asset.file->type_sign);
                        close_read_descriptor(asset);
                        continue;
                    }
                    acul::shared_ptr<Library> library;
                    for (auto block = asset.begin(); block != asset.end(); ++block)
                    {
                        if (block->signature != sign_block::library) continue;
                        auto value = get_block(block);
                        if (value && value->signature() == sign_block::library)
                            library = acul::make_shared<Library>(std::move(*static_cast<Library *>(value.get())));
                        break;
                    }
                    if (!library)
                    {
                        UMBF_LOG_WARN("failed to load umbf library %s. Library metadata is invalid", entry.c_str());
                        close_read_descriptor(asset);
                        continue;
                    }
                    _libraries.emplace(library->file_tree.name, library);
                    close_read_descriptor(asset);
                }
                catch (const std::exception &e)
                {
                    UMBF_LOG_WARN("failed to load umbf library. Error: %s", e.what());
                    continue;
                }
            }
        }
    }

    bool write_block_to_stream(acul::bin_stream &stream, const Block &value)
    {
        const u32 signature = value.signature();
        if (!signature || !registry::resolver)
        {
            stream.write(0ULL);
            return false;
        }
        const auto *value_stream = registry::resolver->get_block_stream(signature);
        if (!value_stream)
        {
            UMBF_LOG_WARN("no writer is registered for UMBF value 0x%08x", signature);
            stream.write(0ULL);
            return false;
        }
        acul::bin_stream payload;
        value_stream->write(payload, const_cast<Block *>(&value));
        stream.write(static_cast<u64>(sizeof(u32) * 2u + payload.size())).write(signature).write(value.tag);
        if (payload.size() != 0u) stream.write(payload.data(), payload.size());
        return true;
    }

    bool read_block_from_stream(acul::bin_stream &stream, acul::unique_ptr<Block> &value)
    {
        value = nullptr;
        if (stream.size() - stream.pos() < sizeof(u64)) return false;
        u64 block_size = 0u;
        stream.read(block_size);
        if (block_size == 0u) return true;
        if (block_size < sizeof(u32) * 2u || block_size > stream.size() - stream.pos()) return false;

        u32 signature = 0u;
        u32 tag = 0u;
        stream.read(signature).read(tag);
        const u64 payload_size = block_size - sizeof(u32) * 2u;
        const auto block_end = stream.pos() + static_cast<size_t>(payload_size);
        const auto *value_stream = registry::resolver ? registry::resolver->get_block_stream(signature) : nullptr;
        if (value_stream)
        {
            ScopedStreamSize scope(stream, block_end);
            value = acul::unique_ptr<Block>(value_stream->read(stream));
            if (value && value->signature() != signature) value = nullptr;
            if (value) value->tag = tag;
        }
        stream.pos(block_end);
        return true;
    }

    namespace registry
    {
        const SegmentCodec segment_compress_codec = {encode_compressed_segment, decode_compressed_segment};

        Resolver *resolver = nullptr;

        namespace
        {
            void write_target(acul::bin_stream &stream, umbf::Block *block)
            {
                auto *target = static_cast<Target *>(block);
                stream.write(target->header.vendor_sign)
                    .write(target->header.vendor_version)
                    .write(target->header.spec_version)
                    .write(target->header.type_sign)
                    .write(target->url)
                    .write(target->checksum);
            }

            umbf::Block *read_target(acul::bin_stream &stream)
            {
                auto *target = acul::alloc<Target>();
                stream.read(target->header.vendor_sign)
                    .read(target->header.vendor_version)
                    .read(target->header.spec_version)
                    .read(target->header.type_sign)
                    .read(target->url)
                    .read(target->checksum);
                return target;
            }

            void write_library(acul::bin_stream &stream, umbf::Block *block)
            {
                stream.write(static_cast<Library *>(block)->file_tree);
            }

            umbf::Block *read_library(acul::bin_stream &stream)
            {
                auto *library = acul::alloc<Library>();
                stream.read(library->file_tree);
                return library;
            }
        } // namespace

        const BlockStream target{read_target, write_target};
        const BlockStream library{read_library, write_library};

    } // namespace registry
} // namespace umbf

namespace acul
{
    template <>
    bin_stream &bin_stream::write(const umbf::Library::Node &node)
    {
        if (node.index == umbf::Library::Node::invalid_index)
            throw acul::runtime_error("UMBF library node index is invalid");
        if (!node.is_directory() && !node.children.empty())
            throw acul::runtime_error("UMBF library asset cannot contain children");
        write(node.name).write(node.index);
        u16 child_count = static_cast<u16>(node.children.size());
        write(child_count);
        for (const auto &child : node.children) write(child);
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(umbf::Library::Node &node)
    {
        read(node.name).read(node.index);
        if (node.index == umbf::Library::Node::invalid_index)
            throw acul::runtime_error("UMBF library node index is invalid");
        u16 child_count;
        read(child_count);
        if (!node.is_directory() && child_count != 0u)
            throw acul::runtime_error("UMBF library asset contains children");
        node.children.resize(child_count);
        for (auto &child : node.children) read(child);
        return *this;
    }

} // namespace acul
