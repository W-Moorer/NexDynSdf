#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

double halton(std::uint64_t index, std::uint32_t base)
{
    double value = 0.0;
    double factor = 1.0 / static_cast<double>(base);
    while (index != 0)
    {
        value += factor * static_cast<double>(index % base);
        index /= base;
        factor /= static_cast<double>(base);
    }
    return value;
}

std::filesystem::path temporary_asset_path(std::size_t filter)
{
    static std::atomic<std::uint64_t> counter{0};
#ifdef _WIN32
    const auto process = static_cast<unsigned long long>(_getpid());
#else
    const auto process = static_cast<unsigned long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() /
        ("nexsdf-influence-" + std::to_string(process) + "-" +
         std::to_string(filter) + "-" + std::to_string(counter.fetch_add(1)) +
         ".nsdf");
}

} // namespace

int main()
{
    try
    {
        const std::filesystem::path model =
            std::filesystem::path(NEXSDF_MODEL_DIR) / "sdflib" / "Gear.obj";
        const nexsdf::SurfaceMesh mesh = nexsdf::load_obj(model.string());
        const nexsdf::ExactSurface exact(mesh);
        std::vector<std::uint32_t> all_triangles(mesh.triangles.size());
        std::iota(all_triangles.begin(), all_triangles.end(), 0u);

        std::vector<nexsdf::Asset> assets;
        for (const nexsdf::InfluenceFilter filter : {
                 nexsdf::InfluenceFilter::AabbLipschitz,
                 nexsdf::InfluenceFilter::PaperGjk,
                 nexsdf::InfluenceFilter::PaperFrankWolfe})
        {
            nexsdf::BuildOptions options;
            options.representation = nexsdf::Representation::ExactInfluenceOctree;
            options.reconstruction = nexsdf::Reconstruction::Exact;
            options.influence_filter = filter;
            options.start_depth = 1;
            options.maximum_depth = 4;
            options.maximum_triangles_per_leaf = 128;
            options.relative_padding = 0.1;
            assets.push_back(nexsdf::build(mesh, options));
            const auto& info = assets.back().info();
            check(info.influence_filter == filter &&
                  info.candidate_index_count != 0 && info.node_count > 1,
                "Gear exact asset records selectable influence provenance");
        }

        const nexsdf::Aabb domain = assets.front().info().domain;
        const nexsdf::Vec3 extent = domain.extent();
        for (std::uint64_t i = 1; i <= 257; ++i)
        {
            const nexsdf::Vec3 point{
                domain.minimum.x + extent.x * halton(i + 20260813, 2),
                domain.minimum.y + extent.y * halton(i + 20260813, 3),
                domain.minimum.z + extent.z * halton(i + 20260813, 5)};
            const nexsdf::QueryResult reference = exact.query_subset(
                point, all_triangles.data(), all_triangles.size());
            for (std::size_t filter = 0; filter < assets.size(); ++filter)
            {
                const nexsdf::QueryResult result = assets[filter].query(point);
                check(result.valid && result.exact &&
                      std::abs(result.phi - reference.phi) <= 1.0e-12,
                    "Gear influence leaf retains an exhaustive nearest triangle");
                check(nexsdf::norm(result.witness - reference.witness) <= 1.0e-12 &&
                      nexsdf::norm(result.raw_gradient - reference.raw_gradient) <= 1.0e-12,
                    "Gear influence leaf preserves exhaustive witness and gradient");
                const nexsdf::QueryResult repeated = assets[filter].query(point);
                check(repeated.branch_signature == result.branch_signature &&
                      repeated.face_id == result.face_id &&
                      repeated.feature == result.feature,
                    "Gear influence query branch and feature are deterministic");
            }
        }

        for (std::size_t filter = 0; filter < assets.size(); ++filter)
        {
            const std::filesystem::path path = temporary_asset_path(filter);
            assets[filter].save(path.string());
            const nexsdf::Asset loaded = nexsdf::Asset::load(path.string());
            const nexsdf::Vec3 point = (domain.minimum + domain.maximum) * 0.5;
            const nexsdf::QueryResult before = assets[filter].query(point);
            const nexsdf::QueryResult after = loaded.query(point);
            check(loaded.info().influence_filter == assets[filter].info().influence_filter &&
                  loaded.info().candidate_index_count ==
                      assets[filter].info().candidate_index_count,
                "influence metadata survives NSDF serialization");
            check(after.phi == before.phi &&
                  after.branch_signature == before.branch_signature &&
                  after.face_id == before.face_id &&
                  after.feature == before.feature &&
                  nexsdf::norm(after.witness - before.witness) == 0.0,
                "influence query survives NSDF serialization bit-for-bit");
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }

    if (failures != 0)
    {
        std::cerr << failures << " influence checks failed\n";
        return 1;
    }
    std::cout << "all paper influence checks passed\n";
    return 0;
}
