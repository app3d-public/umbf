#include <umbf/async.hpp>
#include "read.hpp"

namespace umbf::async
{
    acul::op_result get_segment_table_range(ByteView footer, FileRange file, FileRange &table, bool magic)
    {
        table = {};
        File metadata;
        detail::Layout layout;
        ACUL_TRY(detail::parse_footer(footer, file.offset, file.size, magic, metadata, layout));
        table = {layout.table_offset, layout.table_size};
        return acul::make_op_success();
    }

    acul::op_result create_read_descriptor(ByteView header, ByteView footer, ByteView table, FileRange file,
                                           ReadDescriptor &out, bool magic)
    {
        ReadDescriptor descriptor;
        detail::Layout layout;
        ACUL_TRY(detail::parse_header(header, magic, descriptor.file));
        ACUL_TRY(detail::parse_footer(footer, file.offset, file.size, magic, descriptor.file, layout));
        ACUL_TRY(detail::parse_table(table, descriptor.file, layout));
        descriptor.file_offset = file.offset;
        descriptor.file_size = file.size;
        descriptor.payload_offset = layout.payload_offset;
        out = std::move(descriptor);
        return acul::make_op_success();
    }

    BlockIterator::BlockIterator(std::shared_ptr<acul::bin_stream> stream, u32 count, u64 first, u64 offset)
        : _stream(std::move(stream)), _remaining(count), _index(first), _segment_offset(offset)
    {
        if (_remaining) prepare(0);
    }

    bool BlockIterator::prepare(size_t offset)
    {
        _stream->pos(offset);
        return detail::next_block(*_stream, _current, _begin, _end);
    }

    BlockIterator &BlockIterator::operator++()
    {
        if (!_current) return *this;
        ++_index;
        if (--_remaining) prepare(_end);
        else
        {
            _current = {};
            _stream.reset();
        }
        return *this;
    }

    acul::shared_ptr<Block> BlockIterator::read() const
    {
        void *data = nullptr;
        if (!_current || !_stream || !detail::read_block(*_stream, _current, _begin, _end, data)) return nullptr;
        acul::shared_ptr<Block> block(static_cast<Block *>(data));
        return block->signature() == _current.signature ? block : nullptr;
    }

    acul::shared_ptr<Block> get_block(const BlockIterator &iterator) { return iterator.read(); }

    FileRange BlockChunkDescriptor::range() const noexcept
    {
        if (!*this) return {};
        const auto &segment = _source->file.sg_table[_segment];
        return {_source->payload_offset + segment.offset, segment.size};
    }

    acul::op_result BlockChunkDescriptor::append(ByteView chunk)
    {
        if (!*this || _attempted || (chunk.size && !chunk.data) || chunk.size > range().size - received())
            return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        try
        {
            if (chunk.size) _encoded.insert(_encoded.end(), chunk.data, chunk.data + chunk.size);
            _received += chunk.size;
            return acul::make_op_success();
        }
        catch (...)
        {
            return acul::make_op_error(ACUL_OP_ERROR_GENERIC);
        }
    }

    acul::op_result BlockChunkDescriptor::decode()
    {
        if (_attempted) return _result;
        if (!complete()) return acul::make_op_error(ACUL_OP_INVALID_SIZE);
        _attempted = true;
        const auto &segment = _source->file.sg_table[_segment];
        acul::vector<char> decoded;
        if (segment.signature == UMBF_SEGMENT_RAW)
        {
            _result = detail::validate_segment({_encoded.data(), _encoded.size()}, segment.block_count);
            if (_result.success()) decoded = std::move(_encoded);
        }
        else _result = detail::decode_segment({_encoded.data(), _encoded.size()}, segment, decoded);
        _encoded = acul::vector<char>{};
        if (_result.success())
        {
            try
            {
                _decoded = std::make_shared<acul::bin_stream>(std::move(decoded));
            }
            catch (...)
            {
                _result = acul::make_op_error(ACUL_OP_ERROR_GENERIC);
            }
        }
        if (!_result.success()) detail::log_segment_error(_segment, segment.signature, _result);
        return _result;
    }

    BlockChunkDescriptor &BlockChunkDescriptor::operator++()
    {
        if (!*this) return *this;
        _first_block += _source->file.sg_table[_segment].block_count;
        ++_segment;
        _received = 0;
        _encoded = acul::vector<char>{};
        _decoded.reset();
        _attempted = false;
        _result = acul::make_op_success();
        return *this;
    }

    BlockIterator BlockChunkDescriptor::begin() const
    {
        if (!*this || !_decoded) return {};
        const auto &segment = _source->file.sg_table[_segment];
        return BlockIterator(_decoded, segment.block_count, _first_block,
                             segment.signature == UMBF_SEGMENT_RAW ? range().offset : UINT64_MAX);
    }
} // namespace umbf::async
