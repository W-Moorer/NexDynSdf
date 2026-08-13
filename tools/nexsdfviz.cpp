#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

enum class Mode
{
    Distance,
    Normal,
    GradientError,
    Depth,
    Error
};

struct Options
{
    std::string asset_path;
    std::string output_path;
    char axis{'z'};
    bool coordinate_set{false};
    double coordinate{0.0};
    std::uint32_t resolution{512};
    Mode mode{Mode::Distance};
    bool range_set{false};
    double range{0.0};
};

using Rgb = std::array<std::uint8_t, 3>;

void usage()
{
    std::cerr
        << "usage: nexsdfviz ASSET.nsdf OUTPUT.ppm [options]\n"
        << "  --axis x|y|z             slice normal (default: z)\n"
        << "  --coordinate X           world coordinate (default: domain center)\n"
        << "  --resolution N           square image size (default: 512)\n"
        << "  --mode distance|normal|gradient-error|depth|error\n"
        << "  --range X                fixed positive color range\n";
}

std::string next(int& index, int count, char** values, const char* option)
{
    if (++index >= count) throw std::invalid_argument(std::string("missing value for ") + option);
    return values[index];
}

std::uint32_t parse_resolution(const std::string& value)
{
    const unsigned long long parsed = std::stoull(value);
    if (parsed < 8 || parsed > 8192)
        throw std::invalid_argument("resolution must be in [8, 8192]");
    return static_cast<std::uint32_t>(parsed);
}

Mode parse_mode(const std::string& value)
{
    if (value == "distance") return Mode::Distance;
    if (value == "normal") return Mode::Normal;
    if (value == "gradient-error") return Mode::GradientError;
    if (value == "depth") return Mode::Depth;
    if (value == "error") return Mode::Error;
    throw std::invalid_argument("unknown visualization mode: " + value);
}

const char* mode_name(Mode mode)
{
    switch (mode)
    {
    case Mode::Distance: return "distance";
    case Mode::Normal: return "normal";
    case Mode::GradientError: return "gradient-error";
    case Mode::Depth: return "depth";
    case Mode::Error: return "error";
    }
    return "unknown";
}

Options parse_options(int argc, char** argv)
{
    if (argc < 3) throw std::invalid_argument("asset and output paths are required");
    Options options;
    options.asset_path = argv[1];
    options.output_path = argv[2];
    for (int i = 3; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "--axis")
        {
            const std::string value = next(i, argc, argv, option.c_str());
            if (value.size() != 1 || (value[0] != 'x' && value[0] != 'y' && value[0] != 'z'))
                throw std::invalid_argument("axis must be x, y, or z");
            options.axis = value[0];
        }
        else if (option == "--coordinate")
        {
            options.coordinate = std::stod(next(i, argc, argv, option.c_str()));
            options.coordinate_set = true;
        }
        else if (option == "--resolution")
        {
            options.resolution = parse_resolution(next(i, argc, argv, option.c_str()));
        }
        else if (option == "--mode")
        {
            options.mode = parse_mode(next(i, argc, argv, option.c_str()));
        }
        else if (option == "--range")
        {
            options.range = std::stod(next(i, argc, argv, option.c_str()));
            if (!std::isfinite(options.range) || options.range <= 0.0)
                throw std::invalid_argument("range must be finite and positive");
            options.range_set = true;
        }
        else if (option == "--help")
        {
            usage();
            std::exit(0);
        }
        else
        {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    return options;
}

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

Rgb mix(Rgb a, Rgb b, double t)
{
    t = clamp01(t);
    Rgb result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>(std::lround(a[i] + (b[i] - a[i]) * t));
    return result;
}

Rgb sequential(double value)
{
    value = clamp01(value);
    constexpr std::array<Rgb, 5> colors{{
        Rgb{68, 1, 84}, Rgb{59, 82, 139}, Rgb{33, 145, 140},
        Rgb{94, 201, 98}, Rgb{253, 231, 37}}};
    const double scaled = value * static_cast<double>(colors.size() - 1);
    const std::size_t segment = std::min(
        colors.size() - 2, static_cast<std::size_t>(std::floor(scaled)));
    return mix(colors[segment], colors[segment + 1], scaled - static_cast<double>(segment));
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 1.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::floor(clamp01(fraction) * static_cast<double>(values.size() - 1)));
    return values[index];
}

std::array<int, 3> plane_axes(char axis)
{
    if (axis == 'x') return {1, 2, 0};
    if (axis == 'y') return {0, 2, 1};
    return {0, 1, 2};
}

std::vector<nexsdf::Vec3> sample_points(
    const nexsdf::Aabb& domain,
    char axis,
    double coordinate,
    std::uint32_t resolution)
{
    const auto axes = plane_axes(axis);
    std::vector<nexsdf::Vec3> points;
    points.resize(static_cast<std::size_t>(resolution) * resolution);
    for (std::uint32_t row = 0; row < resolution; ++row)
    {
        for (std::uint32_t column = 0; column < resolution; ++column)
        {
            nexsdf::Vec3 point{};
            const double u = (static_cast<double>(column) + 0.5) / resolution;
            const double v = (static_cast<double>(resolution - 1 - row) + 0.5) / resolution;
            point[axes[0]] = domain.minimum[axes[0]]
                + u * (domain.maximum[axes[0]] - domain.minimum[axes[0]]);
            point[axes[1]] = domain.minimum[axes[1]]
                + v * (domain.maximum[axes[1]] - domain.minimum[axes[1]]);
            point[axes[2]] = coordinate;
            points[static_cast<std::size_t>(row) * resolution + column] = point;
        }
    }
    return points;
}

double automatic_range(const std::vector<nexsdf::QueryResult>& samples, Mode mode)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples)
    {
        if (!sample.valid || !sample.in_domain) continue;
        switch (mode)
        {
        case Mode::Distance: values.push_back(std::abs(sample.phi)); break;
        case Mode::GradientError:
            values.push_back(std::abs(nexsdf::norm(sample.raw_gradient) - 1.0));
            break;
        case Mode::Error:
            if (sample.has_measured_error) values.push_back(sample.measured_leaf_error);
            break;
        case Mode::Depth: values.push_back(static_cast<double>(sample.cell_depth)); break;
        case Mode::Normal: break;
        }
    }
    const double result = percentile(std::move(values), 0.98);
    return std::max(result, std::numeric_limits<double>::epsilon());
}

Rgb colorize(const nexsdf::QueryResult& sample, Mode mode, double range)
{
    if (!sample.valid || !sample.in_domain) return {35, 38, 47};
    switch (mode)
    {
    case Mode::Distance:
    {
        const double t = std::pow(clamp01(std::abs(sample.phi) / range), 0.65);
        const Rgb neutral{245, 247, 250};
        return sample.phi < 0.0
            ? mix(neutral, Rgb{49, 103, 178}, t)
            : mix(neutral, Rgb{220, 73, 37}, t);
    }
    case Mode::Normal:
        return {
            static_cast<std::uint8_t>(std::lround(255.0 * clamp01(0.5 * (sample.unit_normal.x + 1.0)))),
            static_cast<std::uint8_t>(std::lround(255.0 * clamp01(0.5 * (sample.unit_normal.y + 1.0)))),
            static_cast<std::uint8_t>(std::lround(255.0 * clamp01(0.5 * (sample.unit_normal.z + 1.0))))};
    case Mode::GradientError:
        return sequential(std::abs(nexsdf::norm(sample.raw_gradient) - 1.0) / range);
    case Mode::Depth:
        return sequential(static_cast<double>(sample.cell_depth) / range);
    case Mode::Error:
        if (!sample.has_measured_error) return {70, 70, 76};
        return sequential(sample.measured_leaf_error / range);
    }
    return {0, 0, 0};
}

void overlay_zero_contour(
    const std::vector<nexsdf::QueryResult>& samples,
    std::uint32_t resolution,
    std::vector<Rgb>& pixels)
{
    constexpr Rgb contour{15, 23, 42};
    for (std::uint32_t row = 0; row < resolution; ++row)
    {
        for (std::uint32_t column = 0; column < resolution; ++column)
        {
            const std::size_t index = static_cast<std::size_t>(row) * resolution + column;
            const auto& sample = samples[index];
            if (!sample.valid) continue;
            bool crossing = false;
            if (column + 1 < resolution)
            {
                const auto& neighbor = samples[index + 1];
                crossing = neighbor.valid && std::signbit(sample.phi) != std::signbit(neighbor.phi);
            }
            if (!crossing && row + 1 < resolution)
            {
                const auto& neighbor = samples[index + resolution];
                crossing = neighbor.valid && std::signbit(sample.phi) != std::signbit(neighbor.phi);
            }
            if (crossing) pixels[index] = contour;
        }
    }
}

void write_ppm(
    const std::string& path,
    std::uint32_t resolution,
    const std::vector<Rgb>& pixels,
    const Options& options,
    double range)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot open output image: " + path);
    output << "P6\n"
           << "# NexDynSdf mode=" << mode_name(options.mode)
           << " axis=" << options.axis
           << " coordinate=" << std::setprecision(17) << options.coordinate
           << " range=" << range << "\n"
           << resolution << ' ' << resolution << "\n255\n";
    for (const auto& pixel : pixels)
        output.write(reinterpret_cast<const char*>(pixel.data()), 3);
    if (!output) throw std::runtime_error("failed to write output image: " + path);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--help")
    {
        usage();
        return 0;
    }
    try
    {
        Options options = parse_options(argc, argv);
        const nexsdf::Asset asset = nexsdf::Asset::load(options.asset_path);
        const auto& domain = asset.info().domain;
        const int fixed_axis = plane_axes(options.axis)[2];
        if (!options.coordinate_set)
            options.coordinate = 0.5 * (domain.minimum[fixed_axis] + domain.maximum[fixed_axis]);
        if (!std::isfinite(options.coordinate)
            || options.coordinate < domain.minimum[fixed_axis]
            || options.coordinate > domain.maximum[fixed_axis])
            throw std::out_of_range("slice coordinate is outside the asset domain");

        const std::vector<nexsdf::Vec3> points = sample_points(
            domain, options.axis, options.coordinate, options.resolution);
        std::vector<nexsdf::QueryResult> samples(points.size());
        asset.query_batch(points.data(), points.size(), samples.data());
        const double range = options.range_set
            ? options.range : automatic_range(samples, options.mode);

        std::vector<Rgb> pixels;
        pixels.reserve(samples.size());
        std::size_t valid_count = 0;
        for (const auto& sample : samples)
        {
            if (sample.valid && sample.in_domain) ++valid_count;
            pixels.push_back(colorize(sample, options.mode, range));
        }
        overlay_zero_contour(samples, options.resolution, pixels);
        write_ppm(options.output_path, options.resolution, pixels, options, range);
        std::cout << "wrote " << options.output_path
                  << " mode=" << mode_name(options.mode)
                  << " axis=" << options.axis
                  << " coordinate=" << std::setprecision(17) << options.coordinate
                  << " resolution=" << options.resolution
                  << " range=" << range
                  << " valid_samples=" << valid_count << '/' << samples.size() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nexsdfviz: " << error.what() << '\n';
        return 1;
    }
}
