#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{

double seconds(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration<double>(duration).count();
}

std::uint64_t next_random(std::uint64_t& state)
{
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
}

double uniform(std::uint64_t& state)
{
    return static_cast<double>(next_random(state) >> 11u) /
        static_cast<double>(std::uint64_t{1} << 53u);
}

} // namespace

int main()
{
    const std::filesystem::path path =
        std::filesystem::path(NEXSDF_MODEL_DIR) / "sdflib/Gear.obj";
    const nexsdf::SurfaceMesh mesh = nexsdf::load_obj(path.string());
    const nexsdf::ExactSurface surface(mesh);
    std::vector<std::uint32_t> indices(mesh.triangles.size());
    std::iota(indices.begin(), indices.end(), 0u);

    constexpr std::size_t sample_count = 1024;
    std::vector<nexsdf::Vec3> points(sample_count);
    std::uint64_t random_state = 0x4e657844796e5364ull;
    const nexsdf::Vec3 extent = surface.bounds().extent();
    for (nexsdf::Vec3& point : points)
    {
        point = {
            surface.bounds().minimum.x + uniform(random_state) * extent.x,
            surface.bounds().minimum.y + uniform(random_state) * extent.y,
            surface.bounds().minimum.z + uniform(random_state) * extent.z};
    }

    std::vector<double> bvh(sample_count);
    std::vector<nexsdf::QueryResult> batch(sample_count);
    std::vector<double> exhaustive(sample_count);
    const auto bvh_begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < sample_count; ++i)
        bvh[i] = surface.query(points[i]).phi;
    const auto bvh_end = std::chrono::steady_clock::now();

    const auto batch_begin = std::chrono::steady_clock::now();
    surface.query_batch(points.data(), points.size(), batch.data());
    const auto batch_end = std::chrono::steady_clock::now();

    const auto exhaustive_begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < sample_count; ++i)
        exhaustive[i] = surface.query_subset(points[i], indices.data(), indices.size()).phi;
    const auto exhaustive_end = std::chrono::steady_clock::now();

    double maximum_error = 0.0;
    for (std::size_t i = 0; i < sample_count; ++i)
    {
        maximum_error = std::max(maximum_error, std::abs(bvh[i] - exhaustive[i]));
        maximum_error = std::max(maximum_error, std::abs(batch[i].phi - exhaustive[i]));
    }
    if (maximum_error > 1.0e-12)
    {
        std::cerr << "BVH/exhaustive maximum error=" << maximum_error << '\n';
        return 1;
    }

    const double bvh_seconds = seconds(bvh_end - bvh_begin);
    const double batch_seconds = seconds(batch_end - batch_begin);
    const double exhaustive_seconds = seconds(exhaustive_end - exhaustive_begin);
    std::cout << "model_triangles=" << mesh.triangles.size()
              << " samples=" << sample_count
              << " max_error=" << maximum_error
              << " bvh_seconds=" << bvh_seconds
              << " batch_seconds=" << batch_seconds
              << " exhaustive_seconds=" << exhaustive_seconds
              << " bvh_speedup=" << exhaustive_seconds / bvh_seconds
              << " batch_speedup=" << exhaustive_seconds / batch_seconds
              << " batch_vs_scalar=" << bvh_seconds / batch_seconds << '\n';
    return 0;
}
