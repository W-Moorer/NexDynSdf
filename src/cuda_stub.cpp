#include "internal.hpp"

#include <stdexcept>

namespace nexsdf::detail
{
bool cuda_backend_available() noexcept { return false; }

void build_dense_cuda(AssetData&, const BuildOptions&)
{
    throw std::invalid_argument(
        "CUDA backend was not enabled when NexDynSdf was built");
}

bool query_batch_cuda(const AssetData&, const Vec3*, std::size_t, QueryResult*)
{
    throw std::invalid_argument(
        "CUDA backend was not enabled when NexDynSdf was built");
}
}
