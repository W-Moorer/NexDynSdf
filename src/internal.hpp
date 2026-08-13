#pragma once

#include "nexsdf/nexsdf.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <vector>

namespace nexsdf::detail
{

struct Node
{
    Aabb box{};
    std::array<std::int32_t, 8> child{
        -1, -1, -1, -1, -1, -1, -1, -1};
    std::uint64_t data_offset{0};
    std::uint32_t data_count{0};
    std::uint32_t depth{0};
    double measured_error{0.0};

    bool leaf() const noexcept { return child[0] < 0; }
};

struct AssetData
{
    AssetInfo info{};
    SurfaceMesh mesh{};
    std::vector<Node> nodes;
    std::vector<std::uint32_t> triangle_indices;
    std::vector<double> coefficients;
    std::shared_ptr<const ExactSurface> exact_surface;
};

struct PolynomialSample
{
    double value{0.0};
    Vec3 gradient{};
    std::array<double, 9> hessian{};
};

struct TrianglePseudoNormals
{
    Vec3 face{};
    std::array<Vec3, 3> edge{};
    std::array<Vec3, 3> vertex{};
};

QueryResult exact_query_triangle(
    const SurfaceMesh& mesh,
    const std::vector<TrianglePseudoNormals>* pseudo_normals,
    Vec3 point,
    const std::uint32_t* indices,
    std::size_t count,
    bool determine_sign);

double signed_solid_angle(const SurfaceMesh& mesh, Vec3 point);
Aabb mesh_bounds(const SurfaceMesh& mesh);
Aabb padded_bounds(const SurfaceMesh& mesh, const BuildOptions& options);
double aabb_distance(Aabb a, Aabb b) noexcept;
Aabb triangle_bounds(const SurfaceMesh& mesh, std::uint32_t triangle_index);

std::array<double, 8> trilinear_samples(
    const ExactSurface& surface,
    const Aabb& box,
    const double* parent_coefficients = nullptr,
    const Aabb* parent_box = nullptr);
PolynomialSample evaluate_trilinear(
    const double* values,
    const Aabb& box,
    Vec3 point);

std::array<double, 64> tricubic_samples(
    const ExactSurface& surface,
    const Aabb& box,
    double derivative_step,
    const double* parent_coefficients = nullptr,
    const Aabb* parent_box = nullptr);
std::array<double, 8> tricubic_point_jet(
    const ExactSurface& surface,
    Vec3 point,
    double derivative_step);
PolynomialSample evaluate_tricubic(
    const double* jets,
    const Aabb& box,
    Vec3 point);
std::array<double, 8> evaluate_tricubic_jet(
    const double* jets,
    const Aabb& box,
    Vec3 point);

std::array<double, 4> gradient_taylor_sample(
    const ExactSurface& surface,
    Vec3 center);
PolynomialSample evaluate_gradient_taylor(
    const double* values,
    Vec3 center,
    Vec3 point);

std::shared_ptr<const AssetData> build_asset_data(
    const SurfaceMesh& mesh,
    const BuildOptions& options);
QueryResult query_asset(const AssetData& data, Vec3 point);
void save_asset(const AssetData& data, const std::string& path);
std::shared_ptr<const AssetData> load_asset(const std::string& path);

} // namespace nexsdf::detail
