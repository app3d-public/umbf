#include "read.hpp"

namespace umbf::detail
{
    acul::op_result parse_header(ByteView bytes, bool magic, File &file)
    {
        if (!bytes.data || bytes.size != (magic ? 20u : 16u)) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        try
        {
            acul::bin_stream stream(bytes.data, static_cast<size_t>(bytes.size));
            u32 sign = UMBF_MAGIC;
            if (magic) stream.read(sign);
            stream.read(file.vendor_sign).read(file.vendor_version).read(file.spec_version).read(file.type_sign);
            if (sign != UMBF_MAGIC) return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            return acul::make_op_success();
        }
        catch (...)
        {
            return acul::make_op_error(ACUL_OP_READ_ERROR);
        }
    }
    acul::op_result parse_layout(ByteView bytes, u64 offset, u64 size, bool magic, Layout &layout)
    {
        const u64 header_size = magic ? 20u : 16u;
        if (!bytes.data || bytes.size != layout_header_size || size < header_size + layout_header_size ||
            offset > UINT64_MAX - size)
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        try
        {
            acul::bin_stream stream(bytes.data, layout_header_size);
            u64 table = 0, payload = 0;
            stream.read(table).read(payload);
            if (table == UMBF_INVALID_SIZE || payload == UMBF_INVALID_SIZE ||
                payload > size - header_size - layout_header_size ||
                table > size - header_size - layout_header_size - payload || table % table_entry_size)
                return acul::make_op_error(ACUL_OP_INVALID_SIZE);
            const u64 committed_size = header_size + layout_header_size + payload + table;
            // A table-less payload is append-only. A crash may leave uncommitted bytes after payload_size; they are
            // outside the logical file and intentionally ignored. Embedded-table files remain exact.
            if (table != 0u && committed_size != size) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
            const u64 payload_offset = offset + header_size + layout_header_size;
            layout = {offset, committed_size, payload_offset, payload, payload_offset + payload, table};
            return acul::make_op_success();
        }
        catch (...)
        {
            return acul::make_op_error(ACUL_OP_READ_ERROR);
        }
    }
    acul::op_result parse_table(ByteView bytes, File &file, const Layout &layout)
    {
        if (bytes.size != layout.table_size || (bytes.size && !bytes.data))
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        try
        {
            acul::bin_stream stream(bytes.data, static_cast<size_t>(bytes.size));
            file.sg_table.resize(static_cast<size_t>(bytes.size / table_entry_size));
            u64 previous_end = 0;
            for (auto &s : file.sg_table)
            {
                stream.read(s.offset).read(s.size).read(s.block_count).read(s.signature).read(s.checksum);
                if (s.offset < previous_end || s.offset > layout.payload_size ||
                    s.size > layout.payload_size - s.offset)
                    return acul::make_op_error(ACUL_OP_INVALID_SIZE);
                previous_end = s.offset + s.size;
            }
            return acul::make_op_success();
        }
        catch (...)
        {
            return acul::make_op_error(ACUL_OP_READ_ERROR);
        }
    }
    bool next_block(acul::bin_stream &stream, PayloadView &view, size_t &begin, size_t &end)
    {
        view = {};
        try
        {
            u32 sign = 0, tag = 0;
            u64 size = 0;
            stream.read(sign).read(tag).read(size);
            if ((!sign && (tag != 0u || size != 0u)) || stream.pos() > stream.size() ||
                size > stream.size() - stream.pos())
                return false;
            begin = stream.pos();
            end = begin + static_cast<size_t>(size);
            view = {sign, tag, stream.data() + begin, size};
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    acul::op_result decode_segment(ByteView bytes, const Segment &s, acul::vector<char> &out)
    {
        out.clear();
        if (bytes.size != s.size || (bytes.size && !bytes.data)) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        try
        {
            if (s.signature == UMBF_SEGMENT_RAW)
            {
                if (bytes.size) out.assign(bytes.data, bytes.data + static_cast<size_t>(bytes.size));
            }
            else
            {
                const auto *codec = registry::resolver ? registry::resolver->get_segment_codec(s.signature) : nullptr;
                if (!codec) return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
                const auto r = codec->decode(bytes.data, bytes.size, s.block_count, out);
                if (!r.success())
                {
                    out.clear();
                    return r;
                }
            }
            const auto result = validate_segment({out.data(), out.size()}, s.block_count);
            if (!result.success())
            {
                out.clear();
                return result;
            }
            if (acul::crc32(0u, out.data(), out.size()) != s.checksum)
            {
                out.clear();
                return acul::make_op_error(ACUL_OP_CHECKSUM_ERROR);
            }
            return acul::make_op_success();
        }
        catch (...)
        {
            out.clear();
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        }
    }
    acul::op_result validate_segment(ByteView bytes, u32 block_count)
    {
        if (bytes.size && !bytes.data) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        try
        {
            // Validate using the same header parser without copying the entire decoded segment.
            u64 offset = 0;
            for (u32 i = 0; i < block_count; ++i)
            {
                if (offset > bytes.size || block_header_size > bytes.size - offset)
                    throw acul::runtime_error("Truncated block header");
                acul::bin_stream header(bytes.data + static_cast<size_t>(offset), block_header_size);
                u32 sign, tag;
                u64 size;
                header.read(sign).read(tag).read(size);
                offset += block_header_size;
                if ((!sign && (tag != 0u || size != 0u)) || size > bytes.size - offset)
                    throw acul::runtime_error("Invalid block size");
                offset += size;
            }
            if (offset != bytes.size) throw acul::runtime_error("Invalid segment size");
            return acul::make_op_success();
        }
        catch (...)
        {
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        }
    }
    bool read_block(acul::bin_stream &stream, const PayloadView &view, size_t begin, size_t end, Block *&out)
    {
        out = nullptr;
        const auto *reader = registry::resolver ? registry::resolver->get_block_stream(view.signature) : nullptr;
        if (!reader || begin > end || end > stream.size()) return false;
        struct Restore
        {
            acul::bin_stream &stream;
            size_t pos, size;
            ~Restore()
            {
                stream.size(size);
                stream.pos(pos);
            }
        } restore{stream, stream.pos(), stream.size()};
        try
        {
            stream.pos(begin);
            stream.size(end);
            out = reader->read(stream);
            if (out) out->tag = view.tag;
            return out != nullptr;
        }
        catch (const std::exception &e)
        {
            UMBF_LOG_ERROR("failed to deserialize UMBF block 0x%08x: %s", view.signature, e.what());
            return false;
        }
        catch (...)
        {
            UMBF_LOG_ERROR("failed to deserialize UMBF block 0x%08x", view.signature);
            return false;
        }
    }
} // namespace umbf::detail
