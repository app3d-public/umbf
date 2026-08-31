#pragma once

#include "material.hpp"

namespace umbf
{
    UMBF_EXPORT void filter_material_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignes,
                                                 size_t face_count, u64 default_id,
                                                 acul::vector<acul::shared_ptr<MaterialRange>> &dst);
}