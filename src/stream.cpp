#include <acul/log.hpp>
#include <umbf/umbf.hpp>
#include <umbf/version.h>
#ifndef UMBF_BUILD_MIN
    #include <umbf/utils.hpp>
#endif

namespace umbf
{
    namespace streams
    {
#ifndef UMBF_BUILD_MIN
        void write_image_info(acul::bin_stream &stream, Image2D *image)
        {
            stream.write(image->width).write(image->height).write(static_cast<u16>(image->channel_count));
            u8 channel_names_size = image->channel_names.size();
            stream.write(reinterpret_cast<char *>(&channel_names_size), sizeof(u8));
            for (const auto &str : image->channel_names) stream.write(str);
            stream.write(image->bytes_per_channel).write(static_cast<u8>(image->format));
        }

        void write_image(acul::bin_stream &stream, acul::meta::block *block)
        {
            auto image = static_cast<Image2D *>(block);
            write_image_info(stream, image);
            if (!image->pixels) throw acul::runtime_error("Pixels cannot be null");
            stream.write(reinterpret_cast<char *>(image->pixels), image->size());
        }

        void read_image_info(acul::bin_stream &stream, Image2D *image)
        {
            stream.read(image->width)
                .read(image->height)
                .read(reinterpret_cast<char *>(&image->channel_count), sizeof(u16));
            u8 channel_list_size;
            stream.read(channel_list_size);
            for (size_t i = 0; i < channel_list_size; i++)
            {
                acul::string chan;
                stream.read(chan);
                image->channel_names.push_back(chan);
            }
            stream.read(image->bytes_per_channel);
            u8 image_format;
            stream.read(reinterpret_cast<char *>(&image_format), sizeof(u8));
            image->format = static_cast<vk::Format>(image_format);
        }

        acul::meta::block *read_image(acul::bin_stream &stream)
        {
            Image2D *image = acul::alloc<Image2D>();
            read_image_info(stream, image);
            char *pixels = acul::alloc_n<char>(image->size());
            stream.read(pixels, image->size());
            image->pixels = (void *)pixels;
            return image;
        }

        void write_image_atlas(acul::bin_stream &stream, acul::meta::block *block)
        {
            auto atlas = static_cast<Atlas *>(block);
            stream.write(atlas->discard_step).write(atlas->padding).write(static_cast<u16>(atlas->pack_data.size()));
            for (auto &rect : atlas->pack_data) stream.write(rect.w).write(rect.h).write(rect.x).write(rect.y);
        }

        acul::meta::block *read_image_atlas(acul::bin_stream &stream)
        {
            auto *atlas = acul::alloc<Atlas>();
            u16 pack_data_size;
            stream.read(atlas->discard_step).read(atlas->padding).read(pack_data_size);
            atlas->pack_data.resize(pack_data_size);
            for (size_t i = 0; i < pack_data_size; i++)
            {
                stream.read(atlas->pack_data[i].w)
                    .read(atlas->pack_data[i].h)
                    .read(atlas->pack_data[i].x)
                    .read(atlas->pack_data[i].y);
            }
            return atlas;
        }

        void write_material(acul::bin_stream &stream, acul::meta::block *block)
        {
            auto material = static_cast<Material *>(block);
            stream.write(material->textures).write(material->albedo);
        }

        acul::meta::block *read_material(acul::bin_stream &stream)
        {
            Material *material = acul::alloc<Material>();
            stream.read(material->textures).read(material->albedo);
            return material;
        }

        void write_scene(acul::bin_stream &stream, acul::meta::block *block)
        {
            auto scene = static_cast<Scene *>(block);
            // Objects
            stream.write(static_cast<u16>(scene->objects.size()));
            for (const auto &object : scene->objects) stream.write(object.id).write(object.name).write(object.meta);

            // Textures
            stream.write(scene->textures).write(scene->materials);
        }

        acul::meta::block *read_scene(acul::bin_stream &stream)
        {
            Scene *scene = acul::alloc<Scene>();
            u16 objectCount;
            stream.read(objectCount);
            scene->objects.resize(objectCount);
            for (auto &object : scene->objects) stream.read(object.id).read(object.name).read(object.meta);
            stream.read(scene->textures).read(scene->materials);
            return scene;
        }

        void write_mesh(acul::bin_stream &stream, acul::meta::block *block)
        {
            mesh::MeshBlock *mesh = static_cast<mesh::MeshBlock *>(block);
            auto &model = mesh->model;

            // Sizes
            stream.write(static_cast<u32>(model.vertices.size()))
                .write(static_cast<u32>(model.group_count))
                .write(static_cast<u32>(model.faces.size()))
                .write(static_cast<u32>(model.indices.size()));

            // Vertices
            for (auto &vertex : model.vertices) stream.write(vertex.pos).write(vertex.uv).write(vertex.normal);

            // Faces
            for (auto &face : model.faces)
            {
                stream.write(static_cast<u32>(face.vertices.size()))
                    .write(face.vertices.data(), face.vertices.size())
                    .write(face.normal)
                    .write(face.count);
                stream.write(model.indices.data() + face.first_vertex, face.count);
            }

            // Other meta info
            stream.write(model.aabb.min)
                .write(model.aabb.max)
                .write(mesh->transform.position)
                .write(mesh->transform.rotation)
                .write(mesh->transform.scale);
        }

        acul::meta::block *read_mesh(acul::bin_stream &stream)
        {
            mesh::MeshBlock *mesh = acul::alloc<mesh::MeshBlock>();
            auto &model = mesh->model;
            // Sizes
            u32 vertex_count, vertex_group_count, face_count, index_count;
            stream.read(vertex_count).read(vertex_group_count).read(face_count).read(index_count);
            model.vertices.resize(vertex_count);
            model.group_count = vertex_group_count;
            model.faces.resize(face_count);
            model.indices.resize(index_count);

            // Vertices
            for (auto &vertex : model.vertices) stream.read(vertex.pos).read(vertex.uv).read(vertex.normal);

            // Faces
            size_t index_offset = 0;
            for (auto &face : model.faces)
            {
                u32 face_vertices_count;
                stream.read(face_vertices_count);
                face.vertices.resize(face_vertices_count);
                stream.read(face.vertices.data(), face_vertices_count).read(face.normal).read(face.count);
                for (int i = 0; i < face.count; i++) stream.read(model.indices[index_offset + i]);
                face.first_vertex = index_offset;
                index_offset += face.count;
            }

            // Other meta info
            stream.read(model.aabb.min)
                .read(model.aabb.max)
                .read(mesh->transform.position)
                .read(mesh->transform.rotation)
                .read(mesh->transform.scale);
            return mesh;
        }

        acul::meta::block *read_material_info(acul::bin_stream &stream)
        {
            MaterialInfo *block = acul::alloc<MaterialInfo>();
            u32 assign_size;
            stream.read(block->id).read(block->name).read(assign_size);
            block->assignments.resize(assign_size);
            stream.read(block->assignments.data(), assign_size);
            return block;
        }

        void write_mat_range_assign(acul::bin_stream &stream, acul::meta::block *block)
        {
            MatRangeAssignAttr *assignment = static_cast<MatRangeAssignAttr *>(block);
            stream.write(assignment->mat_id)
                .write(static_cast<u32>(assignment->faces.size()))
                .write(assignment->faces.data(), assignment->faces.size());
        }

        acul::meta::block *read_mat_range_assign(acul::bin_stream &stream)
        {
            MatRangeAssignAttr *block = acul::alloc<MatRangeAssignAttr>();
            u32 face_size;
            stream.read(block->mat_id).read(face_size);
            block->faces.resize(face_size);
            stream.read(block->faces.data(), face_size);
            return block;
        }
#endif

        void write_target(acul::bin_stream &stream, acul::meta::block *block)
        {
            auto target = static_cast<Target *>(block);
            stream.write(target->header).write(target->url).write(target->checksum);
        }

        acul::meta::block *read_target(acul::bin_stream &stream)
        {
            Target *target = acul::alloc<Target>();
            stream.read(target->header).read(target->url).read(target->checksum);
            return target;
        }

        void write_library(acul::bin_stream &stream, acul::meta::block *block)
        {
            auto library = static_cast<Library *>(block);
            stream.write(library->fileTree);
        }

        acul::meta::block *read_library(acul::bin_stream &stream)
        {
            Library *library = acul::alloc<Library>();
            stream.read(library->fileTree);
            return library;
        }
    } // namespace streams
} // namespace umbf