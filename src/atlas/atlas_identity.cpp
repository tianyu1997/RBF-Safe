#include "internal/atlas_identity.h"

#include "internal/json.h"
#include "internal/sha256.h"

namespace rbfsafe::internal {
namespace {

Json workspace_box_json(const WorkspaceAabb& box) {
    Json::Array lower;
    Json::Array upper;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        lower.emplace_back(box.lower[axis]);
        upper.emplace_back(box.upper[axis]);
    }
    return Json::Object{{"lower", std::move(lower)}, {"upper", std::move(upper)}};
}

Json cspace_box_json(const CspaceAabb& box) {
    Json::Array axes;
    for (const auto& axis : box.axes())
        axes.emplace_back(Json::Array{axis.lower, axis.upper});
    return axes;
}

} // namespace

std::string atlas_version_identity(const SafeAtlas& atlas) {
    Json::Array regions;
    for (std::size_t index = 0; index < atlas.regions().size(); ++index) {
        const auto& region = atlas.regions()[index];
        Json::Array links;
        if (index < atlas.dependencies().size()) {
            for (const auto& link : atlas.dependencies()[index].envelope.links)
                links.emplace_back(workspace_box_json(link));
        }
        Json::Array neighbors;
        if (index < atlas.adjacency().size()) {
            for (const auto neighbor : atlas.adjacency()[index]) {
                if (neighbor < atlas.regions().size())
                    neighbors.emplace_back(std::to_string(atlas.regions()[neighbor].id));
            }
        }
        const std::string certificate_id = region.certificate_index < atlas.certificates().size()
                                               ? atlas.certificates()[region.certificate_index].id
                                               : std::string{};
        regions.emplace_back(Json::Object{
            {"bounds", cspace_box_json(region.bounds)},
            {"certificate_id", certificate_id},
            {"component", std::to_string(region.component)},
            {"envelope", std::move(links)},
            {"id", std::to_string(region.id)},
            {"neighbors", std::move(neighbors)},
            {"source_node", region.source_node.path()},
        });
    }
    Json::Array repair_domains;
    for (const auto& domain : atlas.repair_domains()) {
        repair_domains.emplace_back(Json::Object{
            {"bounds", cspace_box_json(domain.bounds)},
            {"id", std::to_string(domain.id)},
            {"source_node", domain.source_node.path()},
        });
    }
    return sha256(Json(Json::Object{
                           {"parent_id", atlas.version_info().parent_id},
                           {"repair_domains", std::move(repair_domains)},
                           {"regions", std::move(regions)},
                           {"robot_digest", atlas.robot_digest()},
                           {"scene_digest", atlas.scene_digest()},
                           {"scene_version", atlas.version_info().scene_version},
                           {"sequence", std::to_string(atlas.version_info().sequence)},
                           {"transition_digest", atlas.version_info().transition_digest},
                       })
                      .dump(false));
}

} // namespace rbfsafe::internal
