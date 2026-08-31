#include <umbf/ext/scene/utils.hpp>

namespace umbf::mesh
{
    void fill_vertex_groups(const mesh::Geometry &geometry, acul::vector<mesh::VertexGroup> &groups)
    {
        groups.resize(geometry.group_count);
        for (size_t face_index = 0; face_index < geometry.faces.size(); ++face_index)
        {
            for (const auto &vertex_ref : geometry.faces[face_index].vertices)
            {
                groups[vertex_ref.group].faces.push_back(static_cast<u32>(face_index));
                groups[vertex_ref.group].vertices.push_back(vertex_ref.vertex);
            }
        }
    }
} // namespace umbf::mesh
