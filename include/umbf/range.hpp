#pragma once
#include <memory>
#include "umbf.hpp"

namespace umbf::range
{
    struct FileRange
    {
        u64 offset = 0, size = 0;
    };

    // Metadata only: no transport, mapping or encoded segment storage.
    struct ReadDescriptor
    {
        File file;
        u64 file_offset = 0, file_size = 0, payload_offset = 0, payload_size = 0, table_size = 0;
    };

    inline constexpr u64 metadata_size(bool magic = true) noexcept
    {
        return (magic ? sizeof(u32) : 0u) + sizeof(Header) + sizeof(u64) * 2u;
    }

    // Bootstrap: metadata is the contiguous magic/header/layout prefix.
    UMBF_EXPORT acul::op_result get_segment_table_range(ByteView metadata, FileRange file, FileRange &table,
                                                        bool magic = true);
    // All metadata is already available. The caller owns scheduling and transport.
    UMBF_EXPORT acul::op_result create_read_descriptor(ByteView metadata, ByteView table, FileRange file,
                                                       ReadDescriptor &out, bool magic = true);

    // Iterates only decoded bytes, never fetches data. Copies retain segment storage.
    // Like bin_stream, a shared decoded segment must not be read concurrently.
    class BlockIterator
    {
        friend class BlockChunkDescriptor;
        std::shared_ptr<acul::bin_stream> _stream;
        PayloadView _current;
        size_t _begin = 0, _end = 0;
        u32 _remaining = 0;
        u64 _index = 0, _segment_offset = UINT64_MAX;
        UMBF_EXPORT BlockIterator(std::shared_ptr<acul::bin_stream> stream, u32 count, u64 first, u64 offset);
        bool prepare(size_t offset);

    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = PayloadView;
        using difference_type = ptrdiff_t;
        using pointer = const PayloadView *;
        using reference = const PayloadView &;
        BlockIterator() = default;
        explicit operator bool() const noexcept { return static_cast<bool>(_current); }
        reference operator*() const noexcept { return _current; }
        pointer operator->() const noexcept { return &_current; }
        u64 index() const noexcept { return _index; }
        u64 file_offset() const noexcept
        {
            return _segment_offset == UINT64_MAX ? UINT64_MAX : _segment_offset + _begin;
        }
        UMBF_EXPORT BlockIterator &operator++();
        BlockIterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }
        bool operator==(const BlockIterator &other) const noexcept
        {
            if (!_current || !other._current) return !_current && !other._current;
            return _stream == other._stream && _begin == other._begin;
        }
        UMBF_EXPORT acul::unique_ptr<Block> read() const;
    };
    UMBF_EXPORT acul::unique_ptr<Block> get_block(const BlockIterator &iterator);

    // Push reader. Keep descriptor alive and unchanged until this cursor is released.
    // Deliver chunks sequentially from any transport, on one execution context.
    // ++ releases this segment and selects the next range; it never performs I/O.
    class BlockChunkDescriptor
    {
        const ReadDescriptor *_source = nullptr;
        u32 _segment = 0;
        u64 _first_block = 0, _received = 0;
        acul::vector<char> _encoded;
        std::shared_ptr<acul::bin_stream> _decoded;
        bool _attempted = false;
        acul::op_result _result = acul::make_op_success();

    public:
        explicit BlockChunkDescriptor(const ReadDescriptor &source) : _source(&source) {}
        BlockChunkDescriptor(const BlockChunkDescriptor &) = delete;
        BlockChunkDescriptor &operator=(const BlockChunkDescriptor &) = delete;
        explicit operator bool() const noexcept { return _segment < _source->file.sg_table.size(); }
        u32 segment_index() const noexcept { return _segment; }
        u64 first_block_index() const noexcept { return _first_block; }
        UMBF_EXPORT FileRange range() const noexcept;
        u64 received() const noexcept { return _received; }
        bool complete() const noexcept { return *this && received() == range().size; }
        UMBF_EXPORT acul::op_result append(ByteView chunk);
        // Decode once after completion. On error logs the segment; ++ skips it.
        UMBF_EXPORT acul::op_result decode();
        UMBF_EXPORT BlockChunkDescriptor &operator++();
        UMBF_EXPORT BlockIterator begin() const;
        BlockIterator end() const { return {}; }
    };
} // namespace umbf::range
