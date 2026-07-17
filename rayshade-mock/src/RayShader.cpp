// RayShader mock implementation.
//
// Everything here is a deterministic placeholder. Values are derived by
// hashing their inputs so results are stable across runs given the same
// seed. Nothing is computed "correctly"; this exists only to exercise the
// call sequence of dependent code.
//
// SPDX-License-Identifier: MIT
#include "rayshade/RayShader.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>

namespace rayshade {

namespace {

// splitmix64 finalizer — cheap, stable, good enough for a mock.
uint64_t mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t hashCombine(uint64_t seed, uint64_t value)
{
    return mix(seed ^ mix(value));
}

uint64_t hashString(uint64_t seed, const std::string &s)
{
    uint64_t h = seed;
    for (unsigned char c : s)
        h = hashCombine(h, c);
    return h;
}

// Map a hash to a stable double in [0, 1).
double unitFromHash(uint64_t h)
{
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

// Mock content sizing.
constexpr std::size_t kModelCount = 2;
constexpr std::size_t kRegionsPerModel = 8;
constexpr std::size_t kChannelsPerModel = 2;
constexpr std::size_t kElementsPerChannel = 3;
constexpr std::size_t kRayCount = 3;
constexpr std::size_t kParamsPerRay = 2;

} // namespace

class RayShader::Impl {
public:
    explicit Impl(std::string program) : program_name(std::move(program))
    {
        log("RayShader created: " + program_name);
    }

    struct Model {
        StringList region_names;
        StringList channel_names;
        StringList channel_types;
        std::vector<StringList> channel_elements; // [channel][element]
        Box bounds;
        std::optional<Vec3> anchor;
        std::vector<AngleWindow> masks;
        double base_offset = 0.0;
        double sink_depth = 0.0;
    };

    struct RayPreset {
        std::string name;
        std::string type;
        bool collimated = true;
        StringList param_names;
        std::vector<std::string> param_values;
        std::vector<std::string> param_units;
        std::vector<bool> param_editable;
        std::vector<std::string> param_limits;
        std::set<std::size_t> exclusions;
    };

    struct QueuedRay {
        std::size_t ray_index = 0;
        Vec3 origin;
        Vec3 direction;
        double time = 0.0;
        uint64_t hash = 0;
    };

    std::string program_name;
    std::string resource_root;
    uint64_t seed = 0;
    std::string expiry = "none";
    std::string bundle_version = "0.0.0";
    bool loaded = false;
    bool evaluated = false;
    bool automatic_base = false;
    bool height_field = false;

    StringList model_names;
    StringList ray_names;
    StringList instance_names;
    std::vector<Model> models;
    std::vector<RayPreset> rays;
    std::vector<std::size_t> instance_model; // instance -> model index
    std::vector<QueuedRay> queue;
    uint64_t queue_hash = 0;

    mutable std::map<std::string, std::string> options;
    std::map<std::string, double> uniforms;
    std::set<std::size_t> empty_exclusions;
    std::vector<ResourceInfo> resources;
    std::string debug_log;

    void log(const std::string &line) { debug_log += line + "\n"; }

    void requireLoaded() const
    {
        if (!loaded)
            throw std::runtime_error("RayShader: no bundle loaded");
    }

    void checkModel(std::size_t m) const
    {
        if (m >= models.size())
            throw std::out_of_range("RayShader: model index out of range");
    }

    void checkRay(std::size_t r) const
    {
        if (r >= rays.size())
            throw std::out_of_range("RayShader: ray index out of range");
    }

    void checkInstance(std::size_t i) const
    {
        if (i >= instance_model.size())
            throw std::out_of_range("RayShader: instance index out of range");
    }

    // Build a fixed, deterministic mock world.
    void buildMockWorld()
    {
        model_names.clear();
        models.clear();
        ray_names.clear();
        rays.clear();
        instance_names.clear();
        instance_model.clear();
        queue.clear();
        resources.clear();

        for (std::size_t m = 0; m < kModelCount; ++m) {
            Model model;
            const std::string mname = "model_" + std::to_string(m);
            model_names.push_back(mname);

            for (std::size_t r = 0; r < kRegionsPerModel; ++r)
                model.region_names.push_back(mname + ".region_" + std::to_string(r));

            for (std::size_t c = 0; c < kChannelsPerModel; ++c) {
                model.channel_names.push_back("channel_" + std::to_string(c));
                model.channel_types.push_back(c == 0 ? "scalar" : "mask");
                StringList elems;
                for (std::size_t e = 0; e < kElementsPerChannel; ++e)
                    elems.push_back("element_" + std::to_string(e));
                model.channel_elements.push_back(std::move(elems));
            }

            const double extent = 100.0 + 10.0 * static_cast<double>(m);
            model.bounds = Box{Vec3{-extent, -extent, -extent}, Vec3{extent, extent, extent}};
            model.anchor = Vec3{0.0, 0.0, 0.0};
            model.masks.clear();
            model.base_offset = 0.0;
            model.sink_depth = 0.0;
            models.push_back(std::move(model));

            resources.push_back(ResourceInfo{mname, 1024 * (m + 1)});
        }

        for (std::size_t r = 0; r < kRayCount; ++r) {
            RayPreset ray;
            ray.name = "ray_" + std::to_string(r);
            ray.type = (r % 2 == 0) ? "collimated" : "scattered";
            ray.collimated = (r % 2 == 0);
            for (std::size_t p = 0; p < kParamsPerRay; ++p) {
                ray.param_names.push_back("param_" + std::to_string(p));
                ray.param_values.push_back("1.0");
                ray.param_units.push_back("unit");
                ray.param_editable.push_back(true);
                ray.param_limits.push_back("0.0:10.0");
            }
            ray_names.push_back(ray.name);
            rays.push_back(std::move(ray));
        }

        loaded = true;
    }
};

// --- lifecycle ---------------------------------------------------------------

RayShader::RayShader(const std::string &program_name)
    : impl(std::make_unique<Impl>(program_name))
{
}

RayShader::~RayShader() = default;

void RayShader::setResourceRoot(const std::string &resource_path)
{
    impl->resource_root = resource_path;
    impl->log("resource root: " + resource_path);
}

void RayShader::setSeed(uint64_t seed)
{
    impl->seed = seed;
}

std::string RayShader::getVersion() const
{
    return "0.1.0-mock";
}

// --- bundle ------------------------------------------------------------------

void RayShader::createBundle(const std::string &, const std::string &, const std::string &,
    const std::string &expiry_date, const std::string &)
{
    impl->expiry = expiry_date;
    impl->log("createBundle");
}

void RayShader::saveBundle(const std::string &, const std::string &, const std::string &)
{
    impl->log("saveBundle");
}

void RayShader::loadBundle(const std::string &bundle_filename, const std::string &)
{
    impl->bundle_version = "1.0.0";
    impl->buildMockWorld();
    impl->log("loadBundle: " + bundle_filename);
}

void RayShader::loadBundle(std::istream &, const std::string &)
{
    impl->bundle_version = "1.0.0";
    impl->buildMockWorld();
    impl->log("loadBundle(stream)");
}

const std::string &RayShader::getExpiry() const { return impl->expiry; }
const std::string &RayShader::getBundleVersion() const { return impl->bundle_version; }

std::string RayShader::getOption(const std::string &option) const
{
    auto it = impl->options.find(option);
    if (it == impl->options.end())
        it = impl->options.emplace(option, "default").first;
    return it->second;
}

void RayShader::setOption(const std::string &option, const std::string &selection)
{
    impl->options[option] = selection;
}

double RayShader::getUniform(const std::string &name) const
{
    auto it = impl->uniforms.find(name);
    return it == impl->uniforms.end() ? 0.0 : it->second;
}

void RayShader::setUniform(const std::string &name, double value)
{
    impl->uniforms[name] = value;
}

// --- models ------------------------------------------------------------------

const StringList &RayShader::getModelNames() const { return impl->model_names; }

std::size_t RayShader::getModelIndex(const std::string &model_name) const
{
    for (std::size_t i = 0; i < impl->model_names.size(); ++i)
        if (impl->model_names[i] == model_name)
            return i;
    throw std::out_of_range("RayShader: unknown model '" + model_name + "'");
}

std::size_t RayShader::getModelIndex(std::size_t instance_index) const
{
    impl->checkInstance(instance_index);
    return impl->instance_model[instance_index];
}

const Box &RayShader::getBounds(std::size_t model_index) const
{
    impl->checkModel(model_index);
    return impl->models[model_index].bounds;
}

double RayShader::getBasePlaneOffset(std::size_t model_index) const
{
    impl->checkModel(model_index);
    return impl->models[model_index].base_offset;
}

double RayShader::getSinkDepth(std::size_t model_index) const
{
    impl->checkModel(model_index);
    return impl->models[model_index].sink_depth;
}

const std::optional<Vec3> &RayShader::getAnchorPoint(std::size_t model_index) const
{
    impl->checkModel(model_index);
    return impl->models[model_index].anchor;
}

const std::string &RayShader::getRegionName(std::size_t model_index, std::size_t region_id) const
{
    impl->checkModel(model_index);
    const auto &names = impl->models[model_index].region_names;
    if (region_id >= names.size())
        throw std::out_of_range("RayShader: region id out of range");
    return names[region_id];
}

const StringList &RayShader::getChannels(std::size_t model_index) const
{
    impl->checkModel(model_index);
    return impl->models[model_index].channel_names;
}

const std::string &RayShader::getChannelType(std::size_t model_index, std::size_t channel_index) const
{
    impl->checkModel(model_index);
    const auto &types = impl->models[model_index].channel_types;
    if (channel_index >= types.size())
        throw std::out_of_range("RayShader: channel index out of range");
    return types[channel_index];
}

const StringList &RayShader::getChannelElements(std::size_t model_index,
    std::size_t channel_index) const
{
    impl->checkModel(model_index);
    const auto &elems = impl->models[model_index].channel_elements;
    if (channel_index >= elems.size())
        throw std::out_of_range("RayShader: channel index out of range");
    return elems[channel_index];
}

const std::vector<RayShader::AngleWindow> &RayShader::getAngleMasks(std::size_t model_index) const
{
    impl->checkModel(model_index);
    return impl->models[model_index].masks;
}

// --- ray presets -------------------------------------------------------------

const StringList &RayShader::getRayNames() const { return impl->ray_names; }

const std::string &RayShader::getRayType(std::size_t ray_index) const
{
    impl->checkRay(ray_index);
    return impl->rays[ray_index].type;
}

bool RayShader::getRayIsCollimated(std::size_t ray_index) const
{
    impl->checkRay(ray_index);
    return impl->rays[ray_index].collimated;
}

void RayShader::clearQueuedRays()
{
    impl->queue.clear();
    impl->queue_hash = 0;
    impl->evaluated = false;
}

const StringList &RayShader::getRayParamNames(std::size_t ray_index) const
{
    impl->checkRay(ray_index);
    return impl->rays[ray_index].param_names;
}

const std::string &RayShader::getRayParamValue(std::size_t ray_index,
    std::size_t parameter_index) const
{
    impl->checkRay(ray_index);
    const auto &vals = impl->rays[ray_index].param_values;
    if (parameter_index >= vals.size())
        throw std::out_of_range("RayShader: parameter index out of range");
    return vals[parameter_index];
}

const std::string &RayShader::getRayParamUnits(std::size_t ray_index,
    std::size_t parameter_index) const
{
    impl->checkRay(ray_index);
    const auto &units = impl->rays[ray_index].param_units;
    if (parameter_index >= units.size())
        throw std::out_of_range("RayShader: parameter index out of range");
    return units[parameter_index];
}

bool RayShader::isRayParamEditable(std::size_t ray_index, std::size_t parameter_index) const
{
    impl->checkRay(ray_index);
    const auto &ed = impl->rays[ray_index].param_editable;
    if (parameter_index >= ed.size())
        throw std::out_of_range("RayShader: parameter index out of range");
    return ed[parameter_index];
}

const std::string &RayShader::getRayParamLimits(std::size_t ray_index,
    std::size_t parameter_index) const
{
    impl->checkRay(ray_index);
    const auto &lim = impl->rays[ray_index].param_limits;
    if (parameter_index >= lim.size())
        throw std::out_of_range("RayShader: parameter index out of range");
    return lim[parameter_index];
}

void RayShader::setRayParam(std::size_t ray_index, std::size_t parameter_index,
    const std::string &value)
{
    impl->checkRay(ray_index);
    auto &vals = impl->rays[ray_index].param_values;
    if (parameter_index >= vals.size())
        throw std::out_of_range("RayShader: parameter index out of range");
    vals[parameter_index] = value;
}

const std::set<std::size_t> &RayShader::getRayExclusions(std::size_t ray_index) const
{
    impl->checkRay(ray_index);
    return impl->rays[ray_index].exclusions;
}

std::size_t RayShader::copyRay(std::size_t ray_index, const std::string &new_name)
{
    impl->checkRay(ray_index);
    Impl::RayPreset copy = impl->rays[ray_index];
    copy.name = new_name;
    impl->rays.push_back(std::move(copy));
    impl->ray_names.push_back(new_name);
    return impl->rays.size() - 1;
}

std::size_t RayShader::importRay(const std::string &, const std::string &, const std::string &,
    bool is_collimated, const std::string &ray_name)
{
    Impl::RayPreset ray;
    ray.name = ray_name;
    ray.type = is_collimated ? "collimated" : "scattered";
    ray.collimated = is_collimated;
    impl->rays.push_back(std::move(ray));
    impl->ray_names.push_back(ray_name);
    return impl->rays.size() - 1;
}

void RayShader::replaceProfile(std::size_t ray_index, std::istream &, std::size_t)
{
    impl->checkRay(ray_index);
    impl->log("replaceProfile: " + std::to_string(ray_index));
}

void RayShader::appendProfile(std::size_t ray_index, std::istream &, std::size_t)
{
    impl->checkRay(ray_index);
    impl->log("appendProfile: " + std::to_string(ray_index));
}

// --- geometry probe ----------------------------------------------------------

std::vector<RayShader::Sample> RayShader::probeRay(std::size_t model_index, const Ray &ray)
{
    impl->requireLoaded();
    impl->checkModel(model_index);

    uint64_t h = impl->seed;
    h = hashCombine(h, model_index);
    for (double v : {ray.origin.x, ray.origin.y, ray.origin.z, ray.direction.x, ray.direction.y,
             ray.direction.z}) {
        uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double is 64-bit");
        std::memcpy(&bits, &v, sizeof(bits));
        h = hashCombine(h, bits);
    }

    const std::size_t region_count = impl->models[model_index].region_names.size();
    const std::size_t hit_count = 1 + static_cast<std::size_t>(mix(h) % 3); // 1..3 hits

    std::vector<Sample> out;
    out.reserve(hit_count);
    for (std::size_t i = 0; i < hit_count; ++i) {
        const uint64_t hi = hashCombine(h, i + 1);
        const double t = static_cast<double>(i + 1) * (1.0 + unitFromHash(hi));
        Sample s;
        s.entry = Vec3{ray.origin.x + t * ray.direction.x, ray.origin.y + t * ray.direction.y,
            ray.origin.z + t * ray.direction.z};
        s.region_id = region_count ? static_cast<std::size_t>(mix(hi) % region_count) : Sample::UNSET_ID;
        out.push_back(s);
    }
    return out;
}

// --- instances ---------------------------------------------------------------

bool RayShader::usesAutomaticBase() const { return impl->automatic_base; }

void RayShader::setAutomaticBaseHeight(double)
{
    impl->automatic_base = true;
}

bool RayShader::usesHeightField() const { return impl->height_field; }

void RayShader::setHeightField(const std::string &)
{
    impl->height_field = true;
}

std::size_t RayShader::addInstance(std::size_t model_index, const std::string &instance_name)
{
    impl->requireLoaded();
    impl->checkModel(model_index);
    impl->instance_model.push_back(model_index);
    impl->instance_names.push_back(instance_name);
    return impl->instance_model.size() - 1;
}

void RayShader::moveInstance(std::size_t instance_index, const Vec3 &, RotationOrder, const Vec3 *,
    const double *, const double *, const double *)
{
    impl->checkInstance(instance_index);
    impl->log("moveInstance: " + std::to_string(instance_index));
}

Vec3 RayShader::getSceneCoordinates(std::size_t instance_index, const Vec3 &local_point)
{
    impl->checkInstance(instance_index);
    return local_point; // identity transform in the mock
}

Vec3 RayShader::getLocalCoordinates(std::size_t instance_index, const Vec3 &scene_point)
{
    impl->checkInstance(instance_index);
    return scene_point;
}

Vec3 RayShader::getSceneDirection(std::size_t instance_index, const Vec3 &local_dir)
{
    impl->checkInstance(instance_index);
    return local_dir;
}

Vec3 RayShader::getLocalDirection(std::size_t instance_index, const Vec3 &scene_dir)
{
    impl->checkInstance(instance_index);
    return scene_dir;
}

const StringList &RayShader::getInstanceNames() const { return impl->instance_names; }

// --- queue / evaluate --------------------------------------------------------

void RayShader::queueRay(std::size_t ray_index, const Vec3 &origin, const Angles &direction,
    const std::optional<double> &speed, const std::optional<double> &range,
    const std::optional<uint64_t> &seed, double time)
{
    // Convert yaw/pitch (degrees) to a direction vector, then defer.
    const double deg2rad = 3.14159265358979323846 / 180.0;
    const double yaw = direction.yaw * deg2rad;
    const double pitch = direction.pitch * deg2rad;
    Vec3 dir{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    queueRay(ray_index, origin, dir, speed, range, seed, time);
}

void RayShader::queueRay(std::size_t ray_index, const Vec3 &origin, const Vec3 &direction,
    const std::optional<double> &speed, const std::optional<double> &range,
    const std::optional<uint64_t> &seed, double time)
{
    impl->requireLoaded();
    impl->checkRay(ray_index);

    Impl::QueuedRay q;
    q.ray_index = ray_index;
    q.origin = origin;
    q.direction = direction;
    q.time = time;

    uint64_t h = impl->seed;
    h = hashCombine(h, ray_index);
    if (seed)
        h = hashCombine(h, *seed);
    if (speed) {
        uint64_t bits = 0;
        double v = *speed;
        std::memcpy(&bits, &v, sizeof(bits));
        h = hashCombine(h, bits);
    }
    if (range) {
        uint64_t bits = 0;
        double v = *range;
        std::memcpy(&bits, &v, sizeof(bits));
        h = hashCombine(h, bits);
    }
    for (double v : {origin.x, origin.y, origin.z, direction.x, direction.y, direction.z}) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        h = hashCombine(h, bits);
    }
    q.hash = h;

    impl->queue.push_back(q);
    impl->queue_hash = hashCombine(impl->queue_hash, h);
    impl->evaluated = false;
}

void RayShader::evaluateQueuedRays()
{
    impl->requireLoaded();
    impl->evaluated = true;
    impl->log("evaluateQueuedRays: " + std::to_string(impl->queue.size()));
}

std::string RayShader::getViewSpec(std::size_t queued_ray_index) const
{
    if (queued_ray_index >= impl->queue.size())
        throw std::out_of_range("RayShader: queued ray index out of range");
    const auto &q = impl->queue[queued_ray_index];
    std::ostringstream ss;
    ss << "view origin " << q.origin.x << " " << q.origin.y << " " << q.origin.z << " direction "
       << q.direction.x << " " << q.direction.y << " " << q.direction.z;
    return ss.str();
}

std::string RayShader::getSceneSpec() const
{
    std::ostringstream ss;
    ss << "scene instances " << impl->instance_model.size();
    return ss.str();
}

double RayShader::evaluateShadingParam(std::size_t instance_index, std::size_t channel_index,
    std::size_t element_index) const
{
    impl->requireLoaded();
    impl->checkInstance(instance_index);
    const std::size_t model = impl->instance_model[instance_index];
    if (channel_index >= impl->models[model].channel_elements.size())
        throw std::out_of_range("RayShader: channel index out of range");
    if (element_index >= impl->models[model].channel_elements[channel_index].size())
        throw std::out_of_range("RayShader: element index out of range");

    uint64_t h = impl->seed;
    h = hashCombine(h, impl->queue_hash);
    h = hashCombine(h, instance_index);
    h = hashCombine(h, channel_index);
    h = hashCombine(h, element_index);
    return unitFromHash(mix(h)); // stable value in [0, 1)
}

void RayShader::resetSamples()
{
    impl->queue.clear();
    impl->queue_hash = 0;
    impl->evaluated = false;
}

void RayShader::resetScene()
{
    impl->instance_model.clear();
    impl->instance_names.clear();
    resetSamples();
}

void RayShader::render(const std::string &filename, const Vec3 &, const Vec3 &, double, double,
    double, double)
{
    // Write a tiny placeholder image: each pixel column tinted by a stable
    // per-column hash. Not meaningful; just proves the entry point works.
    std::FILE *f = std::fopen(filename.c_str(), "wb");
    if (!f)
        throw std::runtime_error("RayShader: cannot open '" + filename + "'");
    const int w = 16, h = 16;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint64_t c = mix(hashCombine(impl->seed, static_cast<uint64_t>(x)));
            const unsigned char rgb[3] = {static_cast<unsigned char>(c & 0xff),
                static_cast<unsigned char>((c >> 8) & 0xff),
                static_cast<unsigned char>((c >> 16) & 0xff)};
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    impl->log("render: " + filename);
}

std::vector<ResourceInfo> RayShader::getResourceInfo() const { return impl->resources; }

void RayShader::testFault(int type, const std::string &message, const std::string &detail)
{
    throw std::runtime_error("fault(" + std::to_string(type) + "): " + message + " [" + detail + "]");
}

std::string RayShader::getDebugLog() const { return impl->debug_log; }

} // namespace rayshade
