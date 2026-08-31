#pragma once

#include <umbf/umbf.hpp>
#include <utility>

namespace umbf
{
    namespace sign_block
    {
        namespace format
        {
            enum : u32
            {
                material = 0x7A76D019,
                materials_list = 0x7D70D2A8,
            };
        }

        enum : u32
        {
            material = 0xA8D0C51E,
            material_range = 0xC441E54D,
            material_info = 0x6112A229,
        };
    } // namespace sign_block

    struct MaterialNode
    {
        amal::vec3 rgb;
        bool textured = false;
        i16 texture_id = -1;
    };

    /** Material properties only. Embedded textures live in a following images-list file. */
    struct Material final : Block
    {
        MaterialNode albedo;

        u32 signature() const noexcept override { return sign_block::material; }
    };

    struct MaterialBinding : Block
    {
        u64 id = 0u;
        acul::string name;
        acul::vector<u64> assignments;

        MaterialBinding() = default;
        MaterialBinding(u64 id, const acul::string &name, acul::vector<u64> assignments = {})
            : id(id), name(name), assignments(std::move(assignments))
        {
        }

        u32 signature() const noexcept override { return sign_block::material_info; }
    };

    struct MaterialRange : Block
    {
        u64 mat_id;
        acul::vector<u32> faces;

        u32 signature() const noexcept override { return sign_block::material_range; }
    };

    namespace registry
    {
        extern UMBF_EXPORT const BlockStream material;
        extern UMBF_EXPORT const BlockStream material_info;
        extern UMBF_EXPORT const BlockStream material_range;
    } // namespace registry

    UMBF_EXPORT void insert_material_streams(registry::HashResolver &resolver);
} // namespace umbf

namespace acul
{
    template <>
    inline bin_stream &bin_stream::write(const umbf::MaterialNode &src)
    {
        const u16 data = src.textured ? ((1u << 15u) | (static_cast<u16>(src.texture_id) & 0x7FFFu)) : 0u;
        return write(src.rgb).write(data);
    }

    template <>
    bin_stream &bin_stream::read(umbf::MaterialNode &dst);
} // namespace acul
