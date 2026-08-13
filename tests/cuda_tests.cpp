#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{

nexsdf::SurfaceMesh cube_mesh()
{
    nexsdf::SurfaceMesh mesh;
    mesh.vertices = {
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
        {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}};
    const std::uint32_t indices[][3] = {
        {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
        {0,1,5}, {0,5,4}, {3,7,6}, {3,6,2},
        {0,4,7}, {0,7,3}, {1,2,6}, {1,6,5}};
    for (std::uint32_t face = 0; face < 12; ++face)
    {
        nexsdf::Triangle triangle;
        triangle.vertex = {indices[face][0], indices[face][1], indices[face][2]};
        triangle.face_id = face;
        mesh.triangles.push_back(triangle);
    }
    return mesh;
}

} // namespace

int main()
{
    if (!nexsdf::is_cuda_backend_available())
    {
        std::cerr << "CUDA-enabled build has no available device\n";
        return 1;
    }
    nexsdf::BuildOptions cpu;
    cpu.resolution = {12, 12, 12};
    cpu.representation = nexsdf::Representation::DenseGrid;
    cpu.reconstruction = nexsdf::Reconstruction::Trilinear;
    nexsdf::BuildOptions gpu = cpu;
    gpu.backend = nexsdf::ComputeBackend::CudaExperimental;
    const nexsdf::Asset cpu_asset = nexsdf::build(cube_mesh(), cpu);
    const nexsdf::Asset gpu_asset = nexsdf::build(cube_mesh(), gpu);
    if (gpu_asset.info().build_backend != nexsdf::ComputeBackend::CudaExperimental)
    {
        std::cerr << "CUDA build provenance is missing\n";
        return 1;
    }
    const std::filesystem::path roundtrip_path =
        std::filesystem::temp_directory_path() / "nexsdf-cuda-roundtrip.nsdf";
    gpu_asset.save(roundtrip_path.string());
    const nexsdf::Asset roundtrip_asset = nexsdf::Asset::load(roundtrip_path.string());
    if (roundtrip_asset.info().build_backend !=
        nexsdf::ComputeBackend::CudaExperimental)
    {
        std::cerr << "CUDA NSDF round trip lost build provenance\n";
        return 1;
    }

    std::vector<nexsdf::Vec3> points;
    for (int index = 0; index < 1024; ++index)
        points.push_back({
            -1.15 + 2.3 * ((index * 17) % 1024) / 1023.0,
            -1.15 + 2.3 * ((index * 43) % 1024) / 1023.0,
            -1.15 + 2.3 * ((index * 97) % 1024) / 1023.0});
    std::vector<nexsdf::QueryResult> reference(points.size());
    std::vector<nexsdf::QueryResult> generated(points.size());
    std::vector<nexsdf::QueryResult> roundtrip(points.size());
    std::vector<nexsdf::QueryResult> gpu_query(points.size());
    nexsdf::BatchQueryOptions scalar;
    scalar.backend = nexsdf::BatchBackend::Scalar;
    cpu_asset.query_batch(points.data(), points.size(), reference.data(), scalar);
    gpu_asset.query_batch(points.data(), points.size(), generated.data(), scalar);
    roundtrip_asset.query_batch(points.data(), points.size(), roundtrip.data(), scalar);
    nexsdf::BatchQueryOptions cuda;
    cuda.backend = nexsdf::BatchBackend::CudaExperimental;
    gpu_asset.query_batch(points.data(), points.size(), gpu_query.data(), cuda);
    double generation_phi = 0.0;
    double generation_gradient = 0.0;
    double query_phi = 0.0;
    double query_gradient = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        generation_phi = std::max(generation_phi,
            std::abs(generated[index].phi - reference[index].phi));
        generation_gradient = std::max(generation_gradient,
            nexsdf::norm(generated[index].raw_gradient - reference[index].raw_gradient));
        if (roundtrip[index].phi != generated[index].phi ||
            nexsdf::norm(roundtrip[index].raw_gradient -
                generated[index].raw_gradient) != 0.0 ||
            roundtrip[index].branch_signature != generated[index].branch_signature)
        {
            std::cerr << "CUDA-generated NSDF changed during round trip at "
                      << index << '\n';
            return 1;
        }
        query_phi = std::max(query_phi,
            std::abs(gpu_query[index].phi - generated[index].phi));
        query_gradient = std::max(query_gradient,
            nexsdf::norm(gpu_query[index].raw_gradient - generated[index].raw_gradient));
        if (gpu_query[index].branch_signature != generated[index].branch_signature ||
            !gpu_query[index].has_hessian ||
            std::abs(gpu_query[index].hessian[1] - generated[index].hessian[1]) > 2.0e-11 ||
            std::abs(gpu_query[index].hessian[2] - generated[index].hessian[2]) > 2.0e-11 ||
            std::abs(gpu_query[index].hessian[5] - generated[index].hessian[5]) > 2.0e-11)
        {
            std::cerr << "CUDA query metadata or Hessian differs at " << index
                      << " branch=" << gpu_query[index].branch_signature << '/'
                      << generated[index].branch_signature
                      << " hxy=" << gpu_query[index].hessian[1] << '/'
                      << generated[index].hessian[1]
                      << " hxz=" << gpu_query[index].hessian[2] << '/'
                      << generated[index].hessian[2]
                      << " hyz=" << gpu_query[index].hessian[5] << '/'
                      << generated[index].hessian[5] << '\n';
            return 1;
        }
    }
    std::cout << "cuda_generation_phi_max=" << generation_phi
              << " cuda_generation_gradient_max=" << generation_gradient
              << " cuda_query_phi_max=" << query_phi
              << " cuda_query_gradient_max=" << query_gradient << '\n';
    if (generation_phi > 2.0e-12 || generation_gradient > 2.0e-11 ||
        query_phi > 2.0e-12 || query_gradient > 2.0e-11)
    {
        std::cerr << "CUDA comparison exceeded the explicit double-precision budget\n";
        return 1;
    }
    std::error_code remove_error;
    std::filesystem::remove(roundtrip_path, remove_error);
    return 0;
}
