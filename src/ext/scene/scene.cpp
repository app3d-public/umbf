#include <umbf/ext/scene/scene.hpp>

namespace umbf::registry
{
    namespace
    {
        void write_object_info(acul::bin_stream &stream, umbf::Block *block)
        {
            auto *object = static_cast<ObjectInfo *>(block);
            stream.write(object->id).write(object->parent_id).write(object->name);
        }

        umbf::Block *read_object_info(acul::bin_stream &stream)
        {
            auto *object = acul::alloc<ObjectInfo>();
            stream.read(object->id).read(object->parent_id).read(object->name);
            return object;
        }

        void write_object_list(acul::bin_stream &stream, umbf::Block *block)
        {
            const auto &objects = static_cast<ObjectList *>(block)->objects;
            if (objects.size() > UINT32_MAX) throw acul::runtime_error("Too many scene objects");
            stream.write(static_cast<u32>(objects.size()));
            for (const auto &range : objects) stream.write(range.first_block).write(range.block_count);
        }

        umbf::Block *read_object_list(acul::bin_stream &stream)
        {
            auto *list = acul::alloc<ObjectList>();
            u32 count = 0u;
            stream.read(count);
            list->objects.resize(count);
            for (auto &range : list->objects) stream.read(range.first_block).read(range.block_count);
            return list;
        }

        void write_mesh(acul::bin_stream &stream, umbf::Block *block)
        {
            auto *mesh = static_cast<mesh::Mesh *>(block);
            auto &geometry = mesh->geometry;

            stream.write(static_cast<u32>(geometry.vertices.size()))
                .write(geometry.group_count)
                .write(static_cast<u32>(geometry.faces.size()))
                .write(static_cast<u32>(geometry.indices.size()));

            for (const auto &vertex : geometry.vertices) stream.write(vertex.pos).write(vertex.uv).write(vertex.normal);

            for (const auto &face : geometry.faces)
            {
                stream.write(static_cast<u32>(face.vertices.size()))
                    .write(face.vertices.data(), face.vertices.size())
                    .write(face.normal)
                    .write(face.count)
                    .write(geometry.indices.data() + face.first_vertex, face.count);
            }

            stream.write(geometry.aabb.min)
                .write(geometry.aabb.max)
                .write(mesh->transform.position)
                .write(mesh->transform.rotation)
                .write(mesh->transform.scale)
                .write(mesh->transform.center);
        }

        umbf::Block *read_mesh(acul::bin_stream &stream)
        {
            auto *mesh = acul::alloc<mesh::Mesh>();
            auto &geometry = mesh->geometry;
            u32 vertex_count = 0u;
            u32 face_count = 0u;
            u32 index_count = 0u;
            stream.read(vertex_count).read(geometry.group_count).read(face_count).read(index_count);
            geometry.vertices.resize(vertex_count);
            geometry.faces.resize(face_count);
            geometry.indices.resize(index_count);

            for (auto &vertex : geometry.vertices) stream.read(vertex.pos).read(vertex.uv).read(vertex.normal);

            size_t index_offset = 0u;
            for (auto &face : geometry.faces)
            {
                u32 face_vertex_count = 0u;
                stream.read(face_vertex_count);
                face.vertices.resize(face_vertex_count);
                stream.read(face.vertices.data(), face_vertex_count).read(face.normal).read(face.count);
                if (face.count > geometry.indices.size() - index_offset)
                    throw acul::runtime_error("Mesh face index range exceeds the declared index buffer");
                stream.read(geometry.indices.data() + index_offset, face.count);
                face.first_vertex = static_cast<u32>(index_offset);
                index_offset += face.count;
            }
            if (index_offset != geometry.indices.size())
                throw acul::runtime_error("Mesh face index ranges do not cover the declared index buffer");

            stream.read(geometry.aabb.min)
                .read(geometry.aabb.max)
                .read(mesh->transform.position)
                .read(mesh->transform.rotation)
                .read(mesh->transform.scale)
                .read(mesh->transform.center);
            return mesh;
        }
    } // namespace

    const BlockStream object_info{read_object_info, write_object_info};
    const BlockStream mesh{read_mesh, write_mesh};
    const BlockStream object_list{read_object_list, write_object_list};
} // namespace umbf::registry

namespace umbf
{
    void insert_scene_streams(registry::HashResolver &resolver)
    {
        resolver.block_streams[sign_block::object_info] = &registry::object_info;
        resolver.block_streams[sign_block::mesh] = &registry::mesh;
        resolver.block_streams[sign_block::scene_objects_list] = &registry::object_list;
    }
} // namespace umbf
