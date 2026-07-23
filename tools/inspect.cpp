#include <rbfsafe/rbfsafe.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: rbfsafe-inspect <database-or-archive> [q0 q1 ...]\n";
        return 2;
    }
    auto attestation = rbfsafe::load_artifact_attestation(std::filesystem::path(argv[1]));
    if (attestation) {
        if (argc > 2) {
            std::cerr << "configuration queries do not apply to artifact attestations\n";
            return 2;
        }
        std::cout << "RBF-Safe artifact attestation\n"
                  << "schema: 1\n"
                  << "attestation: " << attestation.value().id << '\n'
                  << "service: " << attestation.value().service_id << '\n'
                  << "key: " << attestation.value().key_id << '\n'
                  << "algorithm: "
                  << rbfsafe::artifact_authentication_algorithm_name(attestation.value().algorithm) << '\n'
                  << "artifact: " << attestation.value().artifact_id << '\n'
                  << "generation: " << attestation.value().artifact_generation << '\n'
                  << "payload: " << attestation.value().payload_digest << '\n'
                  << "bytes: " << attestation.value().payload_bytes << '\n'
                  << "verified: false\n";
        return 0;
    }
    auto atlas = rbfsafe::SafeAtlas::load(std::filesystem::path(argv[1]));
    bool loaded_from_store = false;
    std::size_t stored_versions = 0;
    std::string store_head;
    if (!atlas) {
        auto store = rbfsafe::AtlasVersionStore::open(std::filesystem::path(argv[1]));
        if (store) {
            stored_versions = store.value().versions().size();
            store_head = store.value().current_version_id();
            atlas = store.value().load_current();
            loaded_from_store = static_cast<bool>(atlas);
        }
    }
    if (!atlas) {
        auto database = rbfsafe::RegionDatabase::load(std::filesystem::path(argv[1]));
        if (database) {
            std::cout << "RBF-Safe region database\n"
                      << "schema: 1\n"
                      << "dimension: " << database.value().dimension() << '\n'
                      << "records: " << database.value().records().size() << '\n'
                      << "certificates: " << database.value().certificates().size() << '\n'
                      << "robot: " << database.value().robot_digest() << '\n'
                      << "scene: " << database.value().scene_digest() << '\n'
                      << "scene version: " << database.value().scene_version() << '\n';
            if (argc > 2) {
                if (static_cast<std::size_t>(argc - 2) != database.value().dimension()) {
                    std::cerr << "query dimension does not match database dimension\n";
                    return 2;
                }
                rbfsafe::Configuration query;
                try {
                    for (int index = 2; index < argc; ++index) {
                        std::size_t consumed = 0;
                        const double value = std::stod(argv[index], &consumed);
                        if (consumed != std::string(argv[index]).size() || !std::isfinite(value))
                            throw std::invalid_argument("invalid coordinate");
                        query.push_back(value);
                    }
                } catch (const std::exception&) {
                    std::cerr << "query coordinates must be finite decimal numbers\n";
                    return 2;
                }
                auto records = database.value().regions_at(query);
                auto nearest = database.value().nearest_region(query);
                if (!records || !nearest) {
                    std::cerr << (!records ? records.error().describe() : nearest.error().describe()) << '\n';
                    return 1;
                }
                std::cout << "query contains: " << (!records.value().empty() ? "true" : "false")
                          << "\nquery regions:";
                for (const auto& record : records.value())
                    std::cout << ' ' << record.id;
                std::cout << '\n';
                if (nearest.value())
                    std::cout << "nearest region: " << nearest.value()->id << '\n';
            }
            return 0;
        }
        auto feedback = rbfsafe::PolicyFeedbackDatabase::load(std::filesystem::path(argv[1]));
        if (feedback) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to policy feedback databases\n";
                return 2;
            }
            const auto summary = feedback.value().summary();
            std::cout << "RBF-Safe policy feedback\n"
                      << "schema: 1\n"
                      << "records: " << summary.records << '\n'
                      << "selected accepted: " << summary.selected_accepted << '\n'
                      << "selected repaired: " << summary.selected_repaired << '\n'
                      << "eligible not selected: " << summary.eligible_not_selected << '\n'
                      << "policy rejected: " << summary.policy_rejected << '\n'
                      << "shield rejected: " << summary.shield_rejected << '\n';
            return 0;
        }
        auto memory = rbfsafe::SafetyMemory::load(std::filesystem::path(argv[1]));
        if (memory) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to safety memory databases\n";
                return 2;
            }
            const auto summary = memory.value().summary();
            std::cout << "RBF-Safe safety memory\n"
                      << "schema: 1\n"
                      << "artifacts: " << summary.artifacts << '\n'
                      << "active: " << summary.active << '\n'
                      << "stale: " << summary.stale << '\n'
                      << "quarantined: " << summary.quarantined << '\n'
                      << "retired: " << summary.retired << '\n'
                      << "events: " << summary.events << '\n'
                      << "recorded reuses: " << summary.recorded_reuses << '\n';
            return 0;
        }
        auto memory_store = rbfsafe::SafetyMemoryStore::open(std::filesystem::path(argv[1]));
        if (memory_store) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to safety memory stores\n";
                return 2;
            }
            auto current_memory = memory_store.value().load_current();
            if (!current_memory) {
                std::cerr << current_memory.error().describe() << '\n';
                return 1;
            }
            const auto summary = current_memory.value().summary();
            std::cout << "RBF-Safe safety memory store\n"
                      << "schema: 1\n"
                      << "revisions: " << memory_store.value().revisions().size() << '\n'
                      << "current: " << memory_store.value().current_revision_id() << '\n'
                      << "memory identity: " << current_memory.value().identity() << '\n'
                      << "artifacts: " << summary.artifacts << '\n'
                      << "active: " << summary.active << '\n'
                      << "stale: " << summary.stale << '\n'
                      << "events: " << summary.events << '\n';
            return 0;
        }
        auto schedule_archive = rbfsafe::FleetScheduleArchive::load(std::filesystem::path(argv[1]));
        if (schedule_archive) {
            if (argc > 2) {
                std::cerr << "configuration queries do not apply to fleet schedule archives\n";
                return 2;
            }
            std::cout << "RBF-Safe fleet schedule archive\n"
                      << "schema: 1\n"
                      << "fleet: " << schedule_archive.value().fleet_id() << '\n'
                      << "versions: " << schedule_archive.value().versions().size() << '\n'
                      << "current: " << schedule_archive.value().current_version_id() << '\n';
            if (!schedule_archive.value().current_version_id().empty()) {
                auto version = schedule_archive.value().current_version();
                if (!version) {
                    std::cerr << version.error().describe() << '\n';
                    return 1;
                }
                std::cout << "memory: " << version.value().memory_id << '\n'
                          << "snapshot: " << version.value().fleet.id << '\n'
                          << "status: " << rbfsafe::fleet_schedule_status_name(version.value().report.status)
                          << '\n'
                          << "reservations: " << version.value().report.reservations.size() << '\n'
                          << "conflicts: " << version.value().report.conflicts.size() << '\n'
                          << "pair evaluations: " << version.value().report.pair_evaluations << '\n';
            }
            return 0;
        }
        auto corridor = rbfsafe::HipacCorridor::load(std::filesystem::path(argv[1]));
        if (!corridor) {
            std::cerr << "Atlas load failed: " << atlas.error().describe() << '\n'
                      << "region database load failed: " << database.error().describe() << '\n'
                      << "policy feedback load failed: " << feedback.error().describe() << '\n'
                      << "safety memory load failed: " << memory.error().describe() << '\n'
                      << "safety memory store load failed: " << memory_store.error().describe() << '\n'
                      << "fleet schedule archive load failed: " << schedule_archive.error().describe() << '\n'
                      << "corridor load failed: " << corridor.error().describe() << '\n';
            return 1;
        }
        std::cout << "RBF-Safe corridor\n"
                  << "dimension: " << corridor.value().dimension() << '\n'
                  << "regions: " << corridor.value().regions().size() << '\n'
                  << "portals: " << corridor.value().portals().size() << '\n'
                  << "robot: " << corridor.value().robot_digest() << '\n'
                  << "scene: " << corridor.value().scene_digest() << '\n';
        if (argc > 2) {
            if (static_cast<std::size_t>(argc - 2) != corridor.value().dimension()) {
                std::cerr << "query dimension does not match corridor dimension\n";
                return 2;
            }
            rbfsafe::Configuration query;
            query.reserve(corridor.value().dimension());
            try {
                for (int index = 2; index < argc; ++index) {
                    std::size_t consumed = 0;
                    const double value = std::stod(argv[index], &consumed);
                    if (consumed != std::string(argv[index]).size() || !std::isfinite(value))
                        throw std::invalid_argument("invalid coordinate");
                    query.push_back(value);
                }
            } catch (const std::exception&) {
                std::cerr << "query coordinates must be finite decimal numbers\n";
                return 2;
            }
            auto regions = corridor.value().regions_at(query);
            if (!regions) {
                std::cerr << regions.error().describe() << '\n';
                return 1;
            }
            std::cout << "query contains: " << (!regions.value().empty() ? "true" : "false") << '\n'
                      << "query regions:";
            for (const auto id : regions.value())
                std::cout << ' ' << id;
            std::cout << '\n';
        }
        return 0;
    }
    if (loaded_from_store) {
        std::cout << "RBF-Safe version store\n"
                  << "versions: " << stored_versions << '\n'
                  << "current: " << store_head << '\n';
    }
    std::cout << "RBF-Safe atlas\n"
              << "schema: " << atlas.value().storage_schema() << '\n'
              << "dimension: " << atlas.value().dimension() << '\n'
              << "regions: " << atlas.value().regions().size() << '\n'
              << "certificates: " << atlas.value().certificates().size() << '\n'
              << "repair domains: " << atlas.value().repair_domains().size() << '\n'
              << "lect nodes: " << atlas.value().lect().size() << '\n'
              << "version: " << atlas.value().version_info().id << '\n'
              << "sequence: " << atlas.value().version_info().sequence << '\n'
              << "robot: " << atlas.value().robot_digest() << '\n'
              << "scene: " << atlas.value().scene_digest() << '\n';
    if (argc > 2) {
        if (static_cast<std::size_t>(argc - 2) != atlas.value().dimension()) {
            std::cerr << "query dimension does not match atlas dimension\n";
            return 2;
        }
        rbfsafe::Configuration query;
        query.reserve(atlas.value().dimension());
        try {
            for (int index = 2; index < argc; ++index) {
                std::size_t consumed = 0;
                const double value = std::stod(argv[index], &consumed);
                if (consumed != std::string(argv[index]).size() || !std::isfinite(value)) {
                    throw std::invalid_argument("invalid coordinate");
                }
                query.push_back(value);
            }
        } catch (const std::exception&) {
            std::cerr << "query coordinates must be finite decimal numbers\n";
            return 2;
        }
        auto regions = atlas.value().regions_at(query);
        auto nearest = atlas.value().nearest_region(query);
        if (!regions || !nearest) {
            std::cerr << (!regions ? regions.error().describe() : nearest.error().describe()) << '\n';
            return 1;
        }
        std::cout << "query contains: " << (!regions.value().empty() ? "true" : "false") << '\n'
                  << "query regions:";
        for (const auto& region : regions.value())
            std::cout << ' ' << region.id;
        std::cout << '\n';
        if (nearest.value())
            std::cout << "nearest region: " << nearest.value()->id << '\n';
    }
    return 0;
}
