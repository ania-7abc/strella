# Parent project does not export its library target, so this CML implicitly
# depends on being added from it, i.e. the testing is done only from the build
# tree and is not feasible from an install location

project(strellaTests LANGUAGES CXX)

# ---- Dependencies ----

find_package(Catch2 REQUIRED)
include(Catch)

# ---- Tests ----

file(GLOB strella_TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")

add_executable(strella_test ${strella_TEST_SOURCES})
target_include_directories(strella_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_link_libraries(
    strella_test PRIVATE
    strella_lib
    Catch2::Catch2WithMain
)
target_compile_features(strella_test PRIVATE cxx_std_17)

catch_discover_tests(strella_test)

# ---- End-of-file commands ----

add_folders(Test)
