#pragma once
#include <cinttypes>
#include <umbf/umbf.hpp>
#include "log.hpp"

namespace umbf::detail
{
    constexpr u64 block_header_size = sizeof(u32) * 2 + sizeof(u64);
    constexpr u64 footer_size = sizeof(u64) + sizeof(u32);
    constexpr u64 table_entry_size = sizeof(u64) * 2 + sizeof(u32) * 2;
    struct Layout
    {
        u64 file_offset = 0, file_size = 0, payload_offset = 0, payload_size = 0;
        u64 table_offset = 0, table_size = 0;
    };
    acul::op_result parse_header(ByteView bytes, bool magic, File &file);
    acul::op_result parse_footer(ByteView bytes, u64 offset, u64 size, bool magic, File &file, Layout &layout);
    acul::op_result parse_table(ByteView bytes, File &file, const Layout &layout);
    acul::op_result validate_segment(ByteView bytes, u32 block_count);
    acul::op_result decode_segment(ByteView bytes, const Segment &segment, acul::vector<char> &out);
    inline void log_segment_error(u32 i, u32 s, acul::op_result r)
    {
        UMBF_LOG_ERROR("Skipping UMBF segment %u (codec 0x%08x): error 0x%016" PRIx64, i, s, static_cast<u64>(r));
    }
    bool next_block(acul::bin_stream &stream, PayloadView &view, size_t &begin, size_t &end);
    bool read_block(acul::bin_stream &stream, const PayloadView &view, size_t begin, size_t end, void *&out);
} // namespace umbf::detail
