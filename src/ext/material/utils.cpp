#include <algorithm>
#include <numeric>
#include <umbf/ext/material/utils.hpp>

namespace umbf
{
    void filter_material_assignments(const acul::vector<acul::shared_ptr<MaterialRange>> &assignments,
                                     size_t face_count, u64 default_id,
                                     acul::vector<acul::shared_ptr<MaterialRange>> &dst)
    {
        auto default_assignment = acul::make_shared<MaterialRange>();
        default_assignment->mat_id = default_id;
        default_assignment->faces.resize(face_count);
        std::iota(default_assignment->faces.begin(), default_assignment->faces.end(), 0u);

        if (assignments.empty())
        {
            dst.push_back(default_assignment);
            return;
        }

        acul::vector<bool> face_included(face_count, false);
        for (const auto &assignment : assignments)
            for (const u32 face : assignment->faces)
                if (face < face_count) face_included[face] = true;

        default_assignment->faces.erase(
            std::remove_if(default_assignment->faces.begin(), default_assignment->faces.end(),
                           [&](u32 index) { return face_included[index]; }),
            default_assignment->faces.end());
        if (!default_assignment->faces.empty()) dst.push_back(default_assignment);
        for (const auto &assignment : assignments) dst.push_back(assignment);
    }
} // namespace umbf
