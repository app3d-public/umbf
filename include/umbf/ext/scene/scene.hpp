#pragma once

#include <umbf/umbf.hpp>

namespace umbf
{
    namespace sign_block
    {
        namespace format
        {
            enum : u32
            {
                scene = 0xB5F1EEB1,
                scene_object = 0x7EA13B42,
                scene_objects_list = 0xB7A3EE80,
            };
        }

        enum : u32
        {
            object_info = 0x7EA13B42,
            mesh = 0xF224B521
        };
    } // namespace sign_block

    struct ObjectInfo : Block
    {
        u64 id = 0u;
        u64 parent_id = 0u;
        acul::string name;

        ObjectInfo() = default;
        ObjectInfo(u64 id, const acul::string &name, u64 parent_id = 0u) : id(id), parent_id(parent_id), name(name) {}

        u32 signature() const noexcept override { return sign_block::object_info; }
    };

    namespace mesh
    {
        /// Representation of a unique vertex per vertex attributes - Position, UV coordinates, Normals, etc.
        /// Indexed structure for rendering purpose
        struct Vertex
        {
            amal::vec3 pos{0.0f, 0.0f, 0.0f};    // Position
            amal::vec2 uv{0.0f, 0.0f};           // UV
            amal::vec3 normal{0.0f, 0.0f, 0.0f}; // Normal

            bool operator==(const Vertex &other) const
            {
                return pos == other.pos && uv == other.uv && normal == other.normal;
            }
        };

        /**
         * @brief Represents a reference to a vertex within a group. The group index refers to a vertex group,
         * and the vertex index refers to a specific vertex within that group.
         */
        struct VertexRef
        {
            u32 group;  ///< Index of the vertex group.
            u32 vertex; ///< Index of the specific vertex within the entire vertex buffer
        };

        // Represents a group of vertices. Help structure. Not used directly in the UMBF Mesh.
        struct VertexGroup
        {
            acul::vector<u32> vertices; ///< List of indices pointing to vertices in this group.
            acul::vector<u32> faces;    ///< List of indices of faces that reference these vertices.
        };

        // Represents a polygon face.
        struct Face
        {
            acul::vector<VertexRef> vertices; ///< List of vertex references that define the face.
            amal::vec3 normal;                ///< The normal vector of the face
            u32 first_vertex;                 ///< Starting index in the index buffer for this face.
            u32 count;                        ///< Number of indices that define this face.
        };

        using AABB = acul::min_max<amal::vec3>;

        // Represents a 3D mesh geometry.
        struct Geometry
        {
            acul::vector<Vertex> vertices; ///< Array containing all vertices of the model.
            u32 group_count;               ///< Size of the vertex group array.
            acul::vector<Face> faces;      ///< Array of faces that make up the model.
            acul::vector<u32> indices;     ///< Array of indices for rendering the model.
            AABB aabb;                     ///< Axis-aligned bounding box that encloses the model.
        };

        struct Transform
        {
            amal::vec3 position = {0.0f, 0.0f, 0.0f};
            amal::vec3 rotation = {0.0f, 0.0f, 0.0f};
            amal::vec3 scale = {1.0f, 1.0f, 1.0f};
            amal::vec3 center = {0.0f, 0.0f, 0.0f};
        };

        // Represents a block of mesh data.
        struct Mesh : Block
        {
            Geometry geometry;   ///< The 3D geometry data contained.
            Transform transform; ///< Transformation information.

            u32 signature() const noexcept override { return sign_block::mesh; }
        };
    } // namespace mesh

    namespace registry
    {
        extern UMBF_EXPORT const BlockStream object_info;
        extern UMBF_EXPORT const BlockStream mesh;
    } // namespace registry

    UMBF_EXPORT void insert_scene_streams(registry::HashResolver &resolver);
} // namespace umbf

namespace std
{
    template <>
    struct hash<umbf::mesh::Vertex>
    {
        size_t operator()(const umbf::mesh::Vertex &vertex) const
        {
            size_t seed = 0;
            acul::hash_combine(seed, vertex.pos);
            acul::hash_combine(seed, vertex.uv);
            acul::hash_combine(seed, vertex.normal);
            return seed;
        }
    };
} // namespace std
