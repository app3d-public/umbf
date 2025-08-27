#include <acul/io/file.hpp>
#include <acul/log.hpp>
#include <cassert>
#include <umbf/umbf.hpp>
#include <umbf/utils.hpp>

namespace umbf
{
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

    bool save_file(File &file, const acul::io::path &path, acul::bin_stream &src, int compression)
    {
        acul::bin_stream dst_stream;
        File::Header::Pack pack;
        pack_header(file.header, pack);
        dst_stream.write(UMBF_MAGIC).write(pack);
        if (file.header.compressed)
        {
            acul::vector<char> compressed;
            if (!acul::io::file::compress(src.data() + src.pos(), src.size() - src.pos(), compressed, compression))
            {
                LOG_ERROR("Failed to compress: %s", path.str().c_str());
                return false;
            }

            dst_stream.write(compressed.data(), compressed.size());
        }
        else
            dst_stream.write(src.data() + src.pos(), src.size() - src.pos());
        file.checksum = acul::crc32(0, src.data(), src.size());
        return acul::io::file::write_binary(path.str(), dst_stream.data(), dst_stream.size());
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
            LOG_ERROR("Asset write error: %s", e.what());
            return false;
        }
    }

    bool load_file(const acul::io::path &path, acul::bin_stream &dst, File::Header &header)
    {
        acul::vector<char> source;
        if (acul::io::file::read_binary(path.str(), source) != acul::io::file::op_state::success) return false;

        acul::bin_stream source_stream(std::move(source));
        u32 sign_file_format;
        source_stream.read(sign_file_format);
        if (sign_file_format != UMBF_MAGIC)
        {
            LOG_ERROR("Invalid file signature");
            return false;
        }
        File::Header::Pack pack;
        source_stream.read(pack);
        unpack_header(pack, header);
        if (header.compressed)
        {
            acul::vector<char> decompressed;
            if (!acul::io::file::decompress(source_stream.data() + source_stream.pos(),
                                            source_stream.size() - source_stream.pos(), decompressed))
            {
                LOG_ERROR("Failed to decompress: %s", path.str().c_str());
                return false;
            }
            dst = acul::bin_stream(std::move(decompressed));
        }
        else
            dst = std::move(source_stream);
        return true;
    }

    acul::shared_ptr<File> File::read_from_disk(const acul::string &path)
    {
        try
        {
            acul::bin_stream stream{};
            auto asset = acul::make_shared<File>();
            if (!load_file(path, stream, asset->header)) return nullptr;
            auto offset = stream.pos();
            stream.read(asset->blocks);
            if (asset->blocks.begin() == asset->blocks.end()) LOG_WARN("Meta data not found in '%s'", path.c_str());
            asset->checksum = acul::crc32(0, stream.data() + offset, stream.size() - offset);
            return asset;
        }
        catch (std::exception &e)
        {
            LOG_ERROR("%s", e.what());
            return nullptr;
        }
    }

    bool pack_atlas(size_t max_size, int discard_step, rectpack2D::flipping_option flip, std::vector<Atlas::Rect> &dst)
    {
        rectpack2D::callback_result pack_result{rectpack2D::callback_result::CONTINUE_PACKING};
        static std::function<rectpack2D::callback_result(Atlas::Rect &)> report_successfull = [](Atlas::Rect &) {
            return rectpack2D::callback_result::CONTINUE_PACKING;
        };
        static std::function<rectpack2D::callback_result(Atlas::Rect &)> report_unsuccessfull =
            [&pack_result, max_size](Atlas::Rect &) {
                pack_result = rectpack2D::callback_result::ABORT_PACKING;
                LOG_INFO("Failed to pack atlas. Max size: %zu", max_size);
                return rectpack2D::callback_result::ABORT_PACKING;
            };

        rectpack2D::find_best_packing<Atlas::Spaces>(
            dst, rectpack2D::make_finder_input(max_size, discard_step, report_successfull, report_unsuccessfull, flip));
        return pack_result != rectpack2D::callback_result::ABORT_PACKING;
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

            auto rect = atlas->pack_data[i];
            rect.x += atlas->padding;
            rect.y += atlas->padding;
            rect.w -= 2 * atlas->padding;
            rect.h -= 2 * atlas->padding;
            utils::copy_pixels_to_area(*(src[i]), *image, rect);
        }
    }

    Library::Node *Library::get_node(const acul::io::path &path)
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
                LOG_ERROR("Path not found in the library: %s", path.str().c_str());
                return nullptr;
            }
        }
        return current_node;
    }

    void Registry::init(const acul::io::path &path)
    {
        acul::vector<acul::string> files;
        if (acul::io::file::list_files(path, files) != acul::io::file::op_state::success)
            throw acul::runtime_error("Failed to get libraries list");
        for (const auto &entry : files)
        {
            if (acul::io::get_extension(entry) == ".umlib")
            {
                try
                {
                    LOG_INFO("Loading library: %s", entry.c_str());
                    auto asset = File::read_from_disk(entry);
                    if (!asset || asset->header.type_sign != sign_block::format::library)
                    {
                        LOG_WARN("Failed to load library %s", entry.c_str());
                        continue;
                    }
                    auto library = acul::static_pointer_cast<Library>(asset->blocks.front());
                    _libraries.emplace(library->file_tree.name, library);
                    asset->blocks.clear();
                }
                catch (...)
                {
                    LOG_WARN("Failed to load library %s", entry.c_str());
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
            auto *meta_stream = umbf::streams::resolver->get_stream(header.signature);
            if (meta_stream)
            {
                acul::shared_ptr<umbf::Block> block(meta_stream->read(*this));
                if (block)
                    meta.push_back(block);
                else
                    LOG_WARN("Failed to read meta block: 0x%08x", header.signature);
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
                    throw acul::runtime_error("Asset is invalid. Possible corrupted file structure");
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
                throw acul::runtime_error("UMBF file is invalid. Possible corrupted file structure");
        }
        return *this;
    }

} // namespace acul