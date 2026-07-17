// RayShader — a small mock ray-shading pipeline.
//
// This is a stand-in development harness with a fixed, deterministic
// implementation. It performs no real shading; it returns stable,
// hashed placeholder values so callers can be developed and tested
// against a stable interface. Swap the implementation later without
// changing this header.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rayshade {

using StringList = std::vector<std::string>;

// Minimal geometry types (self-contained; no external math dependency).
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// A ray to probe: an origin and a (not necessarily normalized) direction.
struct Ray {
    Vec3 origin;
    Vec3 direction;
};

// A yaw/pitch orientation, in degrees.
struct Angles {
    double yaw = 0.0;
    double pitch = 0.0;
};

// An axis-aligned bounding box.
struct Box {
    Vec3 min;
    Vec3 max;
};

// Metadata about a loaded resource entry.
struct ResourceInfo {
    std::string name;
    std::size_t bytes = 0;
};

// The RayShader owns a loaded bundle of models, ray presets, and named
// output channels, and lets callers probe geometry and evaluate shading.
class RayShader {
public:
    explicit RayShader(const std::string &program_name);
    ~RayShader();

    RayShader(const RayShader &) = delete;
    RayShader &operator=(const RayShader &) = delete;

    void setResourceRoot(const std::string &resource_path);
    void setSeed(uint64_t seed);
    std::string getVersion() const;

    // Bundle (asset package) authoring / loading.
    void createBundle(const std::string &bundle_filename, const std::string &directory_name,
        const std::string &header_filename, const std::string &expiry_date,
        const std::string &key);
    void saveBundle(const std::string &bundle_filename, const std::string &header,
        const std::string &key);
    void loadBundle(const std::string &bundle_filename, const std::string &key);
    void loadBundle(std::istream &bundle_stream, const std::string &key);

    const std::string &getExpiry() const;
    const std::string &getBundleVersion() const;
    std::string getOption(const std::string &option) const;
    void setOption(const std::string &option, const std::string &selection);
    double getUniform(const std::string &name) const;
    void setUniform(const std::string &name, double value);

    // Models (geometry definitions).
    const StringList &getModelNames() const;
    std::size_t getModelIndex(const std::string &model_name) const;
    std::size_t getModelIndex(std::size_t instance_index) const;
    const Box &getBounds(std::size_t model_index) const;
    double getBasePlaneOffset(std::size_t model_index) const;
    double getSinkDepth(std::size_t model_index) const;
    const std::optional<Vec3> &getAnchorPoint(std::size_t model_index) const;
    const std::string &getRegionName(std::size_t model_index, std::size_t region_id) const;
    const StringList &getChannels(std::size_t model_index) const;
    const std::string &getChannelType(std::size_t model_index, std::size_t channel_index) const;
    const StringList &getChannelElements(std::size_t model_index, std::size_t channel_index) const;

    // Excluded orientation windows for a model.
    struct AngleWindow {
        double low_yaw;
        double high_yaw;
        double low_pitch;
        double high_pitch;
    };
    const std::vector<AngleWindow> &getAngleMasks(std::size_t model_index) const;

    // Ray presets (parameterized ray generators).
    const StringList &getRayNames() const;
    const std::string &getRayType(std::size_t ray_index) const;
    bool getRayIsCollimated(std::size_t ray_index) const;
    void clearQueuedRays();
    const StringList &getRayParamNames(std::size_t ray_index) const;
    const std::string &getRayParamValue(std::size_t ray_index, std::size_t parameter_index) const;
    const std::string &getRayParamUnits(std::size_t ray_index, std::size_t parameter_index) const;
    bool isRayParamEditable(std::size_t ray_index, std::size_t parameter_index) const;
    const std::string &getRayParamLimits(std::size_t ray_index, std::size_t parameter_index) const;
    void setRayParam(std::size_t ray_index, std::size_t parameter_index, const std::string &value);
    const std::set<std::size_t> &getRayExclusions(std::size_t ray_index) const;
    std::size_t copyRay(std::size_t ray_index, const std::string &new_name);
    std::size_t importRay(const std::string &ray_directory, const std::string &profile_file,
        const std::string &data_file, bool is_collimated, const std::string &ray_name);

    static constexpr std::size_t NONE = static_cast<std::size_t>(-1);
    void replaceProfile(std::size_t ray_index, std::istream &profile_stream,
        std::size_t template_index = NONE);
    void appendProfile(std::size_t ray_index, std::istream &profile_stream,
        std::size_t template_index);

    // A single geometry probe hit.
    struct Sample {
        Vec3 entry;
        std::size_t region_id;
        static constexpr std::size_t UNSET_ID = static_cast<std::size_t>(-1);
    };
    std::vector<Sample> probeRay(std::size_t model_index, const Ray &ray);

    bool usesAutomaticBase() const;
    void setAutomaticBaseHeight(double z);
    bool usesHeightField() const;
    void setHeightField(const std::string &height_field_filename);
    std::size_t addInstance(std::size_t model_index, const std::string &instance_name);

    enum RotationOrder {
        None,
        YawPitch,
        RollPitchYaw,
        PitchYawRoll,
        YawPitchRoll,
        RollYawPitch,
        YawRollPitch,
        PitchRollYaw
    };

    void moveInstance(std::size_t instance_index, const Vec3 &translation,
        RotationOrder rotation_order = None, const Vec3 *rotation_origin = {},
        const double *rotation_angle1 = {}, const double *rotation_angle2 = {},
        const double *rotation_angle3 = {});

    Vec3 getSceneCoordinates(std::size_t instance_index, const Vec3 &local_point);
    Vec3 getLocalCoordinates(std::size_t instance_index, const Vec3 &scene_point);
    Vec3 getSceneDirection(std::size_t instance_index, const Vec3 &local_dir);
    Vec3 getLocalDirection(std::size_t instance_index, const Vec3 &scene_dir);

    const StringList &getInstanceNames() const;

    // Queue rays for batch evaluation, then evaluate, then read per-channel results.
    void queueRay(std::size_t ray_index, const Vec3 &origin, const Angles &direction,
        const std::optional<double> &speed, const std::optional<double> &range,
        const std::optional<uint64_t> &seed, double time = 0.0);
    void queueRay(std::size_t ray_index, const Vec3 &origin, const Vec3 &direction,
        const std::optional<double> &speed, const std::optional<double> &range,
        const std::optional<uint64_t> &seed, double time = 0.0);
    void evaluateQueuedRays();
    std::string getViewSpec(std::size_t queued_ray_index) const;
    std::string getSceneSpec() const;

    double evaluateShadingParam(std::size_t instance_index, std::size_t channel_index,
        std::size_t element_index) const;

    void resetSamples();
    void resetScene();

    void render(const std::string &filename, const Vec3 &direction, const Vec3 &center,
        double scene_size, double primary_length, double primary_radius,
        double secondary_radius);

    std::vector<ResourceInfo> getResourceInfo() const;

    void testFault(int type, const std::string &message, const std::string &detail);
    std::string getDebugLog() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace rayshade
