#include <rbfsafe/rbfsafe.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: rbfsafe-inspect <atlas-or-corridor-directory> [q0 q1 ...]\n";
        return 2;
    }
    auto atlas = rbfsafe::SafeAtlas::load(std::filesystem::path(argv[1]));
    if (!atlas) {
        auto corridor = rbfsafe::HipacCorridor::load(std::filesystem::path(argv[1]));
        if (!corridor) {
            std::cerr << "Atlas load failed: " << atlas.error().describe() << '\n'
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
    std::cout << "RBF-Safe atlas\n"
              << "dimension: " << atlas.value().dimension() << '\n'
              << "regions: " << atlas.value().regions().size() << '\n'
              << "certificates: " << atlas.value().certificates().size() << '\n'
              << "lect nodes: " << atlas.value().lect().size() << '\n'
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
