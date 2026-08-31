#pragma once

#include "scene.hpp"

namespace umbf::mesh
{
    UMBF_EXPORT void fill_vertex_groups(const mesh::Geometry &geometry, acul::vector<mesh::VertexGroup> &groups);
}