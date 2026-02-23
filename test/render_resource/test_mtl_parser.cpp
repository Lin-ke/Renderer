#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

// Test MTL file parsing with NPR parameters
TEST_CASE("MTL Parser - NPR LightMap and RIM parameters", "[mtl_parser]") {
    
    // Create a test MTL file
    std::filesystem::path test_mtl = "./assets/models/test_npr.mtl";
    std::filesystem::create_directories(test_mtl.parent_path());
    
    {
        std::ofstream file(test_mtl);
        file << R"(# Test NPR MTL file
newmtl test_npr_material
map_Kd Texture\diffuse.png
Ka 0.2 0.2 0.2
Kd 0.8 0.8 0.8
Ks 0 0 0
Ns 5
d 1
# NPR parameters
map_Ke Texture\lightmap.png
map_Ramp Texture\ramp.png
RimWidth 0.5
RimThreshold 0.1
RimStrength 1.2
RimColor 1.0 0.9 0.8
LambertClamp 0.6
RampOffset 0.1
)";
    }
    
    // Verify file exists
    REQUIRE(std::filesystem::exists(test_mtl));
    
    // Read and verify content
    std::ifstream check_file(test_mtl);
    std::string content((std::istreambuf_iterator<char>(check_file)),
                        std::istreambuf_iterator<char>());
    check_file.close();  // Close file before deletion
    
    CHECK(content.find("map_Ke") != std::string::npos);
    CHECK(content.find("map_Ramp") != std::string::npos);
    CHECK(content.find("RimWidth") != std::string::npos);
    CHECK(content.find("RimThreshold") != std::string::npos);
    CHECK(content.find("RimStrength") != std::string::npos);
    CHECK(content.find("RimColor") != std::string::npos);
    CHECK(content.find("LambertClamp") != std::string::npos);
    CHECK(content.find("RampOffset") != std::string::npos);
    
    // Cleanup
    std::filesystem::remove(test_mtl);
}

TEST_CASE("Klee MTL file exists", "[mtl_parser]") {
    std::filesystem::path klee_mtl = std::string(ENGINE_PATH) + "/assets/models/Klee/klee.mtl";
    REQUIRE(std::filesystem::exists(klee_mtl));
    
    std::ifstream file(klee_mtl);
    REQUIRE(file.is_open());
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    // Check for NPR-specific parameters
    CHECK(content.find("map_Ke") != std::string::npos);
    CHECK(content.find("map_Ramp") != std::string::npos);
    CHECK(content.find("RimWidth") != std::string::npos);
    CHECK(content.find("LambertClamp") != std::string::npos);
}
