#include "baldr/graphid.h"
#include "baldr/rapidjson_utils.h"
#include "gurka.h"
#include "mjolnir/util.h"

#include <boost/property_tree/ptree.hpp>
#include <gtest/gtest.h>
#include <zlib.h>

#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#if !defined(VALHALLA_SOURCE_DIR)
#define VALHALLA_SOURCE_DIR
#endif

#if !defined(VALHALLA_BINARY_DIR)
#define VALHALLA_BINARY_DIR
#endif

namespace {
using boost::property_tree::ptree;
using valhalla::baldr::GraphId;
using valhalla::mjolnir::build_tile_set;
using valhalla::mjolnir::TileManifest;
using namespace valhalla;
using namespace valhalla::midgard;

// Verify that this function runs
TEST(UtilMjolnir, BuildTileSet) {
  ptree config;
  const std::string tile_dir("test/data/util_mjolnir_test_tiles");
  config.put("mjolnir.tile_dir", tile_dir);
  config.put("mjolnir.concurrency", 1);
  EXPECT_TRUE(build_tile_set(config, {VALHALLA_SOURCE_DIR "test/data/harrisburg.osm.pbf"},
                             mjolnir::BuildStage::kInitialize, mjolnir::BuildStage::kCleanup));
  // Clear the tile directory so it doesn't interfere with the next test.
  std::filesystem::remove_all(tile_dir);
  EXPECT_TRUE(!std::filesystem::exists(tile_dir));
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

gurka::map build_test_tiles(const std::string& tile_dir,
                            bool compressed = false,
                            mjolnir::BuildStage end_stage = mjolnir::BuildStage::kCleanup) {
  const auto layout = gurka::detail::map_to_coordinates("A---B---C---D", 500, {5.12, 52.09});
  const gurka::ways ways = {{"AB", {{"highway", "primary"}}},
                            {"BC", {{"highway", "residential"}}},
                            {"CD", {{"highway", "tertiary"}}}};
  return gurka::buildtiles(layout, ways, {}, {}, tile_dir,
                           {{"mjolnir.concurrency", "1"},
                            {"mjolnir.hierarchy", "false"},
                            {"mjolnir.shortcuts", "false"},
                            {"mjolnir.compress_tiles", compressed ? "true" : "false"}},
                           mjolnir::BuildStage::kInitialize, end_stage);
}

TEST(UtilMjolnir, BuildMetadata) {
  const std::string tile_dir = "test/data/util_mjolnir_metadata";
  const auto before = std::time(nullptr);
  const auto map = build_test_tiles(tile_dir);
  const auto after = std::time(nullptr);
  const auto metadata = read_file(std::filesystem::path(tile_dir) / "build_info.json");
  rapidjson::Document document;
  document.Parse(metadata.c_str());
  EXPECT_FALSE(document.HasParseError());
  const auto timestamp = rapidjson::get_optional<int64_t>(document, "/build_timestamp");
  EXPECT_TRUE(timestamp);
  if (timestamp) {
    EXPECT_TRUE(document["build_timestamp"].IsInt64());
    EXPECT_GE(*timestamp, before);
    EXPECT_LE(*timestamp, after);
  }
  EXPECT_TRUE(
      build_tile_set(map.config, {}, mjolnir::BuildStage::kCleanup, mjolnir::BuildStage::kCleanup));
  EXPECT_EQ(read_file(std::filesystem::path(tile_dir) / "build_info.json"), metadata);
}

TEST(UtilMjolnir, CompressedTiles) {
  const std::filesystem::path raw_dir = "test/data/util_mjolnir_raw";
  const std::filesystem::path compressed_dir = "test/data/util_mjolnir_compressed";
  const auto raw = build_test_tiles(raw_dir.string());
  const auto compressed = build_test_tiles(compressed_dir.string(), true);
  baldr::GraphReader reader(compressed.config.get_child("mjolnir"));
  EXPECT_GT(reader.GetTileSet().size(), 1);
  for (const auto& id : reader.GetTileSet()) {
    EXPECT_EQ(id.level(), 2);
    const auto suffix = baldr::GraphTile::FileSuffix(id);
    EXPECT_FALSE(std::filesystem::exists(compressed_dir / suffix));
    const auto gzip_path = compressed_dir / (suffix + ".gz");
    EXPECT_TRUE(std::filesystem::is_regular_file(gzip_path));
    std::unique_ptr<gzFile_s, decltype(&gzclose)> file(gzopen(gzip_path.string().c_str(), "rb"),
                                                       gzclose);
    if (!file)
      throw std::runtime_error("Cannot read " + gzip_path.string());
    std::string bytes;
    std::array<char, 4096> buffer;
    int count;
    while ((count = gzread(file.get(), buffer.data(), buffer.size())) > 0)
      bytes.append(buffer.data(), count);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(bytes, read_file(raw_dir / suffix));
    EXPECT_TRUE(reader.DoesTileExist(id));
    EXPECT_TRUE(reader.GetGraphTile(id));
  }
  EXPECT_TRUE(std::filesystem::is_regular_file(compressed_dir / "build_info.json"));
  for (const auto action : {Options::locate, Options::trace_attributes}) {
    std::string raw_response, compressed_response;
    gurka::do_action(action, raw, {"A", "B", "C", "D"}, "auto", {}, {}, &raw_response);
    gurka::do_action(action, compressed, {"A", "B", "C", "D"}, "auto", {}, {}, &compressed_response);
    EXPECT_EQ(raw_response, compressed_response);
  }
}

TEST(UtilMjolnir, PartialBuildFinalization) {
  const std::filesystem::path tile_dir = "test/data/util_mjolnir_partial";
  const auto map = build_test_tiles(tile_dir.string(), true, mjolnir::BuildStage::kBuild);
  EXPECT_FALSE(std::filesystem::exists(tile_dir / "build_info.json"));
  std::ofstream(tile_dir / "build_info.json") << "{\"build_timestamp\":1}";
  EXPECT_TRUE(
      build_tile_set(map.config, {}, mjolnir::BuildStage::kEnhance, mjolnir::BuildStage::kEnhance));
  EXPECT_FALSE(std::filesystem::exists(tile_dir / "build_info.json"));
  for (const auto& file : std::filesystem::recursive_directory_iterator(tile_dir))
    EXPECT_NE(file.path().extension(), ".gz");
  EXPECT_TRUE(
      build_tile_set(map.config, {}, mjolnir::BuildStage::kFilter, mjolnir::BuildStage::kCleanup));
  EXPECT_TRUE(std::filesystem::is_regular_file(tile_dir / "build_info.json"));
  baldr::GraphReader reader(map.config.get_child("mjolnir"));
  EXPECT_FALSE(reader.GetTileSet().empty());
  for (const auto& id : reader.GetTileSet()) {
    const auto path = tile_dir / baldr::GraphTile::FileSuffix(id);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::is_regular_file(path.string() + ".gz"));
  }
}

TEST(UtilMjolnir, FailedCompressionPreservesTiles) {
  const std::filesystem::path tile_dir = "test/data/util_mjolnir_failed_compression";
  const auto map = build_test_tiles(tile_dir.string(), true, mjolnir::BuildStage::kBuild);
  baldr::GraphReader reader(map.config.get_child("mjolnir"));
  const auto tiles = reader.GetTileSet();
  if (tiles.empty())
    throw std::runtime_error("No tiles to compress");
  const auto blocked = tile_dir / (baldr::GraphTile::FileSuffix(*tiles.begin()) + ".gz");
  std::filesystem::create_directory(blocked);
  EXPECT_THROW(build_tile_set(map.config, {}, mjolnir::BuildStage::kEnhance,
                              mjolnir::BuildStage::kCleanup),
               std::exception);
  EXPECT_FALSE(std::filesystem::exists(tile_dir / "build_info.json"));
  for (const auto& id : tiles)
    EXPECT_TRUE(std::filesystem::is_regular_file(tile_dir / baldr::GraphTile::FileSuffix(id)));
  for (const auto& file : std::filesystem::recursive_directory_iterator(tile_dir))
    EXPECT_NE(file.path().extension(), ".tmp");
}

TEST(UtilMjolnir, TileManifestReadFromFile) {
  const std::string filename(VALHALLA_SOURCE_DIR "test/data/tile_manifest0.json");
  TileManifest read = TileManifest::ReadFromFile(filename);
  EXPECT_EQ(read.tileset.size(), 3);
  EXPECT_EQ(read.tileset[GraphId{5970538}], 0);
  EXPECT_EQ(read.tileset[GraphId{5970546}], 54);
  EXPECT_EQ(read.tileset[GraphId{5970554}], 450);
}

TEST(UtilMjolnir, EmptyTileManifestToString) {
  const std::map<GraphId, size_t> tileset = {};
  TileManifest manifest{tileset};
  EXPECT_EQ(manifest.ToString(), "{\"tiles\":[]}");
}

TEST(UtilMjolnir, NonEmptyTileManifestToString) {
  // Serialize, explicitly check for JSON equality
  const std::map<GraphId, size_t> tileset = {{GraphId{5970538}, 0}};
  TileManifest manifest{tileset};
  std::stringstream buf;
  buf << manifest.ToString();
  ptree json;
  rapidjson::read_json(buf, json);
  // Check for expected:
  // "{\"tiles\":[{\"node_index\":0,\"graphid\":{\"value\":5970538,\"id\":0,\"tile_id\":746317,\"level\":2}}]}"
  size_t count = 0;
  for (const auto& tile_info : json.get_child("tiles")) {
    EXPECT_EQ(tile_info.second.get<size_t>("node_index"), 0);
    const auto& graph_id = tile_info.second.get_child("graphid");
    EXPECT_EQ(graph_id.get<uint64_t>("value"), 5970538);
    EXPECT_EQ(graph_id.get<uint32_t>("tile_id"), 746317);
    EXPECT_EQ(graph_id.get<uint32_t>("id"), 0);
    EXPECT_EQ(graph_id.get<uint32_t>("level"), 2);
    ++count;
  }
  // Only one element is present: vector/array access of this tree child is not possible.
  EXPECT_EQ(count, 1);
}

TEST(UtilMjolnir, TileManifestLogToFile) {
  const std::map<GraphId, size_t> src = {{GraphId{5970538}, 0},
                                         {GraphId{5970546}, 54},
                                         {GraphId{5970554}, 450}};
  TileManifest manifest{src};
  const std::string filename(VALHALLA_BINARY_DIR "dummy_tile_manifest0.json");
  manifest.LogToFile(filename);
  TileManifest read = TileManifest::ReadFromFile(filename);
  EXPECT_EQ(read.tileset.size(), 3);
  EXPECT_EQ(read.tileset[GraphId{5970538}], manifest.tileset[GraphId{5970538}]);
  EXPECT_EQ(read.tileset[GraphId{5970546}], manifest.tileset[GraphId{5970546}]);
  EXPECT_EQ(read.tileset[GraphId{5970554}], manifest.tileset[GraphId{5970554}]);
}

} // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
