#include "nexsdf/nexsdf.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 5)
    {
        std::cerr << "usage: nexsdfinspect ASSET.nsdf [x y z]\n";
        return 2;
    }
    try
    {
        const nexsdf::Asset asset = nexsdf::Asset::load(argv[1]);
        const auto& info = asset.info();
        std::cout << std::setprecision(17)
                  << "format=" << info.format_major << '.' << info.format_minor << '\n'
                  << "representation=" << static_cast<std::uint32_t>(info.representation) << '\n'
                  << "reconstruction=" << static_cast<std::uint32_t>(info.reconstruction) << '\n'
                  << "domain_min=" << info.domain.minimum.x << ' ' << info.domain.minimum.y << ' ' << info.domain.minimum.z << '\n'
                  << "domain_max=" << info.domain.maximum.x << ' ' << info.domain.maximum.y << ' ' << info.domain.maximum.z << '\n'
                  << "nodes=" << info.node_count << " coefficients=" << info.coefficient_count
                  << " triangles=" << info.triangle_count << '\n';
        if (argc == 5)
        {
            const nexsdf::Vec3 point{std::stod(argv[2]), std::stod(argv[3]), std::stod(argv[4])};
            const nexsdf::QueryResult result = asset.query(point);
            if (!result.in_domain) throw std::out_of_range("query point is outside the asset domain");
            std::cout << "phi=" << result.phi << '\n'
                      << "raw_gradient=" << result.raw_gradient.x << ' ' << result.raw_gradient.y << ' ' << result.raw_gradient.z << '\n'
                      << "unit_normal=" << result.unit_normal.x << ' ' << result.unit_normal.y << ' ' << result.unit_normal.z << '\n'
                      << "measured_leaf_error=";
            if (result.has_measured_error) std::cout << result.measured_leaf_error;
            else std::cout << "n/a";
            std::cout << '\n'
                      << "exact=" << (result.exact ? 1 : 0) << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nexsdfinspect: " << error.what() << '\n';
        return 1;
    }
}
