#include "RunwaySurfaceProvider.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

using flying::geo_terrain::RunwaySurfaceDefinition;
using flying::geo_terrain::RunwaySurfaceKind;
using flying::geo_terrain::RunwaySurfaceMetadata;
using flying::geo_terrain::RunwaySurfaceProvider;
using flying::geo_terrain::TerrainSurfaceMaterial;
using flying::geo_terrain::make_runway_physical_surface;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) +
                             ", got " + std::to_string(actual));
  }
}

RunwaySurfaceDefinition surface_definition(std::string runway_id,
                                           TerrainSurfaceMaterial material,
                                           double longitudinal_percent,
                                           double transverse_percent) {
  return {
    RunwaySurfaceMetadata{
      "FPPV",
      std::move(runway_id),
      "project-pilot-fixture-2026-08",
      material == TerrainSurfaceMaterial::kGrass ? RunwaySurfaceKind::kSlzStrip
                                                  : RunwaySurfaceKind::kRunway,
      material,
      0.0,
      material != TerrainSurfaceMaterial::kGrass,
      material != TerrainSurfaceMaterial::kGrass,
      material != TerrainSurfaceMaterial::kGrass,
      longitudinal_percent,
      transverse_percent,
    },
    100.0,
    200.0,
    90.0,
    600.0,
    30.0,
    420.0,
    longitudinal_percent,
    transverse_percent,
    0.015,
    material == TerrainSurfaceMaterial::kGrass ? 12.0 : 8.0,
    TerrainSurfaceMaterial::kGrass,
  };
}

void generated_surfaces_preserve_longitudinal_and_transverse_slope() {
  const auto surface = make_runway_physical_surface(
      surface_definition("FPPV-RWY-09-27", TerrainSurfaceMaterial::kAsphalt, 0.4, 1.2));
  require_near(surface.metadata.longitudinal_slope_percent, 0.4, 1.0e-12,
               "longitudinal slope metadata should be preserved");
  require_near(surface.metadata.transverse_slope_percent, 1.2, 1.0e-12,
               "transverse slope metadata should be preserved");

  const double low_end = surface.visual_height_at(-300.0, 0.0);
  const double high_end = surface.visual_height_at(300.0, 0.0);
  require_near((high_end - low_end) / 600.0 * 100.0, 0.4, 1.0e-12,
               "visual mesh should preserve longitudinal slope");

  const double left_edge = surface.visual_height_at(0.0, -15.0);
  const double right_edge = surface.visual_height_at(0.0, 15.0);
  require_near((right_edge - left_edge) / 30.0 * 100.0, 1.2, 1.0e-12,
               "visual mesh should preserve transverse slope");
}

void wheel_contact_visual_and_collision_surfaces_match_within_five_centimeters() {
  RunwaySurfaceProvider provider;
  provider.add_surface(make_runway_physical_surface(
      surface_definition("FPPV-RWY-09-27", TerrainSurfaceMaterial::kAsphalt, 0.2, 0.8)));

  for (double station = -250.0; station <= 250.0; station += 50.0) {
    for (double lateral = -10.0; lateral <= 10.0; lateral += 5.0) {
      const double east = 100.0 + station;
      const double north = 200.0 - lateral;
      const auto sample = provider.sample(east, north);
      require(sample.has_value(), "wheel contact sample should hit runway surface");
      require(std::abs(sample->visual_height_m - sample->collision_height_m) <= 0.05,
              "visual and collision runway surfaces should stay within 0.05 m");
    }
  }
}

void runway_to_terrain_transition_passes_for_paved_and_grass_surfaces() {
  for (const auto material : {TerrainSurfaceMaterial::kAsphalt, TerrainSurfaceMaterial::kGrass}) {
    const auto surface = make_runway_physical_surface(
        surface_definition("transition-test", material, 0.1, 0.15));
    RunwaySurfaceProvider provider;
    provider.add_surface(surface);
    require(!surface.transition_bands.empty(),
            "runway surface should expose terrain transition bands");
    for (const auto& band : surface.transition_bands) {
      require(band.terrain_material == TerrainSurfaceMaterial::kGrass,
              "transition band should blend to surrounding terrain material");
      require(band.passes_transition_limit(0.05),
              "runway-to-terrain transition should stay inside max step limit");
    }

    const double runway_edge_east = 100.0;
    const double runway_edge_north = 200.0 - 15.0;
    const double transition_mid_east = 100.0;
    const double transition_mid_north = 200.0 - 19.0;
    const double outside_east = 100.0;
    const double outside_north = 200.0 - 40.0;
    const auto edge_sample = provider.sample(runway_edge_east, runway_edge_north);
    const auto transition_sample = provider.sample(transition_mid_east, transition_mid_north);
    const auto outside_sample = provider.sample(outside_east, outside_north);
    require(edge_sample.has_value(), "runway edge should sample the runway surface");
    require(transition_sample.has_value(), "transition apron should be sampleable outside runway footprint");
    require(!outside_sample.has_value(), "samples beyond transition apron should fall through to terrain");
    require(transition_sample->material == TerrainSurfaceMaterial::kGrass,
            "transition apron should expose blended terrain material");
    require(std::abs(transition_sample->visual_height_m - transition_sample->collision_height_m) <= 0.05,
            "transition visual and collision heights should satisfy wheel-contact tolerance");
    require(std::abs(transition_sample->visual_height_m - edge_sample->visual_height_m) <= 0.05,
            "sampled transition should not step away from runway edge by more than 0.05 m");
  }
}

} // namespace

int main() {
  generated_surfaces_preserve_longitudinal_and_transverse_slope();
  wheel_contact_visual_and_collision_surfaces_match_within_five_centimeters();
  runway_to_terrain_transition_passes_for_paved_and_grass_surfaces();
  return 0;
}
