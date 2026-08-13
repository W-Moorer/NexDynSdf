#include "internal.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace nexsdf::detail
{
namespace
{

void require_cuda(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
    {
        throw std::runtime_error(
            std::string(operation) + ": " + cudaGetErrorString(status));
    }
}

template <class T>
class DeviceBuffer
{
public:
    explicit DeviceBuffer(std::size_t count) : count_(count)
    {
        if (count_ != 0)
            require_cuda(cudaMalloc(reinterpret_cast<void**>(&pointer_), count_ * sizeof(T)),
                "cudaMalloc");
    }
    ~DeviceBuffer() { if (pointer_) cudaFree(pointer_); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    T* get() noexcept { return pointer_; }
    const T* get() const noexcept { return pointer_; }
    std::size_t count() const noexcept { return count_; }
private:
    T* pointer_{nullptr};
    std::size_t count_{0};
};

struct DeviceVec3 { double x, y, z; };

__host__ __device__ DeviceVec3 add(DeviceVec3 a, DeviceVec3 b)
{ return {a.x + b.x, a.y + b.y, a.z + b.z}; }
__host__ __device__ DeviceVec3 sub(DeviceVec3 a, DeviceVec3 b)
{ return {a.x - b.x, a.y - b.y, a.z - b.z}; }
__host__ __device__ DeviceVec3 mul(DeviceVec3 a, double value)
{ return {a.x * value, a.y * value, a.z * value}; }
__host__ __device__ double dot3(DeviceVec3 a, DeviceVec3 b)
{ return a.x * b.x + a.y * b.y + a.z * b.z; }
__host__ __device__ DeviceVec3 cross3(DeviceVec3 a, DeviceVec3 b)
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
__host__ __device__ double norm2(DeviceVec3 value) { return dot3(value, value); }

__device__ double point_triangle_squared(
    DeviceVec3 point, DeviceVec3 a, DeviceVec3 b, DeviceVec3 c)
{
    const DeviceVec3 ab = sub(b, a);
    const DeviceVec3 ac = sub(c, a);
    const DeviceVec3 ap = sub(point, a);
    const double d1 = dot3(ab, ap);
    const double d2 = dot3(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return norm2(ap);
    const DeviceVec3 bp = sub(point, b);
    const double d3 = dot3(ab, bp);
    const double d4 = dot3(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return norm2(bp);
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        const double v = d1 / (d1 - d3);
        return norm2(sub(point, add(a, mul(ab, v))));
    }
    const DeviceVec3 cp = sub(point, c);
    const double d5 = dot3(ab, cp);
    const double d6 = dot3(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return norm2(cp);
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        const double w = d2 / (d2 - d6);
        return norm2(sub(point, add(a, mul(ac, w))));
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
    {
        const DeviceVec3 bc = sub(c, b);
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return norm2(sub(point, add(b, mul(bc, w))));
    }
    const double denominator = 1.0 / (va + vb + vc);
    const double v = vb * denominator;
    const double w = vc * denominator;
    return norm2(sub(point, add(a, add(mul(ab, v), mul(ac, w)))));
}

__device__ double solid_angle(
    DeviceVec3 point, DeviceVec3 a, DeviceVec3 b, DeviceVec3 c)
{
    const DeviceVec3 pa = sub(a, point);
    const DeviceVec3 pb = sub(b, point);
    const DeviceVec3 pc = sub(c, point);
    const double la = sqrt(norm2(pa));
    const double lb = sqrt(norm2(pb));
    const double lc = sqrt(norm2(pc));
    const double determinant = dot3(pa, cross3(pb, pc));
    const double denominator = la * lb * lc + dot3(pa, pb) * lc +
        dot3(pb, pc) * la + dot3(pc, pa) * lb;
    return 2.0 * atan2(determinant, denominator);
}

__global__ void dense_exact_kernel(
    DeviceVec3 minimum,
    DeviceVec3 extent,
    std::uint32_t nx,
    std::uint32_t ny,
    std::uint32_t nz,
    const DeviceVec3* vertices,
    const std::uint32_t* indices,
    std::size_t triangle_count,
    double* output)
{
    const std::size_t count = static_cast<std::size_t>(nx + 1) * (ny + 1) * (nz + 1);
    const std::size_t linear = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= count) return;
    const std::uint32_t x = static_cast<std::uint32_t>(linear % (nx + 1));
    const std::uint32_t y = static_cast<std::uint32_t>((linear / (nx + 1)) % (ny + 1));
    const std::uint32_t z = static_cast<std::uint32_t>(linear /
        (static_cast<std::size_t>(nx + 1) * (ny + 1)));
    const DeviceVec3 point{
        minimum.x + extent.x * x / nx,
        minimum.y + extent.y * y / ny,
        minimum.z + extent.z * z / nz};
    double nearest = 1.7976931348623157e+308;
    double winding = 0.0;
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle)
    {
        const DeviceVec3 a = vertices[indices[3 * triangle]];
        const DeviceVec3 b = vertices[indices[3 * triangle + 1]];
        const DeviceVec3 c = vertices[indices[3 * triangle + 2]];
        nearest = fmin(nearest, point_triangle_squared(point, a, b, c));
        winding += solid_angle(point, a, b, c);
    }
    const double distance = sqrt(nearest);
    output[linear] = fabs(winding) > 6.28318530717958647692 ? -distance : distance;
}

struct DeviceQuery
{
    double phi, gx, gy, gz, hxy, hxz, hyz;
    std::uint64_t branch_signature;
    std::uint32_t in_domain;
};

__host__ __device__ std::uint64_t hash_combine_device(
    std::uint64_t seed, std::uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed;
}

__global__ void dense_query_kernel(
    DeviceVec3 minimum,
    DeviceVec3 maximum,
    std::uint32_t nx,
    std::uint32_t ny,
    std::uint32_t nz,
    const double* coefficients,
    const DeviceVec3* points,
    std::size_t count,
    DeviceQuery* output)
{
    const std::size_t linear = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (linear >= count) return;
    const DeviceVec3 p = points[linear];
    if (p.x < minimum.x || p.y < minimum.y || p.z < minimum.z ||
        p.x > maximum.x || p.y > maximum.y || p.z > maximum.z)
    {
        output[linear] = {};
        return;
    }
    const DeviceVec3 extent = sub(maximum, minimum);
    const double coordinates[3]{
        (p.x - minimum.x) / extent.x * nx,
        (p.y - minimum.y) / extent.y * ny,
        (p.z - minimum.z) / extent.z * nz};
    const std::uint32_t cell[3]{
        min(nx - 1, static_cast<std::uint32_t>(fmax(0.0, floor(coordinates[0])))),
        min(ny - 1, static_cast<std::uint32_t>(fmax(0.0, floor(coordinates[1])))),
        min(nz - 1, static_cast<std::uint32_t>(fmax(0.0, floor(coordinates[2]))))};
    const double t[3]{coordinates[0] - cell[0], coordinates[1] - cell[1],
        coordinates[2] - cell[2]};
    const std::size_t sx = nx + 1;
    const std::size_t sy = ny + 1;
    double value = 0.0, gx = 0.0, gy = 0.0, gz = 0.0;
    double hxy = 0.0, hxz = 0.0, hyz = 0.0;
    for (std::uint32_t z = 0; z < 2; ++z)
    for (std::uint32_t y = 0; y < 2; ++y)
    for (std::uint32_t x = 0; x < 2; ++x)
    {
        const double coefficient = coefficients[(cell[0] + x) + sx *
            ((cell[1] + y) + sy * (cell[2] + z))];
        const double wx = x ? t[0] : 1.0 - t[0];
        const double wy = y ? t[1] : 1.0 - t[1];
        const double wz = z ? t[2] : 1.0 - t[2];
        value += coefficient * wx * wy * wz;
        gx += coefficient * (x ? 1.0 : -1.0) * wy * wz * nx / extent.x;
        gy += coefficient * wx * (y ? 1.0 : -1.0) * wz * ny / extent.y;
        gz += coefficient * wx * wy * (z ? 1.0 : -1.0) * nz / extent.z;
        hxy += coefficient * (x ? 1.0 : -1.0) * (y ? 1.0 : -1.0) * wz *
            nx * ny / (extent.x * extent.y);
        hxz += coefficient * (x ? 1.0 : -1.0) * wy * (z ? 1.0 : -1.0) *
            nx * nz / (extent.x * extent.z);
        hyz += coefficient * wx * (y ? 1.0 : -1.0) * (z ? 1.0 : -1.0) *
            ny * nz / (extent.y * extent.z);
    }
    const std::uint64_t cell_index = cell[0] + static_cast<std::uint64_t>(nx) *
        (cell[1] + static_cast<std::uint64_t>(ny) * cell[2]);
    output[linear] = {value, gx, gy, gz, hxy, hxz, hyz,
        hash_combine_device(0x44454e5345ull, cell_index), 1};
}

DeviceVec3 device_vec(Vec3 value) { return {value.x, value.y, value.z}; }

} // namespace

bool cuda_backend_available() noexcept
{
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

void build_dense_cuda(AssetData& data, const BuildOptions& options)
{
    if (!cuda_backend_available())
        throw std::runtime_error("no CUDA device is available");
    if (data.info.composition != CompositionPolicy::SeparateAssets ||
        data.info.component_count != 1)
        throw std::invalid_argument("CUDA dense generation currently requires one shell");
    const auto resolution = options.resolution;
    const std::uint64_t point_count64 = static_cast<std::uint64_t>(resolution[0] + 1) *
        (resolution[1] + 1) * (resolution[2] + 1);
    if (point_count64 > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("CUDA dense grid is too large");
    const std::size_t point_count = static_cast<std::size_t>(point_count64);
    std::vector<DeviceVec3> vertices;
    vertices.reserve(data.mesh.vertices.size());
    for (Vec3 value : data.mesh.vertices) vertices.push_back(device_vec(value));
    std::vector<std::uint32_t> indices;
    indices.reserve(3 * data.mesh.triangles.size());
    for (const Triangle& triangle : data.mesh.triangles)
        indices.insert(indices.end(), triangle.vertex.begin(), triangle.vertex.end());

    DeviceBuffer<DeviceVec3> device_vertices(vertices.size());
    DeviceBuffer<std::uint32_t> device_indices(indices.size());
    DeviceBuffer<double> device_output(point_count);
    require_cuda(cudaMemcpy(device_vertices.get(), vertices.data(),
        vertices.size() * sizeof(DeviceVec3), cudaMemcpyHostToDevice), "copy vertices");
    require_cuda(cudaMemcpy(device_indices.get(), indices.data(),
        indices.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice), "copy indices");
    constexpr unsigned threads = 128;
    dense_exact_kernel<<<static_cast<unsigned>((point_count + threads - 1) / threads), threads>>>(
        device_vec(data.info.domain.minimum), device_vec(data.info.domain.extent()),
        resolution[0], resolution[1], resolution[2], device_vertices.get(),
        device_indices.get(), data.mesh.triangles.size(), device_output.get());
    require_cuda(cudaGetLastError(), "launch dense exact kernel");
    data.coefficients.resize(point_count);
    require_cuda(cudaMemcpy(data.coefficients.data(), device_output.get(),
        point_count * sizeof(double), cudaMemcpyDeviceToHost), "copy dense field");
    data.info.resolution = resolution;
    data.info.node_count = static_cast<std::uint64_t>(resolution[0]) *
        resolution[1] * resolution[2];
    data.info.coefficient_count = point_count;
}

bool query_batch_cuda(
    const AssetData& data,
    const Vec3* points,
    std::size_t count,
    QueryResult* out)
{
    if (!cuda_backend_available()) throw std::runtime_error("no CUDA device is available");
    if (data.info.representation != Representation::DenseGrid ||
        data.info.reconstruction != Reconstruction::Trilinear)
        throw std::invalid_argument("CUDA query currently supports dense trilinear assets");
    if (count == 0) return true;
    std::vector<DeviceVec3> packed;
    packed.reserve(count);
    for (std::size_t index = 0; index < count; ++index) packed.push_back(device_vec(points[index]));
    DeviceBuffer<DeviceVec3> device_points(count);
    DeviceBuffer<double> device_coefficients(data.coefficients.size());
    DeviceBuffer<DeviceQuery> device_results(count);
    require_cuda(cudaMemcpy(device_points.get(), packed.data(), count * sizeof(DeviceVec3),
        cudaMemcpyHostToDevice), "copy query points");
    require_cuda(cudaMemcpy(device_coefficients.get(), data.coefficients.data(),
        data.coefficients.size() * sizeof(double), cudaMemcpyHostToDevice), "copy coefficients");
    constexpr unsigned threads = 128;
    dense_query_kernel<<<static_cast<unsigned>((count + threads - 1) / threads), threads>>>(
        device_vec(data.info.domain.minimum), device_vec(data.info.domain.maximum),
        data.info.resolution[0], data.info.resolution[1], data.info.resolution[2],
        device_coefficients.get(), device_points.get(), count, device_results.get());
    require_cuda(cudaGetLastError(), "launch dense query kernel");
    std::vector<DeviceQuery> results(count);
    require_cuda(cudaMemcpy(results.data(), device_results.get(), count * sizeof(DeviceQuery),
        cudaMemcpyDeviceToHost), "copy query results");
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!results[index].in_domain) { out[index] = {}; continue; }
        QueryResult result;
        result.phi = results[index].phi;
        result.raw_gradient = {results[index].gx, results[index].gy, results[index].gz};
        result.hessian[1] = result.hessian[3] = results[index].hxy;
        result.hessian[2] = result.hessian[6] = results[index].hxz;
        result.hessian[5] = result.hessian[7] = results[index].hyz;
        // Cell ownership at an exact face is part of the CPU contract. Obtain
        // only that discrete identifier through the reference path so CUDA
        // FMA/contraction cannot choose the neighboring cell at a tie.
        result.branch_signature = query_asset(data, points[index]).branch_signature;
        const double length = norm(result.raw_gradient);
        result.unit_normal = length > 1.0e-14 ? result.raw_gradient / length : Vec3{};
        result.valid = std::isfinite(result.phi) && std::isfinite(length);
        result.in_domain = true;
        result.has_hessian = true;
        out[index] = result;
    }
    return true;
}

} // namespace nexsdf::detail
