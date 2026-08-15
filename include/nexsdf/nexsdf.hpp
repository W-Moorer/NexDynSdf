#pragma once

#include "nexsdf/export.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nexsdf
{

struct NEXSDF_API Vec3
{
    double x{0.0};
    double y{0.0};
    double z{0.0};

    double& operator[](std::size_t i) noexcept;
    const double& operator[](std::size_t i) const noexcept;
};

NEXSDF_API Vec3 operator+(Vec3 a, Vec3 b) noexcept;
NEXSDF_API Vec3 operator-(Vec3 a, Vec3 b) noexcept;
NEXSDF_API Vec3 operator*(Vec3 a, double s) noexcept;
NEXSDF_API Vec3 operator*(double s, Vec3 a) noexcept;
NEXSDF_API Vec3 operator/(Vec3 a, double s) noexcept;
NEXSDF_API double dot(Vec3 a, Vec3 b) noexcept;
NEXSDF_API Vec3 cross(Vec3 a, Vec3 b) noexcept;
NEXSDF_API double squared_norm(Vec3 a) noexcept;
NEXSDF_API double norm(Vec3 a) noexcept;
NEXSDF_API Vec3 normalized(Vec3 a) noexcept;

struct NEXSDF_API Aabb
{
    Vec3 minimum{};
    Vec3 maximum{};

    bool contains(Vec3 p) const noexcept;
    Vec3 extent() const noexcept;
};

struct Triangle
{
    std::array<std::uint32_t, 3> vertex{};
    std::uint32_t face_id{0};
    std::array<Vec3, 3> corner_normal{};
    bool has_corner_normals{false};
};

struct SurfaceMesh
{
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    std::string source_path;
};

struct CreaseEdge
{
    std::array<std::uint32_t, 2> vertex{};
    Vec3 c_sharp{};
};

enum class Feature : std::uint32_t
{
    Unknown = 0,
    Face = 1,
    Edge = 2,
    Vertex = 3
};

enum class Representation : std::uint32_t
{
    DenseGrid = 1,
    ExactInfluenceOctree = 2,
    AdaptivePiecewiseOctree = 3
};

enum class Reconstruction : std::uint32_t
{
    Exact = 0,
    Trilinear = 1,
    TricubicHermite = 2,
    GradientTaylor = 3
};

enum class InfluenceFilter : std::uint32_t
{
    AabbLipschitz = 0,
    PaperGjk = 1,
    PaperFrankWolfe = 2
};

enum class CompositionPolicy : std::uint32_t
{
    SeparateAssets = 0,
    SolidUnion = 1,
    NestedParity = 2
};

enum class ComputeBackend : std::uint32_t
{
    CpuScalar = 0,
    CpuParallel = 1,
    CudaExperimental = 2
};

enum class BatchBackend : std::uint32_t
{
    Scalar = 0,
    AutoSimd = 1,
    CudaExperimental = 2
};

struct BatchQueryOptions
{
    BatchBackend backend{BatchBackend::Scalar};
    std::uint32_t worker_threads{1};
};

enum class Status : std::uint32_t
{
    Ok = 0,
    InvalidArgument,
    IoError,
    InvalidFormat,
    InvalidMesh,
    Unsupported,
    OutOfDomain,
    CorruptAsset,
    InternalError
};

struct NEXSDF_API MeshValidation
{
    bool finite{false};
    bool non_degenerate{false};
    bool closed_two_manifold{false};
    bool consistently_oriented{false};
    std::size_t boundary_edges{0};
    std::size_t non_manifold_edges{0};
    std::size_t orientation_mismatches{0};
    std::size_t connected_components{0};
    std::string message;

    bool valid_for_signed_distance() const noexcept;
};

struct QueryResult
{
    double phi{0.0};
    Vec3 raw_gradient{};
    Vec3 unit_normal{};
    std::array<double, 9> hessian{};
    Vec3 witness{};
    std::uint32_t face_id{0};
    Feature feature{Feature::Unknown};
    double measured_leaf_error{0.0};
    std::uint64_t branch_signature{0};
    std::uint32_t cell_depth{0};
    // Conservative asset-local point-motion radius that preserves the exact
    // closest triangle and its face-interior Voronoi branch. Edge/vertex
    // branches and nearest-triangle ties report zero.
    double branch_motion_clearance{0.0};
    bool valid{false};
    bool exact{false};
    bool in_domain{false};
    bool has_hessian{false};
    bool has_witness{false};
    bool has_measured_error{false};
};

struct BuildOptions
{
    Representation representation{Representation::DenseGrid};
    Reconstruction reconstruction{Reconstruction::Trilinear};
    InfluenceFilter influence_filter{InfluenceFilter::AabbLipschitz};
    CompositionPolicy composition{CompositionPolicy::SeparateAssets};
    std::array<std::uint32_t, 3> resolution{32, 32, 32};
    std::uint32_t maximum_depth{7};
    std::uint32_t start_depth{1};
    std::uint32_t maximum_triangles_per_leaf{64};
    double relative_padding{0.1};
    double absolute_padding{0.0};
    double error_tolerance{1.0e-3};
    double derivative_step{0.0};
    ComputeBackend backend{ComputeBackend::CpuScalar};
    std::uint32_t worker_threads{1};
};

struct AssetInfo
{
    std::uint32_t format_major{1};
    std::uint32_t format_minor{3};
    Representation representation{Representation::DenseGrid};
    Reconstruction reconstruction{Reconstruction::Trilinear};
    InfluenceFilter influence_filter{InfluenceFilter::AabbLipschitz};
    CompositionPolicy composition{CompositionPolicy::SeparateAssets};
    Aabb domain{};
    std::array<std::uint32_t, 3> resolution{};
    std::uint32_t maximum_depth{0};
    double requested_error_tolerance{0.0};
    double measured_maximum_error{0.0};
    std::uint64_t node_count{0};
    std::uint64_t coefficient_count{0};
    std::uint64_t triangle_count{0};
    std::uint64_t candidate_index_count{0};
    std::uint32_t component_count{1};
    std::uint32_t active_component_count{1};
    bool has_measured_error{false};
    ComputeBackend build_backend{ComputeBackend::CpuScalar};
    std::uint32_t worker_threads{1};
};

NEXSDF_API SurfaceMesh load_obj(const std::string& path);
NEXSDF_API SurfaceMesh load_nsm_v1(const std::string& path);
NEXSDF_API SurfaceMesh load_stl(const std::string& path);
NEXSDF_API SurfaceMesh load_surface_mesh(const std::string& path);
NEXSDF_API std::vector<CreaseEdge> load_eng_v1(
    const std::string& path,
    const SurfaceMesh& associated_mesh);
NEXSDF_API bool is_cuda_backend_available() noexcept;
NEXSDF_API MeshValidation validate_mesh(const SurfaceMesh& mesh);

class NEXSDF_API ExactSurface
{
public:
    explicit ExactSurface(
        SurfaceMesh mesh,
        CompositionPolicy composition = CompositionPolicy::SeparateAssets);

    const SurfaceMesh& mesh() const noexcept;
    const MeshValidation& validation() const noexcept;
    const Aabb& bounds() const noexcept;
    CompositionPolicy composition() const noexcept;
    std::size_t component_count() const noexcept;
    std::size_t active_component_count() const noexcept;
    const std::vector<std::uint32_t>& active_triangles() const noexcept;
    QueryResult query(Vec3 point) const;
    QueryResult query_certified(Vec3 point) const;
    void query_batch(const Vec3* points, std::size_t count, QueryResult* out) const;
    QueryResult query_subset(
        Vec3 point,
        const std::uint32_t* triangle_indices,
        std::size_t count) const;

private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

class Asset;
NEXSDF_API Asset build(const SurfaceMesh& mesh, const BuildOptions& options);

class NEXSDF_API Asset
{
public:
    Asset();
    ~Asset();
    Asset(Asset&&) noexcept;
    Asset& operator=(Asset&&) noexcept;
    Asset(const Asset&);
    Asset& operator=(const Asset&);

    const AssetInfo& info() const noexcept;
    QueryResult query(Vec3 point) const;
    QueryResult query_certified(Vec3 point) const;
    void query_certified_batch(
        const Vec3* points,
        std::size_t count,
        QueryResult* out) const;
    void query_certified_batch(
        const Vec3* points,
        std::size_t count,
        QueryResult* out,
        const BatchQueryOptions& options) const;
    void query_batch(const Vec3* points, std::size_t count, QueryResult* out) const;
    void query_batch(
        const Vec3* points,
        std::size_t count,
        QueryResult* out,
        const BatchQueryOptions& options) const;
    void save(const std::string& path) const;
    static Asset load(const std::string& path);

private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Asset(std::shared_ptr<const Impl> impl);
    friend NEXSDF_API Asset build(const SurfaceMesh&, const BuildOptions&);
};

NEXSDF_API const char* status_message(Status status) noexcept;

} // namespace nexsdf
