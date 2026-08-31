#pragma once

#include <acul/hash/hashmap.hpp>
#include <acul/hash/utils.hpp>
#include <acul/io/fs/file.hpp>
#include <acul/io/path.hpp>
#include <acul/list.hpp>
#include <acul/memory/smart_ptr.hpp>
#include <acul/op_result.hpp>
#include <acul/string/utils.hpp>
#include <amal/bin_stream.hpp>
#include <amal/rect.hpp>
#include <iterator>
#include <umbf/symbol_export.h>

#define UMBF_MAGIC     0xCA9FB393
#define UMBF_VENDOR_ID 0xBC037D

#define UMBF_SEGMENT_RAW              0u
#define UMBF_SEGMENT_COMPRESSED       0x4454535Au
#define UMBF_SEGMENT_COMPRESSED_IMAGE 0x474D495Au

namespace umbf
{
    struct ReadDescriptor;
    class BlockIterator;

    struct Segment
    {
        u64 offset = 0;
        u64 size = 0;
        u32 block_count = 0;
        u32 signature = UMBF_SEGMENT_RAW;
    };

    struct PayloadView
    {
        u32 signature = 0;
        u32 tag = 0;
        const char *data = nullptr;
        u64 size = 0;

        explicit operator bool() const noexcept { return signature != 0u; }
    };

    using SegmentBuffer = acul::vector<char>;

    struct Block
    {
        u32 tag = 0;
        virtual ~Block() = default;
        virtual u32 signature() const noexcept = 0;
    };

    struct Header
    {
        u32 vendor_sign = 0;
        u32 vendor_version = 0;
        u32 spec_version = 0;
        u32 type_sign = 0;
    };

    /**
     * @brief Represents a generic asset in the system.
     *
     * Persistent UMBF metadata. Segment bytes are deliberately not owned by File.
     */
    struct File : Header
    {
        acul::vector<Segment> sg_table;
        u32 checksum = 0; // CRC32C of decoded payload, stored in the file footer.
    };

    struct SegmentWriter
    {
        u32 signature = UMBF_SEGMENT_RAW;
        u32 block_count = 0u;
    };

    namespace sign_block
    {
        namespace format
        {
            enum : u32
            {
                none = 0x0,
                target = 0xC7DCA0DA,
                library = 0x3500BDEA,
                raw = 0xA031000C
            };
        } // namespace format

        enum : u32
        {
            raw = 0xF82E95C8,
            target = 0x0491F4E9,
            library = 0x8D7824FA
        };
    } // namespace sign_block

    /**
     * Structure representing a Target asset.
     * The Target class represents a type of asset that doesn't store the resource itself but only
     * information about where the resource is located and its location in the cache.
     */
    struct Target final : Block
    {
        acul::string url; // URL of the target resource
        Header header;    // Metadata of the destination resource, without physical layout.
        u32 checksum = 0; // Expected checksum of the target resource.

        u32 signature() const noexcept override { return sign_block::target; }

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
    };

    // The Library class serves as a storage for other assets. These assets can either be embedded or act as
    // targets.
    struct Library final : Block
    {
        // Represents a node in the file structure of the asset library.
        struct Node
        {
            static constexpr u64 invalid_index = ~0ULL;
            static constexpr u64 directory_index = ~0ULL - 1u;

            acul::string name;           // Name of the file node.
            acul::vector<Node> children; // Child nodes of this file node.
            u64 index = invalid_index;

            bool is_directory() const noexcept { return index == directory_index; }
        } file_tree;

        u32 signature() const noexcept override { return sign_block::library; }

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */

        /**
         * @brief Retrieves a list of assets associated with a specified path.
         * @param path Filesystem path to query for assets.
         * @return File search result
         */
        UMBF_EXPORT Node *get_node(const acul::path &path);
        UMBF_EXPORT const Node *get_node(const acul::path &path) const;
    };

    struct FileDescriptorBase
    {
        acul::path path;
        acul::shared_ptr<File> file;
        u64 file_offset = 0;
        u64 file_size = 0;
        u64 payload_offset = 0;
    };

    struct ReadDescriptor : FileDescriptorBase
    {
        acul::shared_ptr<acul::fs::mapped_file> mapping;

        UMBF_EXPORT BlockIterator begin() const;
        UMBF_EXPORT BlockIterator end() const;
    };

    struct WriteDescriptor : FileDescriptorBase
    {
        WriteDescriptor() = default;
        UMBF_EXPORT ~WriteDescriptor() noexcept;
        WriteDescriptor(const WriteDescriptor &) = delete;
        WriteDescriptor &operator=(const WriteDescriptor &) = delete;
        UMBF_EXPORT WriteDescriptor(WriteDescriptor &&other) noexcept;
        UMBF_EXPORT WriteDescriptor &operator=(WriteDescriptor &&other) noexcept;

        FILE *fd = nullptr;
        u32 default_segment_signature = UMBF_SEGMENT_RAW;
        int compression = 5;
        bool finalized = false;
        bool has_active_segment = false;
        u64 active_segment_offset = 0u;
        SegmentWriter active_segment;
        SegmentBuffer active_segment_data;

    private:
        void swap(WriteDescriptor &other) noexcept;
    };

    UMBF_EXPORT acul::op_result create_write_descriptor(const Header &header, WriteDescriptor &descriptor,
                                                        int compression = 5);
    UMBF_EXPORT acul::op_result create_write_descriptor(const acul::path &path, const Header &header,
                                                        WriteDescriptor &descriptor, int compression = 5);
    UMBF_EXPORT SegmentWriter *add_segment(WriteDescriptor &file, u32 signature = UMBF_SEGMENT_RAW);
    UMBF_EXPORT u32 add_payload(WriteDescriptor &file, u32 signature, const void *data, u64 size,
                                SegmentWriter *segment = nullptr, u32 tag = 0u);
    UMBF_EXPORT u32 add_file(WriteDescriptor &file, WriteDescriptor &nested, SegmentWriter *segment = nullptr, u32 tag = 0u);
    UMBF_EXPORT bool finalize_file(WriteDescriptor &descriptor);
    UMBF_EXPORT bool save_file(WriteDescriptor &descriptor, const acul::string &path);

    UMBF_EXPORT acul::op_result create_read_descriptor(const ReadDescriptor &parent, const BlockIterator &block,
                                                       ReadDescriptor &descriptor, u32 type_sign = 0u);

    /**
     * Single-pass iterator over the global block order. Segment boundaries are handled internally and remain a
     * physical storage detail. A PayloadView remains valid until the iterator moves to another segment.
     */
    class BlockIterator
    {
    public:
        using value_type = PayloadView;
        using difference_type = std::ptrdiff_t;
        using pointer = const PayloadView *;
        using reference = const PayloadView &;
        using iterator_category = std::input_iterator_tag;

        BlockIterator() = default;
        UMBF_EXPORT explicit BlockIterator(const ReadDescriptor &descriptor, u64 first_block = 0u);

        UMBF_EXPORT acul::op_result reset(const ReadDescriptor &descriptor, u64 first_block = 0u);
        UMBF_EXPORT reference operator*() const noexcept;
        UMBF_EXPORT pointer operator->() const noexcept;
        UMBF_EXPORT BlockIterator &operator++();
        UMBF_EXPORT BlockIterator operator++(int);
        UMBF_EXPORT bool operator==(const BlockIterator &other) const noexcept;
        bool operator!=(const BlockIterator &other) const noexcept { return !(*this == other); }
        explicit operator bool() const noexcept { return static_cast<bool>(_current); }
        bool failed() const noexcept { return !_result.success(); }
        const acul::op_result &result() const noexcept { return _result; }

        UMBF_EXPORT bool read(u32 expected_signature, void *&block) const;
        UMBF_EXPORT acul::shared_ptr<Block> read() const;

    private:
        friend UMBF_EXPORT acul::op_result create_read_descriptor(const ReadDescriptor &, const BlockIterator &,
                                                                  ReadDescriptor &, u32);

        UMBF_EXPORT acul::op_result load_segment();
        UMBF_EXPORT bool prepare_current();
        UMBF_EXPORT void set_end() noexcept;

        const ReadDescriptor *_source = nullptr;
        u32 _segment_index = 0u;
        u32 _block_index = 0u;
        acul::shared_ptr<acul::bin_stream> _stream;
        PayloadView _current;
        size_t _payload_begin = 0u;
        size_t _payload_end = 0u;
        acul::op_result _result = acul::make_op_success();
    };

    UMBF_EXPORT acul::op_result create_read_descriptor(const acul::path &path, ReadDescriptor &descriptor);
    UMBF_EXPORT acul::op_result read_segment(const ReadDescriptor &descriptor, u32 segment_index,
                                             acul::vector<char> &dst);
    UMBF_EXPORT bool get_block_view(const SegmentBuffer &segment, u32 block_count, u32 block_index,
                                    PayloadView &block) noexcept;
    UMBF_EXPORT acul::op_result read_block(const ReadDescriptor &descriptor, u32 segment_index, u32 block_index,
                                           acul::vector<char> &segment_data, PayloadView &block);
    UMBF_EXPORT acul::op_result read_block(const ReadDescriptor &descriptor, u64 index, acul::vector<char> &segment_data,
                                           PayloadView &block);
    UMBF_EXPORT void close_map_descriptor(ReadDescriptor &descriptor) noexcept;
    UMBF_EXPORT void close_map_descriptor(WriteDescriptor &descriptor) noexcept;
    UMBF_EXPORT acul::shared_ptr<Block> get_block(const BlockIterator &iterator);

    /**
     * Class responsible for managing asset libraries.
     *
     * The Manager class oversees and provides access to various asset libraries, allowing for efficient
     * organization and retrieval of libraries by their names. It provides iterator functionality for easy
     * traversal of the libraries and ensures streamlined integration with the underlying system through
     * its initialization method.
     */
    class Registry
    {
    public:
        using iterator = acul::hashmap<acul::string, acul::shared_ptr<Library>>::iterator;
        using const_iterator = acul::hashmap<acul::string, acul::shared_ptr<Library>>::const_iterator;

        /**
         * @brief Get the asset libraries size
         */
        size_t size() const { return _libraries.size(); }

        acul::shared_ptr<Library> operator[](const acul::string &name)
        {
            auto it = _libraries.find(name);
            if (it == _libraries.end()) return nullptr;
            return it->second;
        }

        acul::shared_ptr<Library> operator[](const acul::string &name) const
        {
            auto it = _libraries.find(name);
            if (it == _libraries.end()) return nullptr;
            return it->second;
        }

        iterator begin() { return _libraries.begin(); }
        const_iterator begin() const { return _libraries.begin(); }
        const_iterator cbegin() const { return _libraries.cbegin(); }
        iterator end() { return _libraries.end(); }
        const_iterator end() const { return _libraries.end(); }
        const_iterator cend() const { return _libraries.cend(); }

        UMBF_EXPORT void init(const acul::path &path);

    private:
        acul::hashmap<acul::string, acul::shared_ptr<Library>> _libraries;
    };

    namespace registry
    {
        struct BlockStream
        {
            void *(*read)(acul::bin_stream &stream);
            void (*write)(acul::bin_stream &stream, void *block);
        };

        struct SegmentCodec
        {
            acul::op_result (*encode)(const SegmentBuffer &sg_buffer, u32 block_count, int compression,
                                      acul::vector<char> &dst);
            acul::op_result (*decode)(const char *data, u64 size, u32 block_count, acul::vector<char> &dst);
        };

        class Resolver
        {
        public:
            virtual const BlockStream *get_block_stream(u32 signature) = 0;
            virtual const SegmentCodec *get_segment_codec(u32 signature) = 0;
        };

        class HashResolver final : public Resolver
        {
        public:
            acul::hashmap<u32, const BlockStream *> block_streams;
            acul::hashmap<u32, const SegmentCodec *> segment_codecs;

            virtual const BlockStream *get_block_stream(u32 signature) override
            {
                auto it = block_streams.find(signature);
                return it == block_streams.end() ? nullptr : it->second;
            }

            virtual const SegmentCodec *get_segment_codec(u32 signature) override
            {
                auto it = segment_codecs.find(signature);
                return it == segment_codecs.end() ? nullptr : it->second;
            }
        };

        extern UMBF_EXPORT Resolver *resolver;
        extern UMBF_EXPORT const BlockStream target;
        extern UMBF_EXPORT const BlockStream library;
        extern UMBF_EXPORT const SegmentCodec segment_compress_codec;
    } // namespace registry

    inline void insert_default_block_streams(registry::HashResolver &resolver)
    {
        resolver.block_streams[sign_block::target] = &registry::target;
        resolver.block_streams[sign_block::library] = &registry::library;
    }

    inline void insert_default_segment_codecs(registry::HashResolver &resolver)
    {
        resolver.segment_codecs[UMBF_SEGMENT_COMPRESSED] = &registry::segment_compress_codec;
    }

    UMBF_EXPORT bool write_block_to_stream(acul::bin_stream &stream, const Block &block);
    UMBF_EXPORT bool read_block_from_stream(acul::bin_stream &stream, acul::shared_ptr<Block> &block);

    inline u32 add_raw_block(WriteDescriptor &file, const void *data, u64 size, SegmentWriter *segment = nullptr,
                             u32 tag = 0u)
    {
        return add_payload(file, sign_block::raw, data, size, segment, tag);
    }

    UMBF_EXPORT u32 add_block(WriteDescriptor &file, Block *block, SegmentWriter *segment = nullptr);
} // namespace umbf

namespace acul
{
    template <>
    bin_stream &bin_stream::write(const umbf::Library::Node &node);

    template <>
    bin_stream &bin_stream::read(umbf::Library::Node &node);
} // namespace acul
