// Smoke test / usage example for the RayShader mock.
//
// Exercises the intended development call sequence: load a bundle, pick a
// model + output channel, add an instance, probe geometry, then queue rays,
// evaluate, and read a per-cell shading value that a caller would map to a
// color. Prints results and exits non-zero on failure.
//
// SPDX-License-Identifier: MIT
#include "rayshade/RayShader.h"

#include <cstdio>
#include <optional>

using namespace rayshade;

// Stand-in "value -> color" the way a caller would tint a cell.
static void toColor(double v, int &r, int &g, int &b)
{
    // Simple three-band ramp; the real caller supplies its own.
    if (v < 0.33) { r = 40; g = 40; b = 200; }
    else if (v < 0.66) { r = 40; g = 200; b = 40; }
    else { r = 220; g = 40; b = 40; }
}

int main()
{
    try {
        RayShader shader("dev-harness");
        shader.setResourceRoot("./resources");
        shader.setSeed(1234);

        shader.loadBundle("mock.bundle", "");
        std::printf("version=%s bundle=%s\n", shader.getVersion().c_str(),
            shader.getBundleVersion().c_str());

        const auto &models = shader.getModelNames();
        if (models.empty()) { std::fprintf(stderr, "no models\n"); return 1; }
        const std::size_t model = shader.getModelIndex(models[0]);

        const Box box = shader.getBounds(model);
        std::printf("model '%s' bounds [%.1f %.1f %.1f]..[%.1f %.1f %.1f]\n", models[model].c_str(),
            box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z);

        const auto &channels = shader.getChannels(model);
        if (channels.empty()) { std::fprintf(stderr, "no channels\n"); return 1; }
        const std::size_t channel = 0;
        const std::string chType = shader.getChannelType(model, channel);
        const auto &elements = shader.getChannelElements(model, channel);
        if (elements.empty()) { std::fprintf(stderr, "no elements\n"); return 1; }
        const std::size_t element = 0;
        std::printf("color source: channel '%s' (%s) element '%s'\n", channels[channel].c_str(),
            chType.c_str(), elements[element].c_str());

        const std::size_t inst = shader.addInstance(model, "inst_0");

        // Geometry probe (cheap, no shading).
        const Ray ray{Vec3{-500, 0, 0}, Vec3{1, 0, 0}};
        const auto hits = shader.probeRay(model, ray);
        std::printf("probeRay -> %zu hits\n", hits.size());
        for (const auto &h : hits)
            std::printf("  region %zu at (%.2f %.2f %.2f) '%s'\n", h.region_id, h.entry.x, h.entry.y,
                h.entry.z, shader.getRegionName(model, h.region_id).c_str());

        // Per-cell evaluation loop (one ray per cell, read, reset).
        const std::size_t rayPreset = 0;
        const int W = 4, H = 4;
        std::printf("cell grid %dx%d:\n", W, H);
        for (int row = 0; row < H; ++row) {
            for (int col = 0; col < W; ++col) {
                shader.setSeed(static_cast<uint64_t>(row) * W + col);
                shader.clearQueuedRays();
                const Vec3 origin{-500.0, static_cast<double>(col) * 10, static_cast<double>(row) * 10};
                shader.queueRay(rayPreset, origin, Vec3{1, 0, 0}, std::optional<double>(1000.0),
                    std::nullopt, std::optional<uint64_t>(42));
                shader.evaluateQueuedRays();
                const double v = shader.evaluateShadingParam(inst, channel, element);
                int r, g, b;
                toColor(v, r, g, b);
                std::printf("  cell(%d,%d) value=%.3f color=(%d,%d,%d)\n", col, row, v, r, g, b);
                shader.resetSamples();
            }
        }

        shader.render("mock_render.ppm", Vec3{1, 0, 0}, Vec3{0, 0, 0}, 100.0, 10.0, 1.0, 1.0);
        std::printf("resources: %zu, instances: %zu\n", shader.getResourceInfo().size(),
            shader.getInstanceNames().size());
        std::printf("OK\n");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 2;
    }
}
