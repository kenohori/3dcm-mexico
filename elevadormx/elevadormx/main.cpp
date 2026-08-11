#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <deque>
#include <queue>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>
#include <list>

#include <ogrsf_frmts.h>
#include <gdal_priv.h>
#include <gdal_alg.h>
#include <cpl_string.h>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/squared_distance_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include "Enhanced_constrained_triangulation_2.h"
#include <CGAL/Point_set_3.h>
#include <CGAL/Barycentric_coordinates_2/triangle_coordinates_2.h>
#include "Quadtree.h"
#include "Edge_map.h"

#include <nlohmann/json.hpp>

typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef CGAL::Exact_predicates_tag Tag;
struct Vertex_info;
typedef CGAL::Triangulation_vertex_base_with_info_2<Vertex_info, Kernel> Vertex_base;
typedef CGAL::Constrained_triangulation_face_base_2<Kernel> Face_base;
struct Face_info;
typedef CGAL::Triangulation_face_base_with_info_2<Face_info, Kernel, Face_base> Face_base_with_info;
typedef CGAL::Triangulation_data_structure_2<Vertex_base, Face_base_with_info> Triangulation_data_structure;
typedef CGAL::Constrained_Delaunay_triangulation_2<Kernel, Triangulation_data_structure, Tag> Constrained_delaunay_triangulation;
typedef Enhanced_constrained_triangulation_2<Constrained_delaunay_triangulation> Triangulation;
typedef CGAL::Point_set_3<Kernel::Point_3> Point_cloud;
typedef Quadtree_node<Kernel, Point_cloud> Point_index;
struct Polygon;
typedef Edge_map<Kernel, Triangulation, Polygon> Edge_index;

struct Vertex_info {
  Kernel::FT z;
  Vertex_info() {
    z = 0.0;
  }
};

struct Face_info {
  bool processed;
  bool interior;
  bool grouped;
  int road_segment;
  Face_info() {
    processed = false;
    interior = false;
    grouped = false;
    road_segment = -1;
  }
};

struct Ring {
  std::vector<Kernel::Point_2> points;
};

struct Triangle {
  Kernel::Point_3 p1, p2, p3;
  Triangle(const Kernel::Point_3 &p1, const Kernel::Point_3 &p2, const Kernel::Point_3 &p3) {
    this->p1 = p1;
    this->p2 = p2;
    this->p3 = p3;
  }
};

struct Polygon {
  Ring outer_ring;
  std::vector<Ring> inner_rings;
  Triangulation triangulation;
  std::vector<Triangle> extra_triangles;
  std::string semantic_class;
  std::string id;
  std::unordered_map<std::string, std::string> attributes;
  Kernel::FT x_min, x_max, y_min, y_max;
};

struct Map {
  std::vector<Polygon> polygons;
  std::string crs_authority, crs_code;
};

struct Config {
  // Raster inputs
  std::string dsm_path;
  std::string dtm_path;
  
  // Vector inputs
  std::string building_path;
  std::string waterbody_path;
  std::string plantcover_path;
  std::string road_path;
  std::string terrain_path;
  
  // Outputs
  std::string terrain_obj_path;
  std::string obj_path;
  std::string cityjson_path;
  
  // Simplified DTM TIN generation
  double dtm_cell_size = 30.0;
  double dtm_search_radius = 120.0;
  double dtm_ratio_to_use = 0.5;
  
  // Polygon lifting
  double building_height_percentile = 0.9;
  
  // Building footprint extraction
  std::string mask_output_path;
  std::string building_mask_path;
  std::string grow_output_path;
  std::string buildings_output_path;
  double seed_threshold = 10.0;
  double tall_building_height = 100.0;
  double tall_tolerance = 15.0;
  double normal_tolerance = 0.75;
  int minimum_region_area = 45;
  
  // Building footprint simplification (Visvalingam–Whyatt, effective-area tolerance in metres; 0 disables)
  double simplify_tolerance = 3.0;
  
  // Quadtree index
  std::size_t bucket_size = 100;
  unsigned int maximum_depth = 10;
  
  // Output precision
  int decimal_digits = 2;
  
  // Road polygon generation
  bool generate_roads = false;
  std::string city_blocks_path;
  std::string land_use_path;
  std::string roads_output_path;
  double study_x_min = 0.0, study_y_min = 0.0, study_x_max = 0.0, study_y_max = 0.0;
  bool study_area_set = false;
  
  // Road polygon classification (by proximity to INEGI line layers)
  std::string road_lines_path;
  std::string railway_lines_path;
  std::string stream_lines_path;
  double line_classification_distance = 50.0;
  
  // Plant cover polygon generation
  bool generate_plantcover = false;
  std::string public_areas_path;
  std::string plantcover_output_path;
  
  // Water body polygon generation
  bool generate_waterbodies = false;
  std::string water_areas_path;
  std::string waterbody_output_path;
  
  // Terrain polygon generation
  bool generate_terrain = false;
  std::string terrain_output_path;
  
  // Apply a CLI/config-file key-value pair to this config
  void set(const std::string &key, const std::string &value) {
    if (key == "dsm") dsm_path = value;
    else if (key == "dtm") dtm_path = value;
    else if (key == "building") building_path = value;
    else if (key == "waterbody") waterbody_path = value;
    else if (key == "plantcover") plantcover_path = value;
    else if (key == "road") road_path = value;
    else if (key == "terrain") terrain_path = value;
    else if (key == "terrain_obj") terrain_obj_path = value;
    else if (key == "obj") obj_path = value;
    else if (key == "cityjson") cityjson_path = value;
    else if (key == "dtm_cell_size") dtm_cell_size = std::stod(value);
    else if (key == "dtm_search_radius") dtm_search_radius = std::stod(value);
    else if (key == "dtm_ratio_to_use") dtm_ratio_to_use = std::stod(value);
    else if (key == "building_height_percentile") building_height_percentile = std::stod(value);
    else if (key == "mask_output") mask_output_path = value;
    else if (key == "building_mask") building_mask_path = value;
    else if (key == "grow_output") grow_output_path = value;
    else if (key == "buildings_output") buildings_output_path = value;
    else if (key == "seed_threshold") seed_threshold = std::stod(value);
    else if (key == "tall_building_height") tall_building_height = std::stod(value);
    else if (key == "tall_tolerance") tall_tolerance = std::stod(value);
    else if (key == "normal_tolerance") normal_tolerance = std::stod(value);
    else if (key == "minimum_region_area") minimum_region_area = std::stoi(value);
    else if (key == "simplify_tolerance") simplify_tolerance = std::stod(value);
    else if (key == "bucket_size") bucket_size = std::stoul(value);
    else if (key == "maximum_depth") maximum_depth = std::stoul(value);
    else if (key == "decimal_digits") decimal_digits = std::stoi(value);
    else if (key == "generate_roads") generate_roads = (value == "true" || value == "1");
    else if (key == "city_blocks") city_blocks_path = value;
    else if (key == "land_use") land_use_path = value;
    else if (key == "roads_output") roads_output_path = value;
    else if (key == "road_lines") road_lines_path = value;
    else if (key == "railway_lines") railway_lines_path = value;
    else if (key == "stream_lines") stream_lines_path = value;
    else if (key == "line_classification_distance") line_classification_distance = std::stod(value);
    else if (key == "generate_plantcover") generate_plantcover = (value == "true" || value == "1");
    else if (key == "public_areas") public_areas_path = value;
    else if (key == "plantcover_output") plantcover_output_path = value;
    else if (key == "generate_waterbodies") generate_waterbodies = (value == "true" || value == "1");
    else if (key == "water_areas") water_areas_path = value;
    else if (key == "waterbody_output") waterbody_output_path = value;
    else if (key == "generate_terrain") generate_terrain = (value == "true" || value == "1");
    else if (key == "terrain_output") terrain_output_path = value;
    else if (key == "study_area") {
      // Format: x_min,y_min,x_max,y_max
      std::stringstream value_stream(value);
      std::string current_value;
      std::getline(value_stream, current_value, ',');
      study_x_min = std::stod(current_value);
      std::getline(value_stream, current_value, ',');
      study_y_min = std::stod(current_value);
      std::getline(value_stream, current_value, ',');
      study_x_max = std::stod(current_value);
      std::getline(value_stream, current_value, ',');
      study_y_max = std::stod(current_value);
      study_area_set = true;
    } else std::cerr << "Unknown config option: " << key << std::endl;
  }
  
  void print() const {
    std::cout << "Rasters:\n";
    std::cout << "\tDSM: " << dsm_path << "\n";
    std::cout << "\tDTM: " << dtm_path << "\n";
    std::cout << "Vector layers:\n";
    std::cout << "\tBuilding: " << building_path << "\n";
    std::cout << "\tWaterBody: " << waterbody_path << "\n";
    std::cout << "\tPlantCover: " << plantcover_path << "\n";
    std::cout << "\tRoad: " << road_path << "\n";
    std::cout << "\tTerrain: " << terrain_path << "\n";
    std::cout << "Outputs:\n";
    std::cout << "\tTerrain OBJ: " << terrain_obj_path << "\n";
    std::cout << "\tOBJ: " << obj_path << "\n";
    std::cout << "\tCityJSON: " << cityjson_path << "\n";
    std::cout << "Parameters:\n";
    std::cout << "\tDTM cell size: " << dtm_cell_size << "\n";
    std::cout << "\tDTM search radius: " << dtm_search_radius << "\n";
    std::cout << "\tDTM ratio to use: " << dtm_ratio_to_use << "\n";
    std::cout << "\tBuilding height percentile: " << building_height_percentile << "\n";
    std::cout << "\tMask output: " << mask_output_path << "\n";
    std::cout << "\tBuilding mask: " << building_mask_path << "\n";
    std::cout << "\tGrow output: " << grow_output_path << "\n";
    std::cout << "\tBuildings output: " << buildings_output_path << "\n";
    std::cout << "\tSeed threshold: " << seed_threshold << "\n";
    std::cout << "\tTall building height: " << tall_building_height << "\n";
    std::cout << "\tTall tolerance: " << tall_tolerance << "\n";
    std::cout << "\tNormal tolerance: " << normal_tolerance << "\n";
    std::cout << "\tMinimum region area: " << minimum_region_area << "\n";
    std::cout << "\tSimplify tolerance: " << simplify_tolerance << "\n";
    std::cout << "\tQuadtree bucket size: " << bucket_size << "\n";
    std::cout << "\tQuadtree maximum depth: " << maximum_depth << "\n";
    std::cout << "\tDecimal digits: " << decimal_digits << "\n";
    std::cout << "Road polygon generation:\n";
    std::cout << "\tGenerate roads: " << (generate_roads ? "yes" : "no") << "\n";
    if (generate_roads) {
      std::cout << "\tCity blocks: " << city_blocks_path << "\n";
      std::cout << "\tWater bodies: " << (generate_waterbodies ? water_areas_path : waterbody_path) << "\n";
      std::cout << "\tLand use: " << land_use_path << "\n";
      std::cout << "\tRoads output: " << roads_output_path << "\n";
      std::cout << "\tStudy area: " << study_x_min << ", " << study_y_min << ", " << study_x_max << ", " << study_y_max << "\n";
      std::cout << "\tRoad lines (vialidad_l): " << road_lines_path << "\n";
      std::cout << "\tRailway lines (via_ferrea_l): " << railway_lines_path << "\n";
      std::cout << "\tStream lines (corriente_ag_l): " << stream_lines_path << "\n";
      std::cout << "\tLine classification distance: " << line_classification_distance << "\n";
    }
    std::cout << "Plant cover polygon generation:\n";
    std::cout << "\tGenerate plant cover: " << (generate_plantcover ? "yes" : "no") << "\n";
    if (generate_plantcover) {
      std::cout << "\tPublic areas (area_publica_a): " << public_areas_path << "\n";
      std::cout << "\tPlant cover output: " << plantcover_output_path << "\n";
    }
    std::cout << "Water body polygon generation:\n";
    std::cout << "\tGenerate water bodies: " << (generate_waterbodies ? "yes" : "no") << "\n";
    if (generate_waterbodies) {
      std::cout << "\tWater areas (cuerpo_agua_a, estanque_a, canal_a, corriente_ag_a): " << water_areas_path << "\n";
      std::cout << "\tWater body output: " << waterbody_output_path << "\n";
    }
    std::cout << "Terrain polygon generation:\n";
    std::cout << "\tGenerate terrain: " << (generate_terrain ? "yes" : "no") << "\n";
    if (generate_terrain) {
      std::cout << "\tCity blocks (manzana_a): " << city_blocks_path << "\n";
      std::cout << "\tTerrain output: " << terrain_output_path << "\n";
    }
  }
};

void label_polygon(Polygon &polygon) {
  for (auto const &current_face: polygon.triangulation.finite_face_handles()) {
    current_face->info().processed = false;
    current_face->info().interior = false;
  } std::list<Triangulation::Face_handle> to_check;
  polygon.triangulation.infinite_face()->info().interior = false;
  polygon.triangulation.infinite_face()->info().processed = true;
  CGAL_assertion(polygon.triangulation.infinite_face()->info().processed == true);
  CGAL_assertion(polygon.triangulation.infinite_face()->info().interior == false);
  to_check.push_back(polygon.triangulation.infinite_face());
  while (!to_check.empty()) {
    CGAL_assertion(to_check.front()->info().processed == true);
    for (int neighbour = 0; neighbour < 3; ++neighbour) {
      if (to_check.front()->neighbor(neighbour)->info().processed == true) {
      } else {
        to_check.front()->neighbor(neighbour)->info().processed = true;
        CGAL_assertion(to_check.front()->neighbor(neighbour)->info().processed == true);
        if (polygon.triangulation.is_constrained(Triangulation::Edge(to_check.front(), neighbour))) {
          to_check.front()->neighbor(neighbour)->info().interior = !to_check.front()->info().interior;
          to_check.push_back(to_check.front()->neighbor(neighbour));
        } else {
          to_check.front()->neighbor(neighbour)->info().interior = to_check.front()->info().interior;
          to_check.push_back(to_check.front()->neighbor(neighbour));
        }
      }
    } to_check.pop_front();
  }
}

int write_3dcm_obj(const char *path, Map &map, const Config &config) {
  const int decimal_digits = config.decimal_digits;
  
  std::ofstream output_stream;
  std::string output_3dcm(path);
  output_stream.open(output_3dcm);
  output_stream << std::fixed;
  output_stream << std::setprecision(decimal_digits);
  output_stream << "mtllib ./elevador.mtl" << std::endl;
  std::unordered_map<Kernel::Point_3, std::size_t> output_vertices;
  std::size_t num_polygons = 0;
  
  // Vertices
  for (std::vector<Polygon>::iterator current_polygon = map.polygons.begin(); current_polygon != map.polygons.end(); ++current_polygon) {
    for (Triangulation::Finite_faces_iterator current_face = current_polygon->triangulation.finite_faces_begin();
         current_face != current_polygon->triangulation.finite_faces_end();
         ++current_face) {
      if (current_face->info().interior == false) continue;
      for (int v = 0; v < 3; ++v) {
        Kernel::Point_3 point(current_face->vertex(v)->point().x(), current_face->vertex(v)->point().y(), current_face->vertex(v)->info().z);
        if (output_vertices.count(point) == 0) {
          output_stream << "v " << point.x() << " " << point.y() << " " << point.z() << "\n";
          output_vertices[point] = output_vertices.size()+1;
        }
      }
    } for (auto const &triangle: current_polygon->extra_triangles) {
      if (output_vertices.count(triangle.p1) == 0) {
        output_stream << "v " << triangle.p1.x() << " " << triangle.p1.y() << " " << triangle.p1.z() << "\n";
        output_vertices[triangle.p1] = output_vertices.size()+1;
      } if (output_vertices.count(triangle.p2) == 0) {
        output_stream << "v " << triangle.p2.x() << " " << triangle.p2.y() << " " << triangle.p2.z() << "\n";
        output_vertices[triangle.p2] = output_vertices.size()+1;
      } if (output_vertices.count(triangle.p3) == 0) {
        output_stream << "v " << triangle.p3.x() << " " << triangle.p3.y() << " " << triangle.p3.z() << "\n";
        output_vertices[triangle.p3] = output_vertices.size()+1;
      }
    }
  }
  
  // Faces
  for (std::vector<Polygon>::iterator current_polygon = map.polygons.begin(); current_polygon != map.polygons.end(); ++current_polygon) {
    
    // Triangles in polygon triangulation
    output_stream << "o " << std::to_string(num_polygons) << "\n";
    output_stream << "usemtl " << current_polygon->semantic_class << "\n";
    for (Triangulation::Finite_faces_iterator current_face = current_polygon->triangulation.finite_faces_begin();
         current_face != current_polygon->triangulation.finite_faces_end();
         ++current_face) {
      if (current_face->info().interior == false) continue;
      output_stream << "f";
      for (int v = 0; v < 3; ++v) {
        output_stream << " " << output_vertices[Kernel::Point_3(current_face->vertex(v)->point().x(), current_face->vertex(v)->point().y(), current_face->vertex(v)->info().z)];
      } output_stream << "\n";
    }
    
    // Additional triangles
    bool first_wall = true;
    for (auto const &triangle: current_polygon->extra_triangles) {
      if (first_wall) {
        if (current_polygon->semantic_class == "Building") output_stream << "usemtl BuildingWall\n";
        first_wall = false;
      } std::size_t v1 = output_vertices[triangle.p1];
      std::size_t v2 = output_vertices[triangle.p2];
      std::size_t v3 = output_vertices[triangle.p3];
      output_stream << "f " << v1 << " " << v2 << " " << v3 << "\n";
    }
    
  ++num_polygons;
  }
  
  output_stream.close();
  return 0;
}

int write_3dcm_cityjson(const char *path, Map &map, const Config &config) {
  const int decimal_digits = config.decimal_digits;
  
  Kernel::FT scale_factor = 1.0;
  for (int digit = 0; digit < decimal_digits; ++digit) scale_factor *= 0.1;
  
  std::ofstream output_stream;
  std::string output_3dcm(path);
  output_stream.open(output_3dcm);
  output_stream << std::fixed;
  output_stream << std::setprecision(2);
  std::unordered_map<Kernel::Point_3, std::size_t> output_vertices;
  std::size_t num_polygons = 0;
  
  // Compute extent
  if (map.polygons.empty()) {
    std::cerr << "Error: No polygons to write to CityJSON." << std::endl;
    return EXIT_FAILURE;
  }
  Kernel::FT x_min = map.polygons.front().x_min;
  Kernel::FT x_max = map.polygons.front().x_max;
  Kernel::FT y_min = map.polygons.front().y_min;
  Kernel::FT y_max = map.polygons.front().y_max;
  Kernel::FT z_min = 10000.0;
  Kernel::FT z_max = -1000.0;
  for (std::vector<Polygon>::iterator current_polygon = map.polygons.begin(); current_polygon != map.polygons.end(); ++current_polygon) {
    for (auto const &current_vertex: current_polygon->triangulation.finite_vertex_handles()) {
      if (current_vertex->point().x() < x_min) x_min = current_vertex->point().x();
      if (current_vertex->point().x() > x_max) x_max = current_vertex->point().x();
      if (current_vertex->point().y() < y_min) y_min = current_vertex->point().y();
      if (current_vertex->point().y() > y_max) y_max = current_vertex->point().y();
      if (current_vertex->info().z < z_min) z_min = current_vertex->info().z;
      if (current_vertex->info().z > z_max) z_max = current_vertex->info().z;
    }
  }
  
  // Prepare CityJSON
  nlohmann::json cityjson;
  cityjson["type"] = "CityJSON";
  cityjson["version"] = "1.1";
  cityjson["transform"] = nlohmann::json::object();
  cityjson["transform"]["scale"] = {scale_factor, scale_factor, scale_factor};
  cityjson["transform"]["translate"] = {x_min, y_min, z_min};
  cityjson["CityObjects"] = nlohmann::json::object();
  cityjson["vertices"] = nlohmann::json::array();
  cityjson["metadata"] = nlohmann::json::object();
  cityjson["metadata"]["geographicalExtent"] = {x_min, y_min, z_min, x_max, y_max, z_max};
  const std::chrono::time_point now{std::chrono::system_clock::now()};
  const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(now)};
  cityjson["metadata"]["referenceDate"] = std::to_string(int(ymd.year())) + "-" + std::to_string(unsigned(ymd.month())) + "-" + std::to_string(unsigned(ymd.day()));
  cityjson["metadata"]["referenceSystem"] = std::string("https://www.opengis.net/def/crs/") + map.crs_authority + "/0/" + map.crs_code;
  
  // Vertices
  for (std::vector<Polygon>::iterator current_polygon = map.polygons.begin(); current_polygon != map.polygons.end(); ++current_polygon) {
    for (Triangulation::Finite_faces_iterator current_face = current_polygon->triangulation.finite_faces_begin();
         current_face != current_polygon->triangulation.finite_faces_end();
         ++current_face) {
      if (current_face->info().interior == false) continue;
      for (int v = 0; v < 3; ++v) {
        Kernel::Point_3 point(current_face->vertex(v)->point().x(), current_face->vertex(v)->point().y(), current_face->vertex(v)->info().z);
        if (output_vertices.count(point) == 0) {
          cityjson["vertices"].push_back({int((point.x()-x_min)/scale_factor), int((point.y()-y_min)/scale_factor), int((point.z()-z_min)/scale_factor)});
          output_vertices[point] = output_vertices.size();
        }
      }
    } for (auto const &triangle: current_polygon->extra_triangles) {
      if (output_vertices.count(triangle.p1) == 0) {
        cityjson["vertices"].push_back({int((triangle.p1.x()-x_min)/scale_factor), int((triangle.p1.y()-y_min)/scale_factor), int((triangle.p1.z()-z_min)/scale_factor)});
        output_vertices[triangle.p1] = output_vertices.size();
      } if (output_vertices.count(triangle.p2) == 0) {
        cityjson["vertices"].push_back({int((triangle.p2.x()-x_min)/scale_factor), int((triangle.p2.y()-y_min)/scale_factor), int((triangle.p2.z()-z_min)/scale_factor)});
        output_vertices[triangle.p2] = output_vertices.size();
      } if (output_vertices.count(triangle.p3) == 0) {
        cityjson["vertices"].push_back({int((triangle.p3.x()-x_min)/scale_factor), int((triangle.p3.y()-y_min)/scale_factor), int((triangle.p3.z()-z_min)/scale_factor)});
        output_vertices[triangle.p3] = output_vertices.size();
      }
    }
  }
  
  // City objects
  for (std::vector<Polygon>::iterator current_polygon = map.polygons.begin(); current_polygon != map.polygons.end(); ++current_polygon) {
    
    cityjson["CityObjects"][current_polygon->id] = nlohmann::json::object();
    cityjson["CityObjects"][current_polygon->id]["type"] = current_polygon->semantic_class;
    cityjson["CityObjects"][current_polygon->id]["geometry"] = nlohmann::json::array();
    cityjson["CityObjects"][current_polygon->id]["geometry"].push_back(nlohmann::json::object());
    cityjson["CityObjects"][current_polygon->id]["geometry"].back()["lod"] = "1.2";
    cityjson["CityObjects"][current_polygon->id]["geometry"].back()["boundaries"] = nlohmann::json::array();
    cityjson["CityObjects"][current_polygon->id]["geometry"].back()["type"] = "MultiSurface";
    cityjson["CityObjects"][current_polygon->id]["attributes"] = nlohmann::json::object();
    for (auto const &attribute: current_polygon->attributes) {
      cityjson["CityObjects"][current_polygon->id]["attributes"][attribute.first] = attribute.second;
    }
    
    // Triangles in polygon triangulation
    for (Triangulation::Finite_faces_iterator current_face = current_polygon->triangulation.finite_faces_begin();
         current_face != current_polygon->triangulation.finite_faces_end();
         ++current_face) {
      if (current_face->info().interior == false) continue;
      std::vector<std::size_t> vertices_of_face;
      for (int v = 0; v < 3; ++v) {
        vertices_of_face.push_back(output_vertices[Kernel::Point_3(current_face->vertex(v)->point().x(), current_face->vertex(v)->point().y(), current_face->vertex(v)->info().z)]);
      } cityjson["CityObjects"][current_polygon->id]["geometry"].back()["boundaries"].push_back({{vertices_of_face[0], vertices_of_face[1], vertices_of_face[2]}});
    }
    
    // Additional triangles
    for (auto const &triangle: current_polygon->extra_triangles) {
      std::size_t v1 = output_vertices[triangle.p1];
      std::size_t v2 = output_vertices[triangle.p2];
      std::size_t v3 = output_vertices[triangle.p3];
      cityjson["CityObjects"][current_polygon->id]["geometry"].back()["boundaries"].push_back({{v1, v2, v3}});
    }
    
  ++num_polygons;
  }
  
  output_stream << cityjson.dump() << std::endl;
  output_stream.close();
  return 0;
}

void index_point_cloud(Point_cloud &point_cloud, Point_index &index, const Config &config) {
  
  const int bucket_size = config.bucket_size;
  const int maximum_depth = config.maximum_depth;
  
  if (point_cloud.empty()) {
    std::cerr << "Warning: Empty point cloud, skipping quadtree index." << std::endl;
    return;
  }
  
  index.compute_extent(point_cloud);
  for (Point_cloud::const_iterator point_index = point_cloud.begin(); point_index != point_cloud.end(); ++point_index) index.insert_point(point_cloud, *point_index);
  index.optimise(point_cloud, bucket_size, maximum_depth);
}

void lift_flat_polygons(const char *cityjson_class, Map &map, Point_cloud &point_cloud, Point_index &point_cloud_index, Kernel::FT ratio_to_use) {
  std::size_t n_polygons = 0;
  for (auto &polygon: map.polygons) {
    if (polygon.semantic_class == cityjson_class) {
      
      // Find index nodes overlapping the polygon bbox
      std::vector<Point_index *> intersected_nodes;
      point_cloud_index.find_intersections(intersected_nodes, polygon.x_min, polygon.x_max, polygon.y_min, polygon.y_max);
      
      // Find PC points overlapping the polygon
      std::vector<Point_cloud::Index> points_in_polygon;
      for (auto const &node: intersected_nodes) {
        for (auto const &point_index: node->points) {
          Triangulation::Face_handle face = polygon.triangulation.locate(Kernel::Point_2(point_cloud.point(point_index).x(),
                                                                                         point_cloud.point(point_index).y()));
          if (!polygon.triangulation.is_infinite(face) && face->info().interior) points_in_polygon.push_back(point_index);
        }
      }
      
      // If there are points, use those
      if (!points_in_polygon.empty()) {
        
        // Sort elevations to obtain elevation
        std::vector<Kernel::FT> elevations;
        for (auto const &point_index: points_in_polygon) elevations.push_back(point_cloud.point(point_index).z());
        std::sort(elevations.begin(), elevations.end());
        Kernel::FT polygon_elevation = elevations[std::floor(ratio_to_use*elevations.size())];
        
        // Set elevation of polygon points to calculated elevation
        for (Triangulation::Finite_vertices_iterator current_vertex = polygon.triangulation.finite_vertices_begin();
             current_vertex != polygon.triangulation.finite_vertices_end();
             ++current_vertex) {
          current_vertex->info().z = polygon_elevation;
        }
        
      }
      
      // TODO: If there are no points
      else {
        std::cout << "No points in polygon!" << std::endl;
      }
      
      ++n_polygons;
    }
  }
}

// Interpolate the DTM TIN elevation at a 2D point, falling back to the closest point on the TIN's convex hull when the point is outside it
Kernel::FT interpolate_dtm_height(const Triangulation &terrain, const Kernel::Point_2 &point) {
  if (terrain.number_of_vertices() == 0) return 0.0;
  Triangulation::Face_handle face_of_point = terrain.locate(point);
  Kernel::Point_2 query_point = point;
  if (terrain.is_infinite(face_of_point)) {
    
    // Get the finite hull edge of this infinite face
    Kernel::Point_2 a, b;
    Kernel::FT za, zb;
    bool first_finite_vertex = true;
    for (int i = 0; i < 3; ++i) {
      if (terrain.is_infinite(face_of_point->vertex(i))) continue;
      if (first_finite_vertex) {
        a = face_of_point->vertex(i)->point();
        za = face_of_point->vertex(i)->info().z;
        first_finite_vertex = false;
      } else {
        b = face_of_point->vertex(i)->point();
        zb = face_of_point->vertex(i)->info().z;
      }
    }
    
    // Project the query point onto the hull edge
    Kernel::Vector_2 ab = b - a;
    const Kernel::FT length_sq = ab.squared_length();
    Kernel::FT t = (length_sq == 0.0) ? 0.0 : CGAL::scalar_product(point - a, ab)/length_sq;
    t = std::max(Kernel::FT(0.0), std::min(Kernel::FT(1.0), t));
    query_point = a + t*ab;
    face_of_point = terrain.locate(query_point);
    if (terrain.is_infinite(face_of_point)) return za + t*(zb-za);
  }
  std::vector<Kernel::FT> barycentric_coordinates;
  CGAL::Barycentric_coordinates::triangle_coordinates_2(face_of_point->vertex(0)->point(),
                                                        face_of_point->vertex(1)->point(),
                                                        face_of_point->vertex(2)->point(),
                                                        query_point,
                                                        std::back_inserter(barycentric_coordinates));
  return barycentric_coordinates[0]*face_of_point->vertex(0)->info().z +
         barycentric_coordinates[1]*face_of_point->vertex(1)->info().z +
         barycentric_coordinates[2]*face_of_point->vertex(2)->info().z;
}

void lift_polygon_vertices(const char *cityjson_class, Map &map, Point_cloud &point_cloud, Point_index &point_cloud_index, Triangulation &terrain) {
  clock_t start_time = clock();
  std::size_t n_vertices = 0, n_polygons = 0;
  for (auto &polygon: map.polygons) {
    if (polygon.semantic_class == cityjson_class) {
      for (Triangulation::Finite_vertices_iterator current_vertex = polygon.triangulation.finite_vertices_begin();
           current_vertex != polygon.triangulation.finite_vertices_end();
           ++current_vertex) {
        
        current_vertex->info().z = interpolate_dtm_height(terrain, current_vertex->point());
        
        ++n_vertices;
      } ++n_polygons;
    }
  }
}

void lift_polygons(const char *cityjson_class, Map &map, Point_cloud &point_cloud, Point_index &point_cloud_index, Triangulation &terrain) {
  std::size_t n_polygons = 0;
  for (auto &polygon: map.polygons) {
    if (polygon.semantic_class == cityjson_class) {
      
      // Lift polygon vertices
      for (Triangulation::Finite_vertices_iterator current_vertex = polygon.triangulation.finite_vertices_begin();
           current_vertex != polygon.triangulation.finite_vertices_end();
           ++current_vertex) {
        
        current_vertex->info().z = interpolate_dtm_height(terrain, current_vertex->point());
      }
      
      // Add terrain vertices in polygon
      std::vector<Triangulation::Vertex_handle> terrain_vertices_in_polygon;
      for (auto const &current_vertex: terrain.finite_vertex_handles()) {
        if (current_vertex->point().x() > polygon.x_min && current_vertex->point().x() < polygon.x_max &&
            current_vertex->point().y() > polygon.y_min && current_vertex->point().y() < polygon.y_max) {
          Triangulation::Locate_type locate_type;
          int vertex_index;
          Triangulation::Face_handle face_of_point = polygon.triangulation.locate(current_vertex->point(), locate_type, vertex_index);
          if (locate_type == Triangulation::FACE && face_of_point->info().interior == true) {
            terrain_vertices_in_polygon.push_back(current_vertex);
          }
        }
      } for (auto const &terrain_vertex: terrain_vertices_in_polygon) {
        Triangulation::Vertex_handle polygon_vertex = polygon.triangulation.insert(terrain_vertex->point());
        polygon_vertex->info().z = terrain_vertex->info().z;
      }
      
      // Relabel triangles as interior/exterior
      label_polygon(polygon);
      
      ++n_polygons;
    }
  }
}

void create_vertical_walls(Map &map, Triangulation &dtm) {
  // For every border edge in map polygon
  for (std::vector<Polygon>::iterator current_polygon = map.polygons.begin(); current_polygon != map.polygons.end(); ++current_polygon) {
    if (current_polygon->semantic_class != "Building") continue;
    for (auto const &face: current_polygon->triangulation.finite_face_handles()) {
      if (face->info().interior) {
        for (int opposite_vertex = 0; opposite_vertex < 3; ++opposite_vertex) {
          if (face->neighbor(opposite_vertex) == current_polygon->triangulation.infinite_face() ||
              !face->neighbor(opposite_vertex)->info().interior) {
            
            // Get origin and destination
            Triangulation::Vertex_handle origin = face->vertex(face->ccw(opposite_vertex));
            Triangulation::Vertex_handle destination = face->vertex(face->cw(opposite_vertex));
            CGAL_assertion(CGAL::orientation(origin->point(), destination->point(), face->vertex(opposite_vertex)->point()) == CGAL::COUNTERCLOCKWISE);
            
            // Get elevations at origin and destination
            std::set<Kernel::FT> origin_elevations, destination_elevations;
            origin_elevations.insert(origin->info().z);
            Kernel::FT z = interpolate_dtm_height(dtm, origin->point());
            origin_elevations.insert(z);
            destination_elevations.insert(destination->info().z);
            z = interpolate_dtm_height(dtm, destination->point());
            destination_elevations.insert(z);
            
            // Vertical wall is not needed
            if (origin_elevations.size() == 1 && destination_elevations.size() == 1) continue;

            // Quad-like
            if (origin_elevations.size() == 2 && destination_elevations.size() == 2) {
              
              // Quad (from top to avoid duplicates)
              if (origin->info().z == *origin_elevations.rbegin() && destination->info().z == *destination_elevations.rbegin()) {
                Kernel::Point_3 origin_top(origin->point().x(), origin->point().y(), *origin_elevations.rbegin());
                Kernel::Point_3 destination_top(destination->point().x(), destination->point().y(), *destination_elevations.rbegin());
                Kernel::Point_3 origin_bottom(origin->point().x(), origin->point().y(), *origin_elevations.begin());
                Kernel::Point_3 destination_bottom(destination->point().x(), destination->point().y(), *destination_elevations.begin());
                current_polygon->extra_triangles.push_back(Triangle(destination_top, origin_top, origin_bottom));
                current_polygon->extra_triangles.push_back(Triangle(origin_bottom, destination_bottom, destination_top));
              }
              
              // Bowtie (from origin at top and destination at bottom to avoid duplicates)
              else if (origin->info().z == *origin_elevations.rbegin() && destination->info().z == *destination_elevations.begin()) {
                std::cout << "Unsupported case: bowtie" << std::endl;
                Kernel::Point_3 origin_top(origin->point().x(), origin->point().y(), *origin_elevations.rbegin());
                Kernel::Point_3 destination_top(destination->point().x(), destination->point().y(), *destination_elevations.rbegin());
                Kernel::Point_3 origin_bottom(origin->point().x(), origin->point().y(), *origin_elevations.begin());
                Kernel::Point_3 destination_bottom(destination->point().x(), destination->point().y(), *destination_elevations.begin());
//                  std::cout << "\torigin top: (" << origin_top << ")" << std::endl;
//                  std::cout << "\torigin bottom: (" << origin_bottom << ")" << std::endl;
//                  std::cout << "\tdestination top: (" << destination_top << ")" << std::endl;
//                  std::cout << "\tdestination bottom: (" << destination_bottom << ")" << std::endl;
                Kernel::Segment_3 top_to_bottom(origin_top, destination_bottom);
                Kernel::Segment_3 bottom_to_top(origin_bottom, destination_top);
                auto result = CGAL::intersection(top_to_bottom, bottom_to_top);
                if (result) {
                  if (const Kernel::Point_3 *intersection_point = std::get_if<Kernel::Point_3>(&*result)) {
//                      std::cout << "\tintersection at (" << *intersection_point << ")" << std::endl;
//                      Kernel::Point_2 intersection_point_2d(intersection_point->x(), intersection_point->y());
//                      current_polygon->extra_triangles.push_back(Triangle(origin_bottom, origin_top, *intersection_point));
//                      current_polygon->extra_triangles.push_back(Triangle(destination_bottom, destination_top, *intersection_point));
                  } else {
                    std::cout << "Error: bowtie intersection is a line segment" << std::endl;
                  }
                } else {
                  std::cout << "Error: bowtie has no intersection point" << std::endl;
                }

//                    for (auto const &same_side_face: edge_index.edges[origin->point()][destination->point()].adjacent_faces) {
//                      Triangulation::Vertex_handle origin_same_side_face = same_side_face.face->vertex(same_side_face.face->ccw(same_side_face.opposite_vertex));
//                      Triangulation::Vertex_handle destination_same_side_face = same_side_face.face->vertex(same_side_face.face->cw(same_side_face.opposite_vertex));
//                      Triangulation::Vertex_handle inserted_vertex = same_side_face.polygon->triangulation.insert_in_edge(intersection_point_2d, same_side_face.face, same_side_face.opposite_vertex);
//                      Triangulation::Face_handle origin_middle_face;
//                      int origin_middle_opposite_vertex;
//                      CGAL_assertion(same_side_face.polygon->triangulation.is_edge(origin_same_side_face, inserted_vertex, origin_middle_face, origin_middle_opposite_vertex));
//                      origin_middle_face->info().interior = true;
//                      origin_middle_face->set_constraint(origin_middle_opposite_vertex, true);
//                      CGAL_assertion(same_side_face.polygon->triangulation.is_constrained(std::pair<Triangulation::Face_handle, int>(origin_middle_face, origin_middle_opposite_vertex)));
//                      Triangulation::Face_handle middle_destination_face;
//                      int middle_destination_opposite_vertex;
//                      CGAL_assertion(same_side_face.polygon->triangulation.is_edge(inserted_vertex, destination_same_side_face, middle_destination_face, middle_destination_opposite_vertex));
//                      middle_destination_face->info().interior = true;
//                      middle_destination_face->set_constraint(middle_destination_opposite_vertex, true);
//                      CGAL_assertion(same_side_face.polygon->triangulation.is_constrained(std::pair<Triangulation::Face_handle, int>(middle_destination_face, middle_destination_opposite_vertex)));
//                    std::cout << "\tseems okay!" << std::endl;
//                    label_polygon(*same_side_face.polygon);
//                    edge_index.erase(same_side_face.polygon);
//                    edge_index.insert(same_side_face.polygon);
//                      edge_index.check(map.polygons);
//                    } edge_index.edges[origin->point()].erase(destination->point());
//                    for (auto const &opposite_side_face: edge_index.edges[destination->point()][origin->point()].adjacent_faces) {
//                      Triangulation::Vertex_handle origin_opposite_side_face = opposite_side_face.face->vertex(opposite_side_face.face->ccw(opposite_side_face.opposite_vertex));
//                      Triangulation::Vertex_handle destination_opposite_side_face = opposite_side_face.face->vertex(opposite_side_face.face->cw(opposite_side_face.opposite_vertex));
//                      Triangulation::Vertex_handle inserted_vertex = opposite_side_face.polygon->triangulation.insert_in_edge(intersection_point_2d, opposite_side_face.face, opposite_side_face.opposite_vertex);
//                      Triangulation::Face_handle origin_middle_face;
//                      int origin_middle_opposite_vertex;
//                      CGAL_assertion(opposite_side_face.polygon->triangulation.is_edge(origin_opposite_side_face, inserted_vertex, origin_middle_face, origin_middle_opposite_vertex));
//                      origin_middle_face->info().interior = true;
//                      origin_middle_face->set_constraint(origin_middle_opposite_vertex, true);
//                      CGAL_assertion(opposite_side_face.polygon->triangulation.is_constrained(std::pair<Triangulation::Face_handle, int>(origin_middle_face, origin_middle_opposite_vertex)));
//                      Triangulation::Face_handle middle_destination_face;
//                      int middle_destination_opposite_vertex;
//                      CGAL_assertion(opposite_side_face.polygon->triangulation.is_edge(inserted_vertex, destination_opposite_side_face, middle_destination_face, middle_destination_opposite_vertex));
//                      middle_destination_face->info().interior = true;
//                      middle_destination_face->set_constraint(middle_destination_opposite_vertex, true);
//                      CGAL_assertion(opposite_side_face.polygon->triangulation.is_constrained(std::pair<Triangulation::Face_handle, int>(middle_destination_face, middle_destination_opposite_vertex)));
//                    std::cout << "\tseems okay!" << std::endl;
//                    label_polygon(*opposite_side_face.polygon);
//                    edge_index.erase(opposite_side_face.polygon);
//                    edge_index.insert(opposite_side_face.polygon);
//                      edge_index.check(map.polygons);
//                    } edge_index.edges[destination->point()].erase(origin->point());
              }
            }
              
            // Triangle (from 2 origin elevations and 1 destination elevation to avoid duplicates)
            else if (origin_elevations.size() == 2 && destination_elevations.size() == 1) {
              Kernel::Point_3 origin_top(origin->point().x(), origin->point().y(), *origin_elevations.rbegin());
              Kernel::Point_3 origin_bottom(origin->point().x(), origin->point().y(), *origin_elevations.begin());
              Kernel::Point_3 destination_only(destination->point().x(), destination->point().y(), destination->info().z);
              current_polygon->extra_triangles.push_back(Triangle(destination_only, origin_top, origin_bottom));
            }
            
            // Skip other triangle case
            else if (origin_elevations.size() == 1 && destination_elevations.size() == 2);
            
            // Warn for other cases (overlapping polygons)
            else {
              std::cout << "Unsupported case: " << origin_elevations.size() << " origin elevations and " << destination_elevations.size() << " destination elevations" << std::endl;
            }
          }
        }
      }
    }
  }
}

void write_terrain_obj(const char *path, Triangulation &terrain) {
  std::ofstream output_stream;
  std::string output_terrain(path);
  output_stream.open(output_terrain);
  output_stream << std::fixed;
  output_stream << std::setprecision(2);
  std::unordered_map<Kernel::Point_3, std::size_t> output_vertices;
  
  // Vertices
  for (auto const &vertex: terrain.finite_vertex_handles()) {
    Kernel::Point_3 point(vertex->point().x(), vertex->point().y(), vertex->info().z);
    output_stream << "v " << point.x() << " " << point.y() << " " << point.z() << "\n";
    output_vertices[point] = output_vertices.size()+1;
  }
  
  // Faces
  for (Triangulation::Finite_faces_iterator current_face = terrain.finite_faces_begin();
       current_face != terrain.finite_faces_end();
       ++current_face) {
    output_stream << "f";
    for (int v = 0; v < 3; ++v) {
      output_stream << " " << output_vertices[Kernel::Point_3(current_face->vertex(v)->point().x(),
                                                              current_face->vertex(v)->point().y(),
                                                              current_face->vertex(v)->info().z)];
    } output_stream << "\n";
  } output_stream.close();
}

// Read polygon features from a dataset into a multipolygon (within the study area)
void read_polygon_features(GDALDataset *dataset, const Config &config, OGRMultiPolygon &multipolygon, std::size_t &n_polygons, Map &map) {
  for (auto &&input_layer: dataset->GetLayers()) {
    input_layer->ResetReading();
    input_layer->SetSpatialFilterRect(config.study_x_min, config.study_y_min, config.study_x_max, config.study_y_max);
    
    // Extract CRS from this layer
    const OGRSpatialReference *spatial_reference = input_layer->GetSpatialRef();
    if (spatial_reference != NULL) {
      const char *authority = spatial_reference->GetAuthorityName(NULL);
      if (authority != NULL) map.crs_authority = std::string(authority);
      const char *code = spatial_reference->GetAuthorityCode(NULL);
      if (code != NULL) map.crs_code = std::string(code);
    }
    
    OGRFeature *input_feature;
    while ((input_feature = input_layer->GetNextFeature()) != NULL) {
      if (!input_feature->GetGeometryRef()) continue;
      OGRwkbGeometryType geometry_type = wkbFlatten(input_feature->GetGeometryRef()->getGeometryType());
      if (geometry_type == wkbPolygon) {
        multipolygon.addGeometry(input_feature->GetGeometryRef());
        ++n_polygons;
      } else if (geometry_type == wkbMultiPolygon) {
        OGRMultiPolygon *input_multipolygon = input_feature->GetGeometryRef()->toMultiPolygon();
        for (int current_polygon = 0; current_polygon < input_multipolygon->getNumGeometries(); ++current_polygon) {
          multipolygon.addGeometry(input_multipolygon->getGeometryRef(current_polygon));
          ++n_polygons;
        }
      }
    }
  }
}

// Read all polygon features from a dataset into the map with the given semantic class
std::size_t read_polygon_layer(GDALDataset *dataset, const std::string &semantic_class, Map &map, const Config &config) {
  std::size_t n_polygons = 0;
  for (auto &&input_layer: dataset->GetLayers()) {
    input_layer->ResetReading();
    if (config.study_area_set) input_layer->SetSpatialFilterRect(config.study_x_min, config.study_y_min, config.study_x_max, config.study_y_max);
    
    // Try to extract CRS from this layer
    const OGRSpatialReference *spatial_reference = input_layer->GetSpatialRef();
    if (spatial_reference != NULL) {
      char *srs = (char *)CPLMalloc(10000*sizeof(char));
      spatial_reference->exportToPrettyWkt(&srs);
      CPLFree(srs);
      const char *authority = spatial_reference->GetAuthorityName(NULL);
      if (authority != NULL) map.crs_authority = std::string(authority);
      const char *code = spatial_reference->GetAuthorityCode(NULL);
      if (code != NULL) map.crs_code = std::string(code);
    }
    
    OGRFeature *input_feature;
    while ((input_feature = input_layer->GetNextFeature()) != NULL) {
      if (!input_feature->GetGeometryRef()) continue;
      
      if (wkbFlatten(input_feature->GetGeometryRef()->getGeometryType()) == wkbPolygon ||
          wkbFlatten(input_feature->GetGeometryRef()->getGeometryType()) == wkbTriangle) {
        OGRPolygon *input_polygon = input_feature->GetGeometryRef()->toPolygon();
        map.polygons.emplace_back();
        map.polygons.back().id = semantic_class + "-" + std::to_string(input_feature->GetFID());
        map.polygons.back().semantic_class = semantic_class;
        for (int current_vertex = 0; current_vertex < input_polygon->getExteriorRing()->getNumPoints(); ++current_vertex) {
          map.polygons.back().outer_ring.points.emplace_back(input_polygon->getExteriorRing()->getX(current_vertex),
                                                             input_polygon->getExteriorRing()->getY(current_vertex));
        } for (int current_inner_ring = 0; current_inner_ring < input_polygon->getNumInteriorRings(); ++current_inner_ring) {
          map.polygons.back().inner_rings.emplace_back();
          for (int current_vertex = 0; current_vertex < input_polygon->getInteriorRing(current_inner_ring)->getNumPoints(); ++current_vertex) {
            map.polygons.back().inner_rings.back().points.emplace_back(input_polygon->getInteriorRing(current_inner_ring)->getX(current_vertex),
                                                                       input_polygon->getInteriorRing(current_inner_ring)->getY(current_vertex));
          }
        }
        ++n_polygons;
      } else if (wkbFlatten(input_feature->GetGeometryRef()->getGeometryType()) == wkbMultiPolygon) {
        OGRMultiPolygon *input_multipolygon = input_feature->GetGeometryRef()->toMultiPolygon();
        for (int current_polygon = 0; current_polygon < input_multipolygon->getNumGeometries(); ++current_polygon) {
          OGRPolygon *input_polygon = input_multipolygon->getGeometryRef(current_polygon);
          map.polygons.emplace_back();
          map.polygons.back().id = semantic_class + "-" + std::to_string(input_feature->GetFID()) + "-" + std::to_string(current_polygon);
          map.polygons.back().semantic_class = semantic_class;
          for (int current_vertex = 0; current_vertex < input_polygon->getExteriorRing()->getNumPoints(); ++current_vertex) {
            map.polygons.back().outer_ring.points.emplace_back(input_polygon->getExteriorRing()->getX(current_vertex),
                                                               input_polygon->getExteriorRing()->getY(current_vertex));
          } for (int current_inner_ring = 0; current_inner_ring < input_polygon->getNumInteriorRings(); ++current_inner_ring) {
            map.polygons.back().inner_rings.emplace_back();
            for (int current_vertex = 0; current_vertex < input_polygon->getInteriorRing(current_inner_ring)->getNumPoints(); ++current_vertex) {
              map.polygons.back().inner_rings.back().points.emplace_back(input_polygon->getInteriorRing(current_inner_ring)->getX(current_vertex),
                                                                         input_polygon->getInteriorRing(current_inner_ring)->getY(current_vertex));
            }
          }
          ++n_polygons;
        }
      } else {
        std::cout << "Unknown type..." << std::endl;
      }
    }
  }
  return n_polygons;
}

// Split a comma-separated list of paths
std::vector<std::string> split_paths(const std::string &paths) {
  std::vector<std::string> result;
  std::stringstream stream(paths);
  std::string current_path;
  while (std::getline(stream, current_path, ',')) {
    if (!current_path.empty()) result.push_back(current_path);
  } return result;
}

// Read the line features of a dataset that intersect the study area, cloning their geometries and attributes for reuse
struct Line_class {
  std::string semantic_class;
  std::vector<OGRGeometry*> geometries;
  std::vector<std::map<std::string, std::string>> attributes;
};

void read_line_features(const char *path, const Config &config, Line_class &line_class) {
  GDALDataset *dataset = (GDALDataset*) GDALOpenEx(path, GDAL_OF_READONLY, NULL, NULL, NULL);
  if (dataset == NULL) {
    std::cerr << "Error: Could not open line dataset: " << path << std::endl;
    return;
  } for (auto &&layer: dataset->GetLayers()) {
    layer->ResetReading();
    layer->SetSpatialFilterRect(config.study_x_min, config.study_y_min, config.study_x_max, config.study_y_max);
    OGRFeature *feature;
    while ((feature = layer->GetNextFeature()) != NULL) {
      OGRGeometry *geometry = feature->GetGeometryRef();
      if (geometry != NULL) {
        line_class.geometries.push_back(geometry->clone());
        std::map<std::string, std::string> attributes;
        for (int field = 0; field < feature->GetFieldCount(); ++field) {
          const OGRFieldDefn *field_definition = feature->GetFieldDefnRef(field);
          if (field_definition == NULL) continue;
          const char *field_value = feature->GetFieldAsString(field);
          if (field_value != NULL) attributes[field_definition->GetNameRef()] = field_value;
        } line_class.attributes.push_back(attributes);
      }
      OGRFeature::DestroyFeature(feature);
    }
  } GDALClose(dataset);
}

// Insert the ring constraints of a polygon into its triangulation and label the interior faces
void triangulate_polygon(Polygon &polygon) {
  std::vector<Kernel::Point_2>::const_iterator current_point = polygon.outer_ring.points.begin();
  Triangulation::Vertex_handle current_vertex = polygon.triangulation.insert(*current_point);
  ++current_point;
  Triangulation::Vertex_handle previous_vertex;
  while (current_point != polygon.outer_ring.points.end()) {
    previous_vertex = current_vertex;
    current_vertex = polygon.triangulation.insert(*current_point);
    if (previous_vertex != current_vertex) polygon.triangulation.odd_even_insert_constraint(previous_vertex, current_vertex);
    ++current_point;
  } for (auto const &ring: polygon.inner_rings) {
    current_point = ring.points.begin();
    current_vertex = polygon.triangulation.insert(*current_point);
    while (current_point != ring.points.end()) {
      previous_vertex = current_vertex;
      current_vertex = polygon.triangulation.insert(*current_point);
      if (previous_vertex != current_vertex) polygon.triangulation.odd_even_insert_constraint(previous_vertex, current_vertex);
      ++current_point;
    }
  } if (polygon.triangulation.number_of_faces() > 0) label_polygon(polygon);
}

// A single noded piece of a road/railway/stream line, inheriting the class and attributes of its source feature
struct Road_segment {
  double x1, y1, x2, y2;
  double min_x, min_y, max_x, max_y;
  std::string semantic_class;
  std::map<std::string, std::string> attributes;
  Road_segment(double x1, double y1, double x2, double y2, const std::string &semantic_class,
               const std::map<std::string, std::string> &attributes) :
      x1(x1), y1(y1), x2(x2), y2(y2), semantic_class(semantic_class), attributes(attributes) {
    min_x = std::min(x1, x2);
    max_x = std::max(x1, x2);
    min_y = std::min(y1, y2);
    max_y = std::max(y1, y2);
  }
};

// Uniform grid over the study area indexing road segments by the cells their bounding box overlaps
struct Segment_grid {
  double cell_size, min_x, min_y, max_x, max_y;
  std::size_t nx, ny;
  std::vector<std::vector<std::size_t>> cells;
  
  void build(const std::vector<Road_segment> &segments, const Config &config) {
    cell_size = std::max(config.line_classification_distance, 1.0);
    min_x = config.study_x_min;
    min_y = config.study_y_min;
    max_x = config.study_x_max;
    max_y = config.study_y_max;
    nx = std::max<std::size_t>(1, std::ceil((max_x-min_x)/cell_size));
    ny = std::max<std::size_t>(1, std::ceil((max_y-min_y)/cell_size));
    cells.assign(nx*ny, {});
    for (std::size_t i = 0; i < segments.size(); ++i) {
      std::size_t cx1 = cell_index_x(segments[i].min_x);
      std::size_t cx2 = cell_index_x(segments[i].max_x);
      std::size_t cy1 = cell_index_y(segments[i].min_y);
      std::size_t cy2 = cell_index_y(segments[i].max_y);
      for (std::size_t cx = cx1; cx <= cx2; ++cx) {
        for (std::size_t cy = cy1; cy <= cy2; ++cy) cells[cy*nx+cx].push_back(i);
      }
    }
  }
  
  // Collect the indices of all segments whose bounding box is within `margin` of the given point
  void query(double x, double y, double margin, std::vector<std::size_t> &result) const {
    const double expanded = margin+cell_size;
    long cx1 = (long)std::floor((x-expanded-min_x)/cell_size);
    long cx2 = (long)std::floor((x+expanded-min_x)/cell_size);
    long cy1 = (long)std::floor((y-expanded-min_y)/cell_size);
    long cy2 = (long)std::floor((y+expanded-min_y)/cell_size);
    if (cx1 < 0) cx1 = 0;
    if (cx2 > (long)(nx-1)) cx2 = (long)(nx-1);
    if (cy1 < 0) cy1 = 0;
    if (cy2 > (long)(ny-1)) cy2 = (long)(ny-1);
    for (long cx = cx1; cx <= cx2; ++cx) {
      for (long cy = cy1; cy <= cy2; ++cy) {
        for (auto const &segment: cells[(std::size_t)cy*nx+(std::size_t)cx]) result.push_back(segment);
      }
    }
  }
  
private:
  std::size_t cell_index_x(double x) const {
    long index = (long)std::floor((x-min_x)/cell_size);
    if (index < 0) index = 0;
    if (index > (long)(nx-1)) index = (long)(nx-1);
    return (std::size_t)index;
  }
  std::size_t cell_index_y(double y) const {
    long index = (long)std::floor((y-min_y)/cell_size);
    if (index < 0) index = 0;
    if (index > (long)(ny-1)) index = (long)(ny-1);
    return (std::size_t)index;
  }
};

double point_segment_distance(double px, double py, const Road_segment &segment) {
  const double dx = segment.x2-segment.x1, dy = segment.y2-segment.y1;
  const double length_squared = dx*dx+dy*dy;
  if (length_squared == 0.0) return std::hypot(px-segment.x1, py-segment.y1);
  const double t = std::max(0.0, std::min(1.0, ((px-segment.x1)*dx+(py-segment.y1)*dy)/length_squared));
  return std::hypot(px-(segment.x1+t*dx), py-(segment.y1+t*dy));
}

// Split the generated road polygons by proximity to noded INEGI line segments:
// vialidad_l -> Road, via_ferrea_l -> Railway, corriente_ag_l -> WaterBody.
// Each triangle of the road triangulation is assigned the class of its nearest segment
// (within line_classification_distance); on ties, Road wins (checked first). Triangles
// with no segment nearby keep the Road default. Adjacent triangles sharing a segment are
// then merged into polygons carrying that segment's class and attributes.
bool classify_road_polygons(const Config &config, std::vector<OGRPolygon*> &road_polygons,
                            std::vector<std::string> &road_classes, std::vector<std::map<std::string, std::string>> &road_attributes) {
  // Returns true when road_polygons has been replaced by owned per-segment polygons
  // (which the caller must destroy), false when it is left as-is pointing into roads_geom.
  // Priority order Road > Railway > WaterBody decides ties (strictly-smaller replaces)
  std::vector<Line_class> line_classes;
  if (!config.road_lines_path.empty()) {
    Line_class line_class;
    line_class.semantic_class = "Road";
    read_line_features(config.road_lines_path.c_str(), config, line_class);
    line_classes.push_back(std::move(line_class));
  } if (!config.railway_lines_path.empty()) {
    Line_class line_class;
    line_class.semantic_class = "Railway";
    read_line_features(config.railway_lines_path.c_str(), config, line_class);
    line_classes.push_back(std::move(line_class));
  } if (!config.stream_lines_path.empty()) {
    Line_class line_class;
    line_class.semantic_class = "WaterBody";
    read_line_features(config.stream_lines_path.c_str(), config, line_class);
    line_classes.push_back(std::move(line_class));
  }
  if (line_classes.empty()) {
    std::cout << "No line layers for road classification; keeping all generated polygons as Road." << std::endl;
    return false;
  }
  
  const double classification_distance = config.line_classification_distance;
  
  // Node all line features (road, railway, stream) into segments at their mutual intersections
  OGRMultiLineString all_lines;
  for (auto const &line_class: line_classes) {
    for (auto *geometry: line_class.geometries) {
      if (wkbFlatten(geometry->getGeometryType()) == wkbLineString) {
        all_lines.addGeometry(geometry);
      } else if (wkbFlatten(geometry->getGeometryType()) == wkbMultiLineString) {
        OGRMultiLineString *multiline = geometry->toMultiLineString();
        for (int i = 0; i < multiline->getNumGeometries(); ++i) all_lines.addGeometry(multiline->getGeometryRef(i));
      }
    }
  }
  if (all_lines.getNumGeometries() == 0) {
    std::cout << "No lines found in the study area; keeping all generated polygons as Road." << std::endl;
    return false;
  }
  OGRGeometry *noded_lines = all_lines.UnaryUnion();
  if (noded_lines == NULL || noded_lines->IsEmpty()) {
    std::cerr << "Error: Line noding failed." << std::endl;
    return false;
  }
  
  // Re-associate each noded piece with its source feature to recover its class and attributes
  std::vector<Road_segment> segments;
  auto add_noded_piece = [&](const OGRLineString *piece) {
    const int n_points = piece->getNumPoints();
    if (n_points < 2) return;
    const double mx = (piece->getX(0)+piece->getX(1))/2.0, my = (piece->getY(0)+piece->getY(1))/2.0;
    OGRPoint midpoint(mx, my);
    std::size_t best_class_index = 0, best_feature_index = 0;
    double best_distance = std::numeric_limits<double>::max();
    for (std::size_t c = 0; c < line_classes.size(); ++c) {
      for (std::size_t f = 0; f < line_classes[c].geometries.size(); ++f) {
        OGREnvelope envelope;
        line_classes[c].geometries[f]->getEnvelope(&envelope);
        if (mx < envelope.MinX-1e-3 || mx > envelope.MaxX+1e-3 || my < envelope.MinY-1e-3 || my > envelope.MaxY+1e-3) continue;
        const double distance = midpoint.Distance(line_classes[c].geometries[f]);
        if (distance < best_distance) {
          best_distance = distance;
          best_class_index = c;
          best_feature_index = f;
        }
      }
    } if (best_distance == std::numeric_limits<double>::max()) return;
    for (int v = 0; v < n_points-1; ++v) {
      segments.emplace_back(piece->getX(v), piece->getY(v), piece->getX(v+1), piece->getY(v+1),
                            line_classes[best_class_index].semantic_class,
                            line_classes[best_class_index].attributes[best_feature_index]);
    }
  };
  if (wkbFlatten(noded_lines->getGeometryType()) == wkbLineString) {
    add_noded_piece(noded_lines->toLineString());
  } else if (wkbFlatten(noded_lines->getGeometryType()) == wkbMultiLineString) {
    OGRMultiLineString *multiline = noded_lines->toMultiLineString();
    for (int i = 0; i < multiline->getNumGeometries(); ++i) add_noded_piece(multiline->getGeometryRef(i)->toLineString());
  }
  OGRGeometryFactory::destroyGeometry(noded_lines);
  if (segments.empty()) {
    std::cout << "No noded line segments; keeping all generated polygons as Road." << std::endl;
    return false;
  }
  std::cout << "Noded line segments: " << segments.size() << std::endl;
  
  // Index the segments in a grid for nearest-segment queries
  Segment_grid grid;
  grid.build(segments, config);
  
  // Split each road polygon into per-segment polygons
  std::vector<OGRPolygon*> split_polygons;
  std::vector<std::string> split_classes;
  std::vector<std::map<std::string, std::string>> split_attributes;
  for (std::size_t i = 0; i < road_polygons.size(); ++i) {
    Polygon polygon;
    OGRLinearRing *outer_ring = road_polygons[i]->getExteriorRing();
    for (int v = 0; v < outer_ring->getNumPoints(); ++v) {
      polygon.outer_ring.points.emplace_back(outer_ring->getX(v), outer_ring->getY(v));
    } for (int r = 0; r < road_polygons[i]->getNumInteriorRings(); ++r) {
      polygon.inner_rings.emplace_back();
      OGRLinearRing *inner_ring = road_polygons[i]->getInteriorRing(r);
      for (int v = 0; v < inner_ring->getNumPoints(); ++v) {
        polygon.inner_rings.back().points.emplace_back(inner_ring->getX(v), inner_ring->getY(v));
      }
    }
    triangulate_polygon(polygon);
    if (polygon.triangulation.number_of_faces() == 0) {
      split_polygons.push_back(road_polygons[i]->clone()->toPolygon());
      split_classes.push_back("Road");
      split_attributes.emplace_back();
      continue;
    }
    
    // Classify each interior triangle by its nearest segment
    for (auto const &face: polygon.triangulation.finite_face_handles()) {
      face->info().road_segment = -1;
      face->info().grouped = false;
    } for (auto const &face: polygon.triangulation.finite_face_handles()) {
      if (!face->info().interior) continue;
      const double cx = (CGAL::to_double(face->vertex(0)->point().x())+CGAL::to_double(face->vertex(1)->point().x())+CGAL::to_double(face->vertex(2)->point().x()))/3.0;
      const double cy = (CGAL::to_double(face->vertex(0)->point().y())+CGAL::to_double(face->vertex(1)->point().y())+CGAL::to_double(face->vertex(2)->point().y()))/3.0;
      std::vector<std::size_t> candidates;
      grid.query(cx, cy, classification_distance, candidates);
      double best_distance = std::numeric_limits<double>::max();
      std::size_t best_segment = 0;
      for (auto const &line_class: line_classes) {
        double min_distance = std::numeric_limits<double>::max();
        std::size_t nearest_segment = 0;
        for (auto const &segment_index: candidates) {
          const Road_segment &segment = segments[segment_index];
          if (segment.semantic_class != line_class.semantic_class) continue;
          const double distance = point_segment_distance(cx, cy, segment);
          if (distance < min_distance) {
            min_distance = distance;
            nearest_segment = segment_index;
          }
        } if (min_distance < best_distance) {
          best_distance = min_distance;
          best_segment = nearest_segment;
        }
      } if (best_distance <= classification_distance) face->info().road_segment = best_segment;
    }
    
    // Merge adjacent triangles belonging to the same segment into groups
    std::vector<std::vector<Triangulation::Face_handle>> groups;
    for (auto const &face: polygon.triangulation.finite_face_handles()) {
      if (!face->info().interior || face->info().grouped) continue;
      std::vector<Triangulation::Face_handle> group;
      std::list<Triangulation::Face_handle> to_check;
      face->info().grouped = true;
      to_check.push_back(face);
      while (!to_check.empty()) {
        Triangulation::Face_handle current = to_check.front();
        to_check.pop_front();
        group.push_back(current);
        for (int neighbour = 0; neighbour < 3; ++neighbour) {
          Triangulation::Face_handle next = current->neighbor(neighbour);
          if (!polygon.triangulation.is_infinite(next) && next->info().interior && !next->info().grouped && next->info().road_segment == face->info().road_segment) {
            next->info().grouped = true;
            to_check.push_back(next);
          }
        }
      } groups.push_back(group);
    }
    
    // Union the triangles of each group into polygons carrying the segment's class and attributes
    for (auto const &group: groups) {
      std::string semantic_class = "Road";
      std::map<std::string, std::string> attributes;
      if (group.front()->info().road_segment >= 0) {
        const Road_segment &segment = segments[group.front()->info().road_segment];
        semantic_class = segment.semantic_class;
        attributes = segment.attributes;
      }
      OGRMultiPolygon group_triangles;
      for (auto const &face: group) {
        OGRLinearRing ring;
        for (int v = 0; v < 3; ++v) ring.addPoint(CGAL::to_double(face->vertex(v)->point().x()), CGAL::to_double(face->vertex(v)->point().y()));
        ring.closeRings();
        OGRPolygon triangle;
        triangle.addRing(&ring);
        group_triangles.addGeometry(&triangle);
      }
      OGRGeometry *union_geometry = group_triangles.UnionCascaded();
      if (union_geometry == NULL || union_geometry->IsEmpty()) continue;
      const OGRwkbGeometryType union_type = wkbFlatten(union_geometry->getGeometryType());
      std::vector<OGRPolygon*> union_polygons;
      if (union_type == wkbPolygon) {
        union_polygons.push_back(union_geometry->toPolygon()->clone()->toPolygon());
      } else if (union_type == wkbMultiPolygon) {
        OGRMultiPolygon *multipolygon = union_geometry->toMultiPolygon();
        for (int current_polygon = 0; current_polygon < multipolygon->getNumGeometries(); ++current_polygon) {
          union_polygons.push_back(multipolygon->getGeometryRef(current_polygon)->clone()->toPolygon());
        }
      }
      OGRGeometryFactory::destroyGeometry(union_geometry);
      for (auto *polygon_result: union_polygons) {
        split_polygons.push_back(polygon_result);
        split_classes.push_back(semantic_class);
        split_attributes.push_back(attributes);
      }
    }
  }
  
  road_polygons = split_polygons;
  road_classes = split_classes;
  road_attributes = split_attributes;
  
  std::size_t n_road = 0, n_railway = 0, n_waterbody = 0;
  for (std::size_t i = 0; i < road_polygons.size(); ++i) {
    if (road_classes[i] == "Road") ++n_road;
    else if (road_classes[i] == "Railway") ++n_railway;
    else if (road_classes[i] == "WaterBody") ++n_waterbody;
  }
  std::cout << "Classified road polygons: " << n_road << " Road, " << n_railway << " Railway, " << n_waterbody << " WaterBody." << std::endl;
  for (auto &line_class: line_classes) {
    for (auto *geometry: line_class.geometries) OGRGeometryFactory::destroyGeometry(geometry);
  }
  return true;
}

// Generate road polygons: union of city blocks, water bodies and land-use features, then the complement within the study area
void generate_road_polygons(Config &config, Map &map) {
  if (config.city_blocks_path.empty()) {
    std::cerr << "Error: generate_roads requires a city_blocks path." << std::endl;
    return;
  }
  
  // Collect the non-road areas (city blocks, water bodies, land use)
  OGRMultiPolygon non_road_areas;
  std::size_t n_polygons = 0;
  
  GDALDataset *dataset = (GDALDataset*) GDALOpenEx(config.city_blocks_path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
  if (dataset == NULL) {
    std::cerr << "Error: Could not open city blocks dataset: " << config.city_blocks_path << std::endl;
    return;
  } std::cout << "Opening city blocks type: " << dataset->GetDriverName() << std::endl;
  read_polygon_features(dataset, config, non_road_areas, n_polygons, map);
  GDALClose(dataset);
  std::cout << "City blocks: " << n_polygons << std::endl;
  
  if (config.generate_waterbodies) {
    for (auto const &water_areas_path: split_paths(config.water_areas_path)) {
      n_polygons = 0;
      dataset = (GDALDataset*) GDALOpenEx(water_areas_path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
      if (dataset == NULL) {
        std::cerr << "Error: Could not open water areas dataset: " << water_areas_path << std::endl;
      } else {
        std::cout << "Opening water areas type: " << dataset->GetDriverName() << std::endl;
        read_polygon_features(dataset, config, non_road_areas, n_polygons, map);
        GDALClose(dataset);
        std::cout << "Water areas: " << n_polygons << std::endl;
      }
    }
  } else if (!config.waterbody_path.empty()) {
    n_polygons = 0;
    dataset = (GDALDataset*) GDALOpenEx(config.waterbody_path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
    if (dataset == NULL) {
      std::cerr << "Error: Could not open water bodies dataset: " << config.waterbody_path << std::endl;
    } else {
      std::cout << "Opening water bodies type: " << dataset->GetDriverName() << std::endl;
      read_polygon_features(dataset, config, non_road_areas, n_polygons, map);
      GDALClose(dataset);
      std::cout << "Water bodies: " << n_polygons << std::endl;
    }
  }
  
  for (auto const &land_use_path: split_paths(config.land_use_path)) {
    n_polygons = 0;
    dataset = (GDALDataset*) GDALOpenEx(land_use_path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
    if (dataset == NULL) {
      std::cerr << "Error: Could not open land use dataset: " << land_use_path << std::endl;
    } else {
      std::cout << "Opening land use type: " << dataset->GetDriverName() << std::endl;
      read_polygon_features(dataset, config, non_road_areas, n_polygons, map);
      GDALClose(dataset);
      std::cout << "Land use: " << n_polygons << std::endl;
    }
  }
  
  // Union the non-road areas (GEOS handles shared/overlapping boundaries robustly)
  OGRGeometry *non_road_union = non_road_areas.UnionCascaded();
  if (non_road_union == NULL || non_road_union->IsEmpty()) {
    std::cerr << "Error: Non-road union is empty." << std::endl;
    return;
  }
  
  // Study area rectangle
  OGRLinearRing study_ring;
  study_ring.addPoint(config.study_x_min, config.study_y_min);
  study_ring.addPoint(config.study_x_max, config.study_y_min);
  study_ring.addPoint(config.study_x_max, config.study_y_max);
  study_ring.addPoint(config.study_x_min, config.study_y_max);
  study_ring.addPoint(config.study_x_min, config.study_y_min);
  OGRPolygon study_polygon;
  study_polygon.addRing(&study_ring);
  
  // Roads = study area minus union of non-road areas
  OGRGeometry *roads_geom = study_polygon.Difference(non_road_union);
  if (roads_geom == NULL || roads_geom->IsEmpty()) {
    std::cerr << "Error: No road polygons could be computed." << std::endl;
    OGRGeometryFactory::destroyGeometry(non_road_union);
    OGRGeometryFactory::destroyGeometry(roads_geom);
    return;
  }
  
  // Collect resulting polygons
  std::vector<OGRPolygon*> road_polygons;
  OGRwkbGeometryType road_type = wkbFlatten(roads_geom->getGeometryType());
  if (road_type == wkbPolygon) {
    road_polygons.push_back(roads_geom->toPolygon());
  } else if (road_type == wkbMultiPolygon) {
    OGRMultiPolygon *multipolygon = roads_geom->toMultiPolygon();
    for (int current_polygon = 0; current_polygon < multipolygon->getNumGeometries(); ++current_polygon) {
      road_polygons.push_back(multipolygon->getGeometryRef(current_polygon));
    }
  }
  std::cout << "Road polygons: " << road_polygons.size() << std::endl;
  
  // Classify the gaps by proximity to INEGI line layers (Road/Railway/WaterBody)
  std::vector<std::string> road_classes(road_polygons.size(), "Road");
  std::vector<std::map<std::string, std::string>> road_attributes(road_polygons.size());
  const bool road_polygons_owned = classify_road_polygons(config, road_polygons, road_classes, road_attributes);
  
  // Write to output file if requested
  if (!config.roads_output_path.empty()) {
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (driver == NULL) {
      std::cerr << "Error: GPKG driver not available." << std::endl;
    } else {
      GDALDataset *output = driver->Create(config.roads_output_path.c_str(), 0, 0, 0, GDT_Unknown, NULL);
      if (output == NULL) {
        std::cerr << "Error: Could not create " << config.roads_output_path << std::endl;
      } else {
        OGRSpatialReference output_srs;
        output_srs.SetFromUserInput(("EPSG:" + map.crs_code).c_str());
        OGRLayer *layer = output->CreateLayer("Road", &output_srs, wkbPolygon, NULL);
        if (layer != NULL) {
          OGRFieldDefn class_field("class", OFTString);
          class_field.SetWidth(16);
          layer->CreateField(&class_field);
          for (std::size_t i = 0; i < road_polygons.size(); ++i) {
            for (auto const &attribute: road_attributes[i]) {
              if (layer->FindFieldIndex(attribute.first.c_str(), TRUE) < 0) {
                OGRFieldDefn attribute_field(attribute.first.c_str(), OFTString);
                attribute_field.SetWidth(64);
                layer->CreateField(&attribute_field);
              }
            }
            OGRFeature *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feature->SetGeometry(road_polygons[i]);
            feature->SetField("class", road_classes[i].c_str());
            for (auto const &attribute: road_attributes[i]) feature->SetField(attribute.first.c_str(), attribute.second.c_str());
            if (layer->CreateFeature(feature) != OGRERR_NONE) {
              std::cerr << "Error: Could not write road polygon." << std::endl;
            } OGRFeature::DestroyFeature(feature);
          }
        } GDALClose(output);
        std::cout << "Wrote road polygons to " << config.roads_output_path << std::endl;
      }
    }
  }
  
  // Inject into the map
  std::size_t n_road = 0, n_railway = 0, n_waterbody = 0;
  for (std::size_t i = 0; i < road_polygons.size(); ++i) {
    OGRPolygon *road_polygon = road_polygons[i];
    map.polygons.emplace_back();
    if (road_classes[i] == "Railway") map.polygons.back().id = "railway-" + std::to_string(n_railway++);
    else if (road_classes[i] == "WaterBody") map.polygons.back().id = "waterbody-" + std::to_string(n_waterbody++);
    else map.polygons.back().id = "road-" + std::to_string(n_road++);
    map.polygons.back().semantic_class = road_classes[i];
    for (auto const &attribute: road_attributes[i]) map.polygons.back().attributes[attribute.first] = attribute.second;
    OGRLinearRing *outer_ring = road_polygon->getExteriorRing();
    for (int current_vertex = 0; current_vertex < outer_ring->getNumPoints(); ++current_vertex) {
      map.polygons.back().outer_ring.points.emplace_back(outer_ring->getX(current_vertex), outer_ring->getY(current_vertex));
    } for (int current_inner_ring = 0; current_inner_ring < road_polygon->getNumInteriorRings(); ++current_inner_ring) {
      map.polygons.back().inner_rings.emplace_back();
      OGRLinearRing *inner_ring = road_polygon->getInteriorRing(current_inner_ring);
      for (int current_vertex = 0; current_vertex < inner_ring->getNumPoints(); ++current_vertex) {
        map.polygons.back().inner_rings.back().points.emplace_back(inner_ring->getX(current_vertex), inner_ring->getY(current_vertex));
      }
    }
  }
  
  // Free the per-segment polygons generated by the splitter (when it ran)
  if (road_polygons_owned) {
    for (auto *polygon: road_polygons) OGRGeometryFactory::destroyGeometry(polygon);
  }
  
  OGRGeometryFactory::destroyGeometry(non_road_union);
  OGRGeometryFactory::destroyGeometry(roads_geom);
}

// Add a clipped OGR polygon to the map with the given semantic class
std::size_t add_clipped_polygon_to_map(OGRPolygon *clipped_polygon, Map &map, const std::string &semantic_class, std::size_t polygon_index) {
  map.polygons.emplace_back();
  map.polygons.back().id = semantic_class + "-" + std::to_string(polygon_index);
  map.polygons.back().semantic_class = semantic_class;
  OGRLinearRing *outer_ring = clipped_polygon->getExteriorRing();
  for (int current_vertex = 0; current_vertex < outer_ring->getNumPoints(); ++current_vertex) {
    map.polygons.back().outer_ring.points.emplace_back(outer_ring->getX(current_vertex), outer_ring->getY(current_vertex));
  } for (int current_inner_ring = 0; current_inner_ring < clipped_polygon->getNumInteriorRings(); ++current_inner_ring) {
    map.polygons.back().inner_rings.emplace_back();
    OGRLinearRing *inner_ring = clipped_polygon->getInteriorRing(current_inner_ring);
    for (int current_vertex = 0; current_vertex < inner_ring->getNumPoints(); ++current_vertex) {
      map.polygons.back().inner_rings.back().points.emplace_back(inner_ring->getX(current_vertex), inner_ring->getY(current_vertex));
    }
  } return 1;
}

// Convert a map polygon to an OGR polygon geometry (rings are closed)
OGRPolygon *polygon_to_ogr(const Polygon &polygon) {
  OGRLinearRing *outer_ring = new OGRLinearRing();
  for (auto const &point: polygon.outer_ring.points) outer_ring->addPoint(CGAL::to_double(point.x()), CGAL::to_double(point.y()));
  outer_ring->closeRings();
  OGRPolygon *ogr_polygon = new OGRPolygon();
  ogr_polygon->addRingDirectly(outer_ring);
  for (auto const &ring: polygon.inner_rings) {
    OGRLinearRing *inner_ring = new OGRLinearRing();
    for (auto const &point: ring.points) inner_ring->addPoint(CGAL::to_double(point.x()), CGAL::to_double(point.y()));
    inner_ring->closeRings();
    ogr_polygon->addRingDirectly(inner_ring);
  } return ogr_polygon;
}

// Subtract the given geometry from a polygon, appending the resulting polygon(s) (with the source's class, id and attributes) to the destination vector
void subtract_geometry(const Polygon &source, OGRGeometry *subtractor, std::vector<Polygon> &destination) {
  if (subtractor == NULL || subtractor->IsEmpty()) {
    destination.push_back(source);
    return;
  }
  OGRPolygon *ogr_polygon = polygon_to_ogr(source);
  OGRGeometry *result = ogr_polygon->Difference(subtractor);
  OGRGeometryFactory::destroyGeometry(ogr_polygon);
  if (result == NULL || result->IsEmpty()) {
    if (result != NULL) OGRGeometryFactory::destroyGeometry(result);
    return;
  }
  std::vector<OGRPolygon*> result_polygons;
  OGRwkbGeometryType result_type = wkbFlatten(result->getGeometryType());
  if (result_type == wkbPolygon) result_polygons.push_back(result->toPolygon());
  else if (result_type == wkbMultiPolygon) {
    OGRMultiPolygon *multipolygon = result->toMultiPolygon();
    for (int current_polygon = 0; current_polygon < multipolygon->getNumGeometries(); ++current_polygon) {
      result_polygons.push_back(multipolygon->getGeometryRef(current_polygon));
    }
  }
  std::size_t piece = 1;
  for (OGRPolygon *result_polygon: result_polygons) {
    destination.push_back(source);
    Polygon &new_polygon = destination.back();
    if (piece > 1) new_polygon.id = source.id + "-" + std::to_string(piece);
    ++piece;
    new_polygon.outer_ring.points.clear();
    new_polygon.inner_rings.clear();
    OGRLinearRing *outer_ring = result_polygon->getExteriorRing();
    for (int current_vertex = 0; current_vertex < outer_ring->getNumPoints(); ++current_vertex) {
      new_polygon.outer_ring.points.emplace_back(outer_ring->getX(current_vertex), outer_ring->getY(current_vertex));
    } for (int current_inner_ring = 0; current_inner_ring < result_polygon->getNumInteriorRings(); ++current_inner_ring) {
      new_polygon.inner_rings.emplace_back();
      OGRLinearRing *inner_ring = result_polygon->getInteriorRing(current_inner_ring);
      for (int current_vertex = 0; current_vertex < inner_ring->getNumPoints(); ++current_vertex) {
        new_polygon.inner_rings.back().points.emplace_back(inner_ring->getX(current_vertex), inner_ring->getY(current_vertex));
      }
    }
  } OGRGeometryFactory::destroyGeometry(result);
}

// Resolve overlaps between PlantCover, WaterBody and Terrain polygons so that the
// higher-priority class keeps the shared area (priority: WaterBody > PlantCover > Terrain)
void resolve_area_overlaps(Map &map) {
  // Collect the higher-priority polygons into multipolygons
  OGRMultiPolygon waterbody_areas, plantcover_areas;
  for (auto const &polygon: map.polygons) {
    if (polygon.semantic_class == "WaterBody") {
      OGRPolygon *ogr_polygon = polygon_to_ogr(polygon);
      waterbody_areas.addGeometry(ogr_polygon);
      OGRGeometryFactory::destroyGeometry(ogr_polygon);
    } else if (polygon.semantic_class == "PlantCover") {
      OGRPolygon *ogr_polygon = polygon_to_ogr(polygon);
      plantcover_areas.addGeometry(ogr_polygon);
      OGRGeometryFactory::destroyGeometry(ogr_polygon);
    }
  }
  
  // Union the higher-priority classes
  OGRGeometry *waterbody_union = waterbody_areas.getNumGeometries() > 0 ? waterbody_areas.UnionCascaded() : NULL;
  OGRGeometry *plantcover_union = plantcover_areas.getNumGeometries() > 0 ? plantcover_areas.UnionCascaded() : NULL;
  
  // For terrain, subtract both plant cover and water bodies
  OGRGeometry *terrain_subtractor = NULL;
  if (waterbody_union != NULL && plantcover_union != NULL) terrain_subtractor = waterbody_union->Union(plantcover_union);
  else if (waterbody_union != NULL) terrain_subtractor = waterbody_union->clone();
  else if (plantcover_union != NULL) terrain_subtractor = plantcover_union->clone();
  
  std::vector<Polygon> new_polygons;
  new_polygons.reserve(map.polygons.size());
  for (auto &polygon: map.polygons) {
    if (polygon.semantic_class == "PlantCover") subtract_geometry(polygon, waterbody_union, new_polygons);
    else if (polygon.semantic_class == "Terrain") subtract_geometry(polygon, terrain_subtractor, new_polygons);
    else new_polygons.push_back(std::move(polygon));
  } map.polygons.swap(new_polygons);
  
  if (terrain_subtractor != NULL) OGRGeometryFactory::destroyGeometry(terrain_subtractor);
  if (waterbody_union != NULL) OGRGeometryFactory::destroyGeometry(waterbody_union);
  if (plantcover_union != NULL) OGRGeometryFactory::destroyGeometry(plantcover_union);
}

// Read the polygon features of one or more comma-separated datasets, clip them to the study area, and load them into the map with the given semantic class
std::size_t read_clipped_polygon_features(const std::string &paths, const Config &config, Map &map, const std::string &semantic_class) {
  // Study area rectangle
  OGRLinearRing study_ring;
  study_ring.addPoint(config.study_x_min, config.study_y_min);
  study_ring.addPoint(config.study_x_max, config.study_y_min);
  study_ring.addPoint(config.study_x_max, config.study_y_max);
  study_ring.addPoint(config.study_x_min, config.study_y_max);
  study_ring.addPoint(config.study_x_min, config.study_y_min);
  OGRPolygon study_polygon;
  study_polygon.addRing(&study_ring);
  
  std::size_t n_polygons = 0;
  for (auto const &path: split_paths(paths)) {
    GDALDataset *dataset = (GDALDataset*) GDALOpenEx(path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
    if (dataset == NULL) {
      std::cerr << "Error: Could not open dataset: " << path << std::endl;
      continue;
    }
    for (auto &&input_layer: dataset->GetLayers()) {
      input_layer->ResetReading();
      input_layer->SetSpatialFilterRect(config.study_x_min, config.study_y_min, config.study_x_max, config.study_y_max);
      
      // Extract CRS from this layer
      const OGRSpatialReference *spatial_reference = input_layer->GetSpatialRef();
      if (spatial_reference != NULL) {
        const char *authority = spatial_reference->GetAuthorityName(NULL);
        if (authority != NULL) map.crs_authority = std::string(authority);
        const char *code = spatial_reference->GetAuthorityCode(NULL);
        if (code != NULL) map.crs_code = std::string(code);
      }
      
      OGRFeature *input_feature;
      while ((input_feature = input_layer->GetNextFeature()) != NULL) {
        if (!input_feature->GetGeometryRef()) continue;
        
        // Clip each feature to the study area (GEOS handles boundary cases robustly)
        OGRGeometry *clipped = input_feature->GetGeometryRef()->Intersection(&study_polygon);
        if (clipped == NULL || clipped->IsEmpty()) {
          if (clipped != NULL) OGRGeometryFactory::destroyGeometry(clipped);
          continue;
        }
        
        OGRwkbGeometryType clipped_type = wkbFlatten(clipped->getGeometryType());
        if (clipped_type == wkbPolygon) {
          n_polygons += add_clipped_polygon_to_map(clipped->toPolygon(), map, semantic_class, n_polygons);
        } else if (clipped_type == wkbMultiPolygon) {
          OGRMultiPolygon *clipped_multipolygon = clipped->toMultiPolygon();
          for (int current_polygon = 0; current_polygon < clipped_multipolygon->getNumGeometries(); ++current_polygon) {
            n_polygons += add_clipped_polygon_to_map(clipped_multipolygon->getGeometryRef(current_polygon), map, semantic_class, n_polygons);
          }
        }
        OGRGeometryFactory::destroyGeometry(clipped);
      }
    }
    GDALClose(dataset);
  }
  return n_polygons;
}

// Write the map polygons with the given semantic class to a GPKG file
void write_polygons_to_gpkg(const std::string &output_path, const Map &map, const std::string &semantic_class, std::size_t first_polygon) {
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
  if (driver == NULL) {
    std::cerr << "Error: GPKG driver not available." << std::endl;
    return;
  }
  GDALDataset *output = driver->Create(output_path.c_str(), 0, 0, 0, GDT_Unknown, NULL);
  if (output == NULL) {
    std::cerr << "Error: Could not create " << output_path << std::endl;
    return;
  }
  OGRSpatialReference output_srs;
  output_srs.SetFromUserInput(("EPSG:" + map.crs_code).c_str());
  OGRLayer *layer = output->CreateLayer(semantic_class.c_str(), &output_srs, wkbPolygon, NULL);
  if (layer != NULL) {
    for (std::size_t i = first_polygon; i < map.polygons.size(); ++i) {
      const Polygon &polygon = map.polygons[i];
      if (polygon.semantic_class != semantic_class) continue;
      OGRLinearRing outer_ring;
      for (auto const &point: polygon.outer_ring.points) outer_ring.addPoint(CGAL::to_double(point.x()), CGAL::to_double(point.y()));
      outer_ring.closeRings();
      OGRPolygon polygon_geom;
      polygon_geom.addRing(&outer_ring);
      for (auto const &ring: polygon.inner_rings) {
        OGRLinearRing inner_ring;
        for (auto const &point: ring.points) inner_ring.addPoint(CGAL::to_double(point.x()), CGAL::to_double(point.y()));
        inner_ring.closeRings();
        polygon_geom.addRing(&inner_ring);
      }
      OGRFeature *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
      feature->SetGeometry(&polygon_geom);
      if (layer->CreateFeature(feature) != OGRERR_NONE) {
        std::cerr << "Error: Could not write " << semantic_class << " polygon." << std::endl;
      } OGRFeature::DestroyFeature(feature);
    }
  } GDALClose(output);
  std::cout << "Wrote " << semantic_class << " polygons to " << output_path << std::endl;
}

// Generate plant cover polygons: INEGI public areas (area_publica_a) clipped to the study area
void generate_plantcover_polygons(Config &config, Map &map) {
  if (config.public_areas_path.empty()) {
    std::cerr << "Error: generate_plantcover requires a public_areas path." << std::endl;
    return;
  }
  
  const std::size_t first_polygon = map.polygons.size();
  const std::size_t n_polygons = read_clipped_polygon_features(config.public_areas_path, config, map, "PlantCover");
  std::cout << "Plant cover polygons: " << n_polygons << std::endl;
  
  // Write to output file if requested
  if (!config.plantcover_output_path.empty()) write_polygons_to_gpkg(config.plantcover_output_path, map, "PlantCover", first_polygon);
}

// Generate water body polygons: INEGI water area layers (cuerpo_agua_a, estanque_a, canal_a, corriente_ag_a) clipped to the study area
void generate_waterbodies_polygons(Config &config, Map &map) {
  if (config.water_areas_path.empty()) {
    std::cerr << "Error: generate_waterbodies requires a water_areas path." << std::endl;
    return;
  }
  
  const std::size_t first_polygon = map.polygons.size();
  const std::size_t n_polygons = read_clipped_polygon_features(config.water_areas_path, config, map, "WaterBody");
  std::cout << "Water body polygons: " << n_polygons << std::endl;
  
  // Write to output file if requested
  if (!config.waterbody_output_path.empty()) write_polygons_to_gpkg(config.waterbody_output_path, map, "WaterBody", first_polygon);
}

// Generate terrain polygons: union of city blocks (manzana_a) clipped to the study area
void generate_terrain_polygons(Config &config, Map &map) {
  if (config.city_blocks_path.empty()) {
    std::cerr << "Error: generate_terrain requires a city_blocks path." << std::endl;
    return;
  }
  
  const std::size_t first_polygon = map.polygons.size();
  
  // Collect the city blocks
  OGRMultiPolygon city_block_areas;
  std::size_t n_polygons = 0;
  GDALDataset *dataset = (GDALDataset*) GDALOpenEx(config.city_blocks_path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
  if (dataset == NULL) {
    std::cerr << "Error: Could not open city blocks dataset: " << config.city_blocks_path << std::endl;
    return;
  } std::cout << "Opening city blocks type: " << dataset->GetDriverName() << std::endl;
  read_polygon_features(dataset, config, city_block_areas, n_polygons, map);
  GDALClose(dataset);
  std::cout << "City blocks: " << n_polygons << std::endl;
  
  // Union the city blocks (GEOS handles shared/overlapping boundaries robustly)
  OGRGeometry *city_block_union = city_block_areas.UnionCascaded();
  if (city_block_union == NULL || city_block_union->IsEmpty()) {
    std::cerr << "Error: City block union is empty." << std::endl;
    return;
  }
  
  // Clip to the study area
  OGRLinearRing study_ring;
  study_ring.addPoint(config.study_x_min, config.study_y_min);
  study_ring.addPoint(config.study_x_max, config.study_y_min);
  study_ring.addPoint(config.study_x_max, config.study_y_max);
  study_ring.addPoint(config.study_x_min, config.study_y_max);
  study_ring.addPoint(config.study_x_min, config.study_y_min);
  OGRPolygon study_polygon;
  study_polygon.addRing(&study_ring);
  OGRGeometry *terrain_geom = city_block_union->Intersection(&study_polygon);
  OGRGeometryFactory::destroyGeometry(city_block_union);
  if (terrain_geom == NULL || terrain_geom->IsEmpty()) {
    std::cerr << "Error: No terrain polygons could be computed." << std::endl;
    if (terrain_geom != NULL) OGRGeometryFactory::destroyGeometry(terrain_geom);
    return;
  }
  
  // Collect resulting polygons
  std::vector<OGRPolygon*> terrain_polygons;
  OGRwkbGeometryType terrain_type = wkbFlatten(terrain_geom->getGeometryType());
  if (terrain_type == wkbPolygon) {
    terrain_polygons.push_back(terrain_geom->toPolygon());
  } else if (terrain_type == wkbMultiPolygon) {
    OGRMultiPolygon *multipolygon = terrain_geom->toMultiPolygon();
    for (int current_polygon = 0; current_polygon < multipolygon->getNumGeometries(); ++current_polygon) {
      terrain_polygons.push_back(multipolygon->getGeometryRef(current_polygon));
    }
  }
  std::cout << "Terrain polygons: " << terrain_polygons.size() << std::endl;
  
  // Inject into the map
  std::size_t n_terrain = 0;
  for (std::size_t i = 0; i < terrain_polygons.size(); ++i) {
    OGRPolygon *terrain_polygon = terrain_polygons[i];
    map.polygons.emplace_back();
    map.polygons.back().id = "terrain-" + std::to_string(n_terrain++);
    map.polygons.back().semantic_class = "Terrain";
    OGRLinearRing *outer_ring = terrain_polygon->getExteriorRing();
    for (int current_vertex = 0; current_vertex < outer_ring->getNumPoints(); ++current_vertex) {
      map.polygons.back().outer_ring.points.emplace_back(outer_ring->getX(current_vertex), outer_ring->getY(current_vertex));
    } for (int current_inner_ring = 0; current_inner_ring < terrain_polygon->getNumInteriorRings(); ++current_inner_ring) {
      map.polygons.back().inner_rings.emplace_back();
      OGRLinearRing *inner_ring = terrain_polygon->getInteriorRing(current_inner_ring);
      for (int current_vertex = 0; current_vertex < inner_ring->getNumPoints(); ++current_vertex) {
        map.polygons.back().inner_rings.back().points.emplace_back(inner_ring->getX(current_vertex), inner_ring->getY(current_vertex));
      }
    }
  }
  OGRGeometryFactory::destroyGeometry(terrain_geom);
  
  // Write to output file if requested
  if (!config.terrain_output_path.empty()) write_polygons_to_gpkg(config.terrain_output_path, map, "Terrain", first_polygon);
}

// Check whether a raster value is NODATA (exact or within a relative tolerance)
bool is_nodata_value(float value, int has_nodata, double nodata) {
  if (!has_nodata) return false;
  if (value == nodata) return true;
  return std::abs(double(value)-nodata) <= 1e-6*std::max(std::abs(double(value)), std::abs(nodata));
}

// Visvalingam–Whyatt simplification of a closed ring (first == last), removing points whose effective triangle area is below the tolerance
void simplify_ring_vw(std::vector<Kernel::Point_2> &ring, Kernel::FT tolerance) {
  if (ring.empty()) return;
  if (ring.front() != ring.back()) ring.push_back(ring.front());
  const std::size_t n = ring.size()-1;
  if (n < 4) return;
  
  // Effective area of the triangle (prev, i, next) in the ring
  auto effective_area = [&](std::size_t i) -> double {
    const std::size_t prev = (i+n-1) % n;
    const std::size_t next = (i+1) % n;
    const double ax = CGAL::to_double(ring[prev].x());
    const double ay = CGAL::to_double(ring[prev].y());
    const double bx = CGAL::to_double(ring[i].x());
    const double by = CGAL::to_double(ring[i].y());
    const double cx = CGAL::to_double(ring[next].x());
    const double cy = CGAL::to_double(ring[next].y());
    return 0.5*std::abs((bx-ax)*(cy-ay) - (by-ay)*(cx-ax));
  };
  
  std::vector<double> areas(n);
  std::priority_queue<std::pair<double, std::size_t>, std::vector<std::pair<double, std::size_t>>, std::greater<>> heap;
  for (std::size_t i = 0; i < n; ++i) {
    areas[i] = effective_area(i);
    heap.push({areas[i], i});
  }
  
  std::vector<bool> removed(n, false);
  std::size_t remaining = n;
  while (remaining > 3) {
    while (!heap.empty() && (removed[heap.top().second] || heap.top().first != areas[heap.top().second])) heap.pop();
    if (heap.empty()) break;
    const std::size_t i = heap.top().second;
    const double min_area = heap.top().first;
    heap.pop();
    if (min_area > tolerance) break;
    removed[i] = true;
    --remaining;
    
    // Recompute the effective areas of the neighbours of the removed point
    std::size_t prev = (i+n-1) % n;
    while (removed[prev]) prev = (prev+n-1) % n;
    std::size_t next = (i+1) % n;
    while (removed[next]) next = (next+1) % n;
    areas[prev] = effective_area(prev);
    areas[next] = effective_area(next);
    heap.push({areas[prev], prev});
    heap.push({areas[next], next});
  }
  
  if (remaining == n) return;
  std::vector<Kernel::Point_2> simplified;
  simplified.reserve(remaining+1);
  for (std::size_t i = 0; i < n; ++i) {
    if (!removed[i]) simplified.push_back(ring[i]);
  }
  if (simplified.size() >= 3) {
    simplified.push_back(simplified.front());
    ring = std::move(simplified);
  }
}

// Compute the object-height raster (DSM minus DTM) and mask forbidden areas to NODATA
void mask_building_areas(const Config &config, Map &map) {
  const float nodata = -9999.0f;
  
  GDALDataset *dsm_dataset = (GDALDataset *)GDALOpen(config.dsm_path.c_str(), GA_ReadOnly);
  if (!dsm_dataset) {
    std::cerr << "Error: Could not open DSM dataset: " << config.dsm_path << std::endl;
    return;
  } GDALDataset *dtm_dataset = (GDALDataset *)GDALOpen(config.dtm_path.c_str(), GA_ReadOnly);
  if (!dtm_dataset) {
    std::cerr << "Error: Could not open DTM dataset: " << config.dtm_path << std::endl;
    GDALClose(dsm_dataset);
    return;
  }
  
  const int width = dsm_dataset->GetRasterXSize();
  const int height = dsm_dataset->GetRasterYSize();
  double dsm_geotransform[6];
  if (dsm_dataset->GetGeoTransform(dsm_geotransform) != CE_None) {
    std::cerr << "Error: Could not get DSM geotransform." << std::endl;
    GDALClose(dtm_dataset);
    GDALClose(dsm_dataset);
    return;
  }
  
  GDALRasterBand *dsm_band = dsm_dataset->GetRasterBand(1);
  GDALRasterBand *dtm_band = dtm_dataset->GetRasterBand(1);
  const int dtm_width = dtm_dataset->GetRasterXSize();
  const int dtm_height = dtm_dataset->GetRasterYSize();
  double dtm_geotransform[6];
  if (dtm_dataset->GetGeoTransform(dtm_geotransform) != CE_None) {
    std::cerr << "Error: Could not get DTM geotransform." << std::endl;
    GDALClose(dtm_dataset);
    GDALClose(dsm_dataset);
    return;
  }
  
  float *dsm_data = new float[width*height];
  float *dtm_data = new float[dtm_width*dtm_height];
  float *heights = new float[width*height];
  unsigned char *mask = new unsigned char[width*height]();
  bool read_ok = true;
  
  if (dsm_band->RasterIO(GF_Read, 0, 0, width, height, dsm_data, width, height, GDT_Float32, 0, 0) != CE_None) {
    std::cerr << "Error reading DSM raster data." << std::endl;
    read_ok = false;
  } else if (dtm_band->RasterIO(GF_Read, 0, 0, dtm_width, dtm_height, dtm_data, dtm_width, dtm_height, GDT_Float32, 0, 0) != CE_None) {
    std::cerr << "Error reading DTM raster data." << std::endl;
    read_ok = false;
  }
  
  if (!read_ok) {
    delete[] dsm_data; delete[] dtm_data; delete[] heights; delete[] mask;
    GDALClose(dtm_dataset); GDALClose(dsm_dataset);
    return;
  }
  
  int dsm_has_nodata = 0;
  double dsm_nodata = dsm_band->GetNoDataValue(&dsm_has_nodata);
  int dtm_has_nodata = 0;
  double dtm_nodata = dtm_band->GetNoDataValue(&dtm_has_nodata);
  
  // DSM and DTM from the same INEGI tile share a grid
  const bool aligned = (dtm_width == width && dtm_height == height);
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const int index = row*width + col;
      const float dsm_value = dsm_data[index];
      bool no_data = is_nodata_value(dsm_value, dsm_has_nodata, dsm_nodata) || std::isnan(dsm_value);
      float dtm_value = 0.0f;
      if (!no_data) {
        if (aligned) {
          dtm_value = dtm_data[index];
        } else {
          const double x = dsm_geotransform[0] + (col+0.5)*dsm_geotransform[1] + (row+0.5)*dsm_geotransform[2];
          const double y = dsm_geotransform[3] + (col+0.5)*dsm_geotransform[4] + (row+0.5)*dsm_geotransform[5];
          const double denominator = dtm_geotransform[1]*dtm_geotransform[5] - dtm_geotransform[2]*dtm_geotransform[4];
          if (std::abs(denominator) < 1e-12) {
            no_data = true;
          } else {
            const int dtm_col = (int)std::floor((dtm_geotransform[5]*(x-dtm_geotransform[0]) - dtm_geotransform[2]*(y-dtm_geotransform[3])) / denominator);
            const int dtm_row = (int)std::floor((-dtm_geotransform[4]*(x-dtm_geotransform[0]) + dtm_geotransform[1]*(y-dtm_geotransform[3])) / denominator);
            if (dtm_col < 0 || dtm_col >= dtm_width || dtm_row < 0 || dtm_row >= dtm_height) no_data = true;
            else dtm_value = dtm_data[dtm_row*dtm_width + dtm_col];
          }
        }
        if (is_nodata_value(dtm_value, dtm_has_nodata, dtm_nodata)) no_data = true;
        if (std::isnan(dtm_value)) no_data = true;
      }
      heights[index] = no_data ? nodata : dsm_value - dtm_value;
    }
  }
  
  // Rasterize forbidden areas (Road, WaterBody, PlantCover) into a mask
  std::vector<OGRGeometryH> geometries;
  for (auto const &polygon: map.polygons) {
    if (polygon.semantic_class != "Road" && polygon.semantic_class != "Railway" && polygon.semantic_class != "WaterBody" && polygon.semantic_class != "PlantCover") continue;
    OGRPolygon *ogr_polygon = new OGRPolygon();
    OGRLinearRing *ogr_outer = new OGRLinearRing();
    for (auto const &point: polygon.outer_ring.points) ogr_outer->addPoint(CGAL::to_double(point.x()), CGAL::to_double(point.y()));
    if (!polygon.outer_ring.points.empty() && polygon.outer_ring.points.back() != polygon.outer_ring.points.front()) {
      ogr_outer->addPoint(CGAL::to_double(polygon.outer_ring.points.front().x()), CGAL::to_double(polygon.outer_ring.points.front().y()));
    } ogr_polygon->addRingDirectly(ogr_outer);
    for (auto const &ring: polygon.inner_rings) {
      OGRLinearRing *ogr_hole = new OGRLinearRing();
      for (auto const &point: ring.points) ogr_hole->addPoint(CGAL::to_double(point.x()), CGAL::to_double(point.y()));
      if (!ring.points.empty() && ring.points.back() != ring.points.front()) ogr_hole->addPoint(CGAL::to_double(ring.points.front().x()), CGAL::to_double(ring.points.front().y()));
      ogr_polygon->addRingDirectly(ogr_hole);
    } geometries.push_back((OGRGeometryH)ogr_polygon);
  }
  std::cout << "Masking " << geometries.size() << " forbidden polygons." << std::endl;
  
  GDALDriver *mem_driver = GetGDALDriverManager()->GetDriverByName("MEM");
  GDALDataset *mask_dataset = mem_driver->Create("", width, height, 1, GDT_Byte, NULL);
  mask_dataset->SetGeoTransform(dsm_geotransform);
  mask_dataset->SetProjection(dsm_dataset->GetProjectionRef());
  GDALRasterBand *mask_band = mask_dataset->GetRasterBand(1);
  double burn_values[] = {1.0};
  int band_list[] = {1};
  char **rasterize_options = NULL;
  rasterize_options = CSLAddString(rasterize_options, "ALL_TOUCHED=TRUE");
  for (auto const &geometry: geometries) {
    if (GDALRasterizeGeometries((GDALDatasetH)mask_dataset, 1, band_list, 1, &geometry, NULL, NULL, burn_values, rasterize_options, NULL, NULL) != CE_None) {
      std::cerr << "Error: Could not rasterize forbidden area." << std::endl;
    }
  } CSLDestroy(rasterize_options);
  mask_band->RasterIO(GF_Read, 0, 0, width, height, mask, width, height, GDT_Byte, 0, 0);
  GDALClose(mask_dataset);
  
  // Apply the mask and write the output
  for (int index = 0; index < width*height; ++index) {
    if (mask[index] != 0) heights[index] = nodata;
  }
  GDALDriver *gtiff_driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  GDALDataset *output_dataset = gtiff_driver->Create(config.mask_output_path.c_str(), width, height, 1, GDT_Float32, NULL);
  if (!output_dataset) {
    std::cerr << "Error: Could not create " << config.mask_output_path << std::endl;
  } else {
    output_dataset->SetGeoTransform(dsm_geotransform);
    output_dataset->SetProjection(dsm_dataset->GetProjectionRef());
    GDALRasterBand *output_band = output_dataset->GetRasterBand(1);
    output_band->SetNoDataValue(nodata);
    if (output_band->RasterIO(GF_Write, 0, 0, width, height, heights, width, height, GDT_Float32, 0, 0) != CE_None) {
      std::cerr << "Error: Could not write mask raster." << std::endl;
    } GDALClose(output_dataset);
    std::cout << "Wrote masked height raster to " << config.mask_output_path << std::endl;
  }
  
  delete[] dsm_data; delete[] dtm_data; delete[] heights; delete[] mask;
  for (auto const &geometry: geometries) OGR_G_DestroyGeometry(geometry);
  GDALClose(dtm_dataset);
  GDALClose(dsm_dataset);
}

// Convert the labelled building raster to vector footprints (replaces the manual QGIS polygonisation step)
void polygonize_buildings(const Config &config, GDALRasterBand *labels_band, int width, int height) {
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
  if (driver == NULL) {
    std::cerr << "Error: GPKG driver not available." << std::endl;
    return;
  }
  
  GDALDataset *output = driver->Create(config.buildings_output_path.c_str(), 0, 0, 0, GDT_Unknown, NULL);
  if (output == NULL) {
    std::cerr << "Error: Could not create " << config.buildings_output_path << std::endl;
    return;
  }
  
  OGRSpatialReference output_srs;
  if (output_srs.SetFromUserInput(labels_band->GetDataset()->GetProjectionRef()) != OGRERR_NONE) {
    std::cerr << "Warning: Could not parse raster SRS, writing without a spatial reference." << std::endl;
  }
  OGRLayer *layer = output->CreateLayer("Building", &output_srs, wkbPolygon, NULL);
  if (layer == NULL) {
    std::cerr << "Error: Could not create Building layer." << std::endl;
    GDALClose(output);
    return;
  }
  OGRFieldDefn building_field("building", OFTInteger);
  building_field.SetWidth(10);
  layer->CreateField(&building_field);
  
  // Build a mask band so the background (label 0) is not polygonized
  GDALDriver *mem_driver = GetGDALDriverManager()->GetDriverByName("MEM");
  GDALDataset *mask_dataset = mem_driver->Create("mask", width, height, 1, GDT_Byte, NULL);
  GDALRasterBand *mask_band = mask_dataset->GetRasterBand(1);
  unsigned char *mask = new unsigned char[width*height];
  unsigned int *labels = new unsigned int[width*height];
  if (labels_band->RasterIO(GF_Read, 0, 0, width, height, labels, width, height, GDT_UInt32, 0, 0) != CE_None) {
    std::cerr << "Error: Could not read building labels for polygonisation." << std::endl;
    delete[] mask; delete[] labels;
    GDALClose(mask_dataset);
    GDALClose(output);
    return;
  }
  for (int i = 0; i < width*height; ++i) mask[i] = (labels[i] != 0) ? 1 : 0;
  if (mask_band->RasterIO(GF_Write, 0, 0, width, height, mask, width, height, GDT_Byte, 0, 0) != CE_None) {
    std::cerr << "Error: Could not write mask band." << std::endl;
  }
  delete[] labels; delete[] mask;
  
  char **options = NULL;
  options = CSLSetNameValue(options, "8CONNECTED", "FALSE");
  if (GDALPolygonize(labels_band, mask_band, layer, 0, options, NULL, NULL) != CE_None) {
    std::cerr << "Error: GDALPolygonize failed." << std::endl;
  } else {
    std::cout << "Polygonised building labels into " << layer->GetFeatureCount() << " footprints, wrote to " << config.buildings_output_path << std::endl;
  }
  CSLDestroy(options);
  GDALClose(mask_dataset);
  GDALClose(output);
}

// Extract building footprints by region growing on the masked object-height raster
void grow_building_footprints(const Config &config) {
  const std::string mask_path = config.building_mask_path.empty() ? config.mask_output_path : config.building_mask_path;
  if (mask_path.empty()) {
    std::cerr << "Error: Region growing requires a masked heights raster (--mask_output or --building_mask)." << std::endl;
    return;
  }
  
  GDALDataset *dataset = (GDALDataset *)GDALOpen(mask_path.c_str(), GA_ReadOnly);
  if (!dataset) {
    std::cerr << "Error: Could not open masked raster: " << mask_path << std::endl;
    return;
  }
  
  const int width = dataset->GetRasterXSize();
  const int height = dataset->GetRasterYSize();
  double geotransform[6];
  if (dataset->GetGeoTransform(geotransform) != CE_None) {
    std::cerr << "Error: Could not get geotransform of " << mask_path << std::endl;
    GDALClose(dataset);
    return;
  }
  
  GDALRasterBand *band = dataset->GetRasterBand(1);
  int has_nodata = 0;
  double nodata = band->GetNoDataValue(&has_nodata);
  
  float *heights = new float[width*height];
  if (band->RasterIO(GF_Read, 0, 0, width, height, heights, width, height, GDT_Float32, 0, 0) != CE_None) {
    std::cerr << "Error reading masked raster data." << std::endl;
    delete[] heights;
    GDALClose(dataset);
    return;
  }
  
  unsigned int *building_id = new unsigned int[width*height]();
  unsigned int *region_stamp = new unsigned int[width*height]();
  std::deque<int> queue;
  std::vector<int> region_pixels;
  unsigned int current_stamp = 0, n_buildings = 0;
  
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const int index = row*width + col;
      if (building_id[index] != 0) continue;
      const float seed_height = heights[index];
      if (is_nodata_value(seed_height, has_nodata, nodata) || std::isnan(seed_height)) continue;
      if (seed_height <= config.seed_threshold) continue;
      
      const float tolerance = (seed_height >= config.tall_building_height) ? config.tall_tolerance : config.normal_tolerance;
      
      ++current_stamp;
      queue.clear();
      queue.push_back(index);
      region_stamp[index] = current_stamp;
      region_pixels.clear();
      region_pixels.push_back(index);
      while (!queue.empty()) {
        const int current_index = queue.front();
        queue.pop_front();
        const int current_row = current_index / width;
        const int current_col = current_index % width;
        const double current_height = heights[current_index];
        const int neighbour_offsets[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (int neighbour = 0; neighbour < 4; ++neighbour) {
          const int new_row = current_row + neighbour_offsets[neighbour][0];
          const int new_col = current_col + neighbour_offsets[neighbour][1];
          if (new_row < 0 || new_row >= height || new_col < 0 || new_col >= width) continue;
          const int new_index = new_row*width + new_col;
          if (region_stamp[new_index] == current_stamp) continue;
          const double new_height = heights[new_index];
          if (is_nodata_value(new_height, has_nodata, nodata) || std::isnan(new_height)) continue;
          if (std::abs(new_height-current_height) <= tolerance) {
            region_stamp[new_index] = current_stamp;
            queue.push_back(new_index);
            region_pixels.push_back(new_index);
          }
        }
      }
      
      // Keep footprints with at least minimum_region_area pixels
      if ((int)region_pixels.size()-1 >= config.minimum_region_area) {
        ++n_buildings;
        for (auto const &pixel: region_pixels) building_id[pixel] = n_buildings;
      }
    }
  }
  
  std::cout << "Region growing found " << n_buildings << " building footprints." << std::endl;
  
  GDALDriver *gtiff_driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  GDALDataset *output_dataset = gtiff_driver->Create(config.grow_output_path.c_str(), width, height, 1, GDT_UInt32, NULL);
  if (!output_dataset) {
    std::cerr << "Error: Could not create " << config.grow_output_path << std::endl;
  } else {
    output_dataset->SetGeoTransform(geotransform);
    output_dataset->SetProjection(dataset->GetProjectionRef());
    GDALRasterBand *output_band = output_dataset->GetRasterBand(1);
    output_band->SetNoDataValue(0);
    if (output_band->RasterIO(GF_Write, 0, 0, width, height, building_id, width, height, GDT_UInt32, 0, 0) != CE_None) {
      std::cerr << "Error: Could not write building labels." << std::endl;
    } else {
      output_band->FlushCache();
      std::cout << "Wrote building labels to " << config.grow_output_path << std::endl;
      if (!config.buildings_output_path.empty()) {
        polygonize_buildings(config, output_band, width, height);
      }
    } GDALClose(output_dataset);
  }
  
  delete[] heights; delete[] building_id; delete[] region_stamp;
  GDALClose(dataset);
}

int main(int argc, const char * argv[]) {
  
  Config config;
  std::map<std::string, std::string> command_line_overrides;
  
  // Parse command line arguments (format: --key value, or --config <file>)
  std::string config_file_path;
  for (int arg = 1; arg < argc; ++arg) {
    std::string key = argv[arg];
    if (key == "--config") {
      if (arg+1 >= argc) {
        std::cerr << "Error: Missing value for --config" << std::endl;
        return EXIT_FAILURE;
      } config_file_path = argv[++arg];
    } else if (key.rfind("--", 0) == 0) {
      std::string option_key = key.substr(2);
      if (arg+1 >= argc) {
        std::cerr << "Error: Missing value for " << key << std::endl;
        return EXIT_FAILURE;
      } command_line_overrides[option_key] = argv[++arg];
    } else {
      std::cerr << "Error: Unknown argument: " << key << std::endl;
      return EXIT_FAILURE;
    }
  }
  
  // Load config file (overrides defaults)
  if (!config_file_path.empty()) {
    std::ifstream config_file(config_file_path);
    if (!config_file.is_open()) {
      std::cerr << "Error: Could not open config file: " << config_file_path << std::endl;
      return EXIT_FAILURE;
    } nlohmann::json config_json;
    config_file >> config_json;
    for (auto &entry: config_json.items()) {
      if (entry.value().is_string()) {
        config.set(entry.key(), entry.value().get<std::string>());
      } else if (entry.value().is_number()) {
        config.set(entry.key(), std::to_string(entry.value().get<double>()));
      } else if (entry.value().is_boolean()) {
        config.set(entry.key(), entry.value().get<bool>() ? "true" : "false");
      } else {
        std::cerr << "Unsupported config value type for " << entry.key() << std::endl;
      }
    }
  }
  
  // Command line arguments override the config file
  for (auto const &option: command_line_overrides) config.set(option.first, option.second);
  
  // Check required inputs
  if (config.dsm_path.empty() || config.dtm_path.empty()) {
    std::cerr << "Error: DSM and DTM paths are required (--dsm and --dtm)." << std::endl;
    return EXIT_FAILURE;
  }
  
  // Check required outputs
  if (config.terrain_obj_path.empty() || config.obj_path.empty() || config.cityjson_path.empty()) {
    std::cerr << "Error: Output paths are required (--terrain_obj, --obj and --cityjson)." << std::endl;
    return EXIT_FAILURE;
  }
  
  std::cout << "3DCM Mexico configuration:" << std::endl;
  config.print();
  
  std::map<std::string, std::string> vector_paths, raster_paths;
  raster_paths["dsm"] = config.dsm_path;
  raster_paths["dtm"] = config.dtm_path;
  if (!config.generate_waterbodies) vector_paths["WaterBody"] = config.waterbody_path;
  else if (!config.waterbody_path.empty()) {
    std::cout << "Water bodies will be generated in-tool; ignoring --waterbody (" << config.waterbody_path << ")" << std::endl;
  }
  if (!config.generate_plantcover) vector_paths["PlantCover"] = config.plantcover_path;
  else if (!config.plantcover_path.empty()) {
    std::cout << "Plant cover will be generated in-tool; ignoring --plantcover (" << config.plantcover_path << ")" << std::endl;
  }
  if (!config.generate_roads) vector_paths["Road"] = config.road_path;
  if (!config.generate_terrain) vector_paths["Terrain"] = config.terrain_path;
  else if (!config.terrain_path.empty()) {
    std::cout << "Terrain will be generated in-tool; ignoring --terrain (" << config.terrain_path << ")" << std::endl;
  }
  
  // When building footprints are grown in-tool, the generated footprints replace the --building input
  const bool generate_buildings_in_tool = !config.grow_output_path.empty() && !config.buildings_output_path.empty();
  if (!generate_buildings_in_tool) {
    vector_paths["Building"] = config.building_path;
  } else if (!config.building_path.empty()) {
    std::cout << "Building footprints will be generated in-tool; ignoring --building (" << config.building_path << ")" << std::endl;
  }
  
  Map map;
  Point_cloud dsm, dtm_points;
  Point_index dsm_index, dtm_points_index;
  Triangulation dtm;
  
  GDALAllRegister();
  
  Kernel::FT dsm_x_min = 0.0, dsm_y_min = 0.0, dsm_x_max = 0.0, dsm_y_max = 0.0;
  
  for (auto const &path: raster_paths) {
    GDALDataset *dataset = (GDALDataset *)GDALOpen(path.second.c_str(), GA_ReadOnly);
    if (!dataset) {
      std::cerr << "Error: Could not open " << path.first << " dataset: " << path.second << std::endl;
      continue;
    } std::cout << "Opening " << path.first << " type: " << dataset->GetDriverName() << std::endl;
    
    GDALRasterBand *band = dataset->GetRasterBand(1);
    if (!band) {
      GDALClose(dataset);
      std::cerr << "Could not get raster band" << std::endl;
      continue;
    }
    
    int width = dataset->GetRasterXSize();
    int height = dataset->GetRasterYSize();
    
    double geotransform[6];
    if (dataset->GetGeoTransform(geotransform) != CE_None) {
      GDALClose(dataset);
      std::cerr << "Could not get geotransform" << std::endl;
      continue;
    }
    
    // Save the DSM extent to use as a default study area
    if (path.first == "dsm") {
      dsm_x_min = geotransform[0];
      dsm_y_max = geotransform[3];
      dsm_x_max = geotransform[0] + width*geotransform[1] + height*geotransform[2];
      dsm_y_min = geotransform[3] + width*geotransform[4] + height*geotransform[5];
    }
    
    int hasNoData = 0;
    double noDataValue = band->GetNoDataValue(&hasNoData);
    
    float *rasterData = new float[width*height];
    CPLErr err = band->RasterIO(GF_Read, 0, 0, width, height, rasterData, width, height, GDT_Float32, 0, 0);
    
    if (err != CE_None) {
      delete[] rasterData;
      GDALClose(dataset);
      std::cerr << "Error reading raster data" << std::endl;
      continue;
    }
    
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < width; col++) {
        int index = row * width + col;
        float value = rasterData[index];
        
        // Skip no data values (using the same tolerant check as the building mask)
        if (is_nodata_value(value, hasNoData, noDataValue)) continue;
        if (std::isnan(value)) continue;
        
        // Calculate coordinates
        double x = geotransform[0] + col * geotransform[1] + row * geotransform[2];
        double y = geotransform[3] + col * geotransform[4] + row * geotransform[5];
        
        if (path.first == "dtm") {
          dtm_points.insert(Kernel::Point_3(x, y, value));
        } else if (path.first == "dsm") {
          dsm.insert(Kernel::Point_3(x, y, value));
        }
      }
    }
    
    delete[] rasterData;
    GDALClose(dataset);
  }
  
  index_point_cloud(dtm_points, dtm_points_index, config);
  index_point_cloud(dsm, dsm_index, config);
  
  const Kernel::FT dtm_cell_size = config.dtm_cell_size;
  const Kernel::FT dtm_search_radius = config.dtm_search_radius;
  const Kernel::FT dtm_ratio_to_use = config.dtm_ratio_to_use;
  Kernel::FT squared_search_radius = dtm_search_radius*dtm_search_radius;
  if (!dtm_points.empty()) {
    for (Kernel::FT x = dtm_points_index.x_min-0.5*dtm_search_radius; x < dtm_points_index.x_max+0.5*dtm_search_radius; x += dtm_cell_size) {
      for (Kernel::FT y = dtm_points_index.y_min-0.5*dtm_search_radius; y < dtm_points_index.y_max+0.5*dtm_search_radius; y += dtm_cell_size) {
        std::vector<Point_index *> intersected_nodes;
        dtm_points_index.find_intersections(intersected_nodes, x-0.5*dtm_search_radius, x+0.5*dtm_search_radius, y-0.5*dtm_search_radius, y+0.5*dtm_search_radius);
        std::vector<Kernel::FT> elevations;
        for (auto const &node: intersected_nodes) {
          for (auto const &point_index: node->points) {
            Kernel::FT squared_2d_distance = (dtm_points.point(point_index).x()-x)*(dtm_points.point(point_index).x()-x) +
                                             (dtm_points.point(point_index).y()-y)*(dtm_points.point(point_index).y()-y);
            if (squared_2d_distance < squared_search_radius) {
              elevations.push_back(dtm_points.point(point_index).z());
            }
          }
        } std::sort(elevations.begin(), elevations.end());
        Triangulation::Vertex_handle vertex = dtm.insert(Kernel::Point_2(x, y));
        if (elevations.empty()) {
          vertex->info().z = 0.0;
          continue;
        } vertex->info().z = elevations[std::floor(dtm_ratio_to_use*elevations.size())];
      }
    }
  }
  
  // Generate road polygons from city blocks if requested
  if (config.generate_roads) {
    if (!config.study_area_set) {
      config.study_x_min = dsm_x_min;
      config.study_y_min = dsm_y_min;
      config.study_x_max = dsm_x_max;
      config.study_y_max = dsm_y_max;
      config.study_area_set = true;
      std::cout << "Using DSM extent as study area: " << config.study_x_min << ", " << config.study_y_min << ", " << config.study_x_max << ", " << config.study_y_max << std::endl;
    } generate_road_polygons(config, map);
  }
  
  // Generate plant cover polygons from INEGI public areas if requested
  if (config.generate_plantcover) {
    if (!config.study_area_set) {
      config.study_x_min = dsm_x_min;
      config.study_y_min = dsm_y_min;
      config.study_x_max = dsm_x_max;
      config.study_y_max = dsm_y_max;
      config.study_area_set = true;
      std::cout << "Using DSM extent as study area: " << config.study_x_min << ", " << config.study_y_min << ", " << config.study_x_max << ", " << config.study_y_max << std::endl;
    } generate_plantcover_polygons(config, map);
  }
  
  // Generate water body polygons from INEGI water area layers if requested
  if (config.generate_waterbodies) {
    if (!config.study_area_set) {
      config.study_x_min = dsm_x_min;
      config.study_y_min = dsm_y_min;
      config.study_x_max = dsm_x_max;
      config.study_y_max = dsm_y_max;
      config.study_area_set = true;
      std::cout << "Using DSM extent as study area: " << config.study_x_min << ", " << config.study_y_min << ", " << config.study_x_max << ", " << config.study_y_max << std::endl;
    } generate_waterbodies_polygons(config, map);
  }
  
  // Generate terrain polygons from city blocks if requested
  if (config.generate_terrain) {
    if (!config.study_area_set) {
      config.study_x_min = dsm_x_min;
      config.study_y_min = dsm_y_min;
      config.study_x_max = dsm_x_max;
      config.study_y_max = dsm_y_max;
      config.study_area_set = true;
      std::cout << "Using DSM extent as study area: " << config.study_x_min << ", " << config.study_y_min << ", " << config.study_x_max << ", " << config.study_y_max << std::endl;
    } generate_terrain_polygons(config, map);
  }
  
  for (auto const &path: vector_paths) {
    GDALDataset *dataset = (GDALDataset*) GDALOpenEx(path.second.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
    if (dataset == NULL) {
      std::cerr << "Error: Could not open " << path.first << " dataset: " << path.second << std::endl;
      continue;
    } std::cout << "Opening " << path.first << " type: " << dataset->GetDriverName() << std::endl;
    std::size_t n_polygons = read_polygon_layer(dataset, path.first, map, config);
    std::cout << "Loaded " << n_polygons << " " << path.first << " polygons." << std::endl;
    GDALClose(dataset);
  }

  // Resolve overlaps between PlantCover, WaterBody and Terrain polygons so that
  // the higher-priority class keeps the shared area (WaterBody > PlantCover > Terrain)
  resolve_area_overlaps(map);

  // Mask forbidden areas in the DSM-DTM object-height raster
  if (!config.mask_output_path.empty()) mask_building_areas(config, map);

  // Extract building footprints by region growing on the masked heights
  if (!config.grow_output_path.empty()) grow_building_footprints(config);

  // Load the generated building footprints into the model (single-invocation pipeline)
  if (generate_buildings_in_tool) {
    GDALDataset *dataset = (GDALDataset*) GDALOpenEx(config.buildings_output_path.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
    if (dataset == NULL) {
      std::cerr << "Error: Could not open generated building footprints: " << config.buildings_output_path << std::endl;
    } else {
      std::size_t n_generated_buildings = read_polygon_layer(dataset, "Building", map, config);
      std::cout << "Loaded " << n_generated_buildings << " generated building footprints into the model." << std::endl;
      GDALClose(dataset);
    }
  }

  // Basic polygon repair (pre-requisite for triangulation)
  std::vector<Polygon>::iterator current_polygon = map.polygons.begin();
  while (current_polygon != map.polygons.end()) {
    // Close polygons and rings
    if (current_polygon->outer_ring.points.back() != current_polygon->outer_ring.points.front()) {
      std::cout << "Warning: Last point != first. Adding it again at the end..." << std::endl;
      current_polygon->outer_ring.points.push_back(current_polygon->outer_ring.points.front());
    } for (auto &ring: current_polygon->inner_rings) {
      if (ring.points.back() != ring.points.front()) {
        std::cout << "Warning: Last point != first. Adding it again at the end..." << std::endl;
        ring.points.push_back(ring.points.front());
      }
    }
    
    // Simplify building footprint rings (Visvalingam–Whyatt)
    if (current_polygon->semantic_class == "Building" && config.simplify_tolerance > 0.0) {
      simplify_ring_vw(current_polygon->outer_ring.points, config.simplify_tolerance);
      for (auto &ring: current_polygon->inner_rings) simplify_ring_vw(ring.points, config.simplify_tolerance);
    }
    
    // Delete degenerate polygons and rings
    if (current_polygon->outer_ring.points.size() < 4) {
      std::cout << "Deleting polygon with < 3 vertices..." << std::endl;
      current_polygon = map.polygons.erase(current_polygon);
      continue;
    } auto current_ring = current_polygon->inner_rings.begin();
    while (current_ring != current_polygon->inner_rings.end()) {
      if (current_ring->points.size() < 4) {
        std::cout << "Deleting ring with < 3 vertices..." << std::endl;
        current_ring = current_polygon->inner_rings.erase(current_ring);
        continue;
      } ++current_ring;
    }
    
    ++current_polygon;
  }
  
  // Triangulate polygons
  current_polygon = map.polygons.begin();
  while (current_polygon != map.polygons.end()) {
    triangulate_polygon(*current_polygon);
    if (current_polygon->triangulation.number_of_faces() == 0) {
      std::cout << "Deleting degenerate polygon (no triangles after insertion of constraints)..." << std::endl;
      current_polygon = map.polygons.erase(current_polygon);
      continue;
    }
    
    // Check if the result isn't degenerate
    std::size_t interior_triangles = 0;
    for (auto const &current_face: current_polygon->triangulation.finite_face_handles()) {
      if (current_face->info().interior) ++interior_triangles;
    } if (interior_triangles == 0) {
      std::cout << "Deleting degenerate polygon (no interior triangles)..." << std::endl;
      current_polygon = map.polygons.erase(current_polygon);
      continue;
    }
    
    ++current_polygon;
  }
  
  // Compute bounds of repaired polygons
  for (auto &polygon: map.polygons) {
    bool first_boundary_point = true;
    for (auto current_vertex: polygon.triangulation.finite_vertex_handles()) {
      bool incident_to_interior = false;
      bool incident_to_exterior = false;
      Triangulation::Face_circulator first_face = polygon.triangulation.incident_faces(current_vertex);
      Triangulation::Face_circulator current_face = first_face;
      do {
        if (current_face->info().interior) incident_to_interior = true;
        else incident_to_exterior = true;
        ++current_face;
      } while (current_face != first_face);
      if (incident_to_interior && incident_to_exterior) {
        if (first_boundary_point) {
          polygon.x_min = current_vertex->point().x();
          polygon.x_max = current_vertex->point().x();
          polygon.y_min = current_vertex->point().y();
          polygon.y_max = current_vertex->point().y();
          first_boundary_point = false;
        } else {
          if (current_vertex->point().x() < polygon.x_min) polygon.x_min = current_vertex->point().x();
          if (current_vertex->point().x() > polygon.x_max) polygon.x_max = current_vertex->point().x();
          if (current_vertex->point().y() < polygon.y_min) polygon.y_min = current_vertex->point().y();
          if (current_vertex->point().y() > polygon.y_max) polygon.y_max = current_vertex->point().y();
        }
      }
    }
  }
  
  lift_flat_polygons("Building", map, dsm, dsm_index, config.building_height_percentile);
  lift_polygon_vertices("Road", map, dsm, dsm_index, dtm);
  lift_polygon_vertices("Railway", map, dsm, dsm_index, dtm);
  lift_polygons("PlantCover", map, dsm, dsm_index, dtm);
  lift_polygon_vertices("WaterBody", map, dsm, dsm_index, dtm);
  lift_polygon_vertices("Terrain", map, dsm, dsm_index, dtm);
  create_vertical_walls(map, dtm);
  
  write_terrain_obj(config.terrain_obj_path.c_str(), dtm);
  write_3dcm_obj(config.obj_path.c_str(), map, config);
  write_3dcm_cityjson(config.cityjson_path.c_str(), map, config);
  return EXIT_SUCCESS;
}
