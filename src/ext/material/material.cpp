#include <umbf/ext/material/material.hpp>

namespace umbf::registry
{
    namespace
    {
        void write_material(acul::bin_stream &stream, void *block)
        {
            stream.write(static_cast<Material *>(block)->albedo);
        }

        void *read_material(acul::bin_stream &stream)
        {
            auto *material = acul::alloc<Material>();
            stream.read(material->albedo);
            return material;
        }

        void write_material_info(acul::bin_stream &stream, void *block)
        {
            auto *material = static_cast<MaterialBinding *>(block);
            stream.write(material->id).write(material->name).write(static_cast<u32>(material->assignments.size()));
            if (!material->assignments.empty())
                stream.write(material->assignments.data(), material->assignments.size());
        }

        void *read_material_info(acul::bin_stream &stream)
        {
            auto *block = acul::alloc<MaterialBinding>();
            u32 count = 0u;
            stream.read(block->id).read(block->name).read(count);
            block->assignments.resize(count);
            if (count != 0u) stream.read(block->assignments.data(), count);
            return block;
        }

        void write_material_range(acul::bin_stream &stream, void *block)
        {
            auto *range = static_cast<MaterialRange *>(block);
            stream.write(range->mat_id).write(static_cast<u32>(range->faces.size()));
            if (!range->faces.empty()) stream.write(range->faces.data(), range->faces.size());
        }

        void *read_material_range(acul::bin_stream &stream)
        {
            auto *block = acul::alloc<MaterialRange>();
            u32 count = 0u;
            stream.read(block->mat_id).read(count);
            block->faces.resize(count);
            if (count != 0u) stream.read(block->faces.data(), count);
            return block;
        }
    } // namespace

    const BlockStream material{read_material, write_material};
    const BlockStream material_info{read_material_info, write_material_info};
    const BlockStream material_range{read_material_range, write_material_range};
} // namespace umbf::registry

namespace acul
{
    template <>
    bin_stream &bin_stream::read(umbf::MaterialNode &dst)
    {
        u16 data = 0u;
        read(dst.rgb).read(data);
        dst.textured = data >> 15u;
        dst.texture_id = dst.textured ? static_cast<i16>(data & 0x7FFFu) : -1;
        return *this;
    }
} // namespace acul

namespace umbf
{
    void insert_material_streams(registry::HashResolver &resolver)
    {
        resolver.block_streams[sign_block::material] = &registry::material;
        resolver.block_streams[sign_block::material_info] = &registry::material_info;
        resolver.block_streams[sign_block::material_range] = &registry::material_range;
    }
} // namespace umbf
