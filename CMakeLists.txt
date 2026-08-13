cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)

# This library has no third-party build dependency. Disable machine-wide
# vcpkg/MSBuild integration so a parent workspace cannot inject every installed
# library (and its runtime/post-build hooks) into the standalone tools.
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false;VcpkgEnableManifest=false")
project(NexDynSdf VERSION 0.1.0 LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

option(NEXSDF_BUILD_SHARED "Build the query library as a shared library" OFF)
option(NEXSDF_BUILD_TOOLS "Build command-line tools" ON)
option(NEXSDF_BUILD_TESTS "Build tests" ON)

include(GNUInstallDirs)

set(NEXSDF_LIBRARY_TYPE STATIC)
if(NEXSDF_BUILD_SHARED)
    set(NEXSDF_LIBRARY_TYPE SHARED)
endif()

add_library(NexSdfQuery ${NEXSDF_LIBRARY_TYPE}
    src/asset.cpp
    src/c_api.cpp
    src/geometry.cpp
    src/influence.cpp
    src/mesh_io.cpp
    src/reconstruction.cpp
)
add_library(NexSdf::Query ALIAS NexSdfQuery)
set_target_properties(NexSdfQuery PROPERTIES EXPORT_NAME Query)
target_compile_features(NexSdfQuery PUBLIC cxx_std_17)
target_include_directories(NexSdfQuery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_compile_definitions(NexSdfQuery PRIVATE NEXSDF_BUILDING_LIBRARY=1)
if(NEXSDF_BUILD_SHARED)
    target_compile_definitions(NexSdfQuery PUBLIC NEXSDF_SHARED=1)
endif()
if(MSVC)
    target_compile_options(NexSdfQuery PRIVATE /W4 /permissive- /EHsc /wd4251)
else()
    target_compile_options(NexSdfQuery PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NEXSDF_BUILD_TOOLS)
    add_executable(nexsdfgen tools/nexsdfgen.cpp)
    target_link_libraries(nexsdfgen PRIVATE NexSdf::Query)
    add_executable(nexsdfinspect tools/nexsdfinspect.cpp)
    target_link_libraries(nexsdfinspect PRIVATE NexSdf::Query)
    add_executable(nexsdfviz tools/nexsdfviz.cpp)
    target_link_libraries(nexsdfviz PRIVATE NexSdf::Query)
    add_executable(nexsdfmodelaudit tools/nexsdfmodelaudit.cpp)
    target_link_libraries(nexsdfmodelaudit PRIVATE NexSdf::Query)
    add_executable(nexsdfvalidate tools/nexsdfvalidate.cpp)
    target_link_libraries(nexsdfvalidate PRIVATE NexSdf::Query)
    if(WIN32)
        target_link_libraries(nexsdfvalidate PRIVATE Psapi)
    endif()
    install(TARGETS nexsdfgen nexsdfinspect nexsdfviz nexsdfmodelaudit nexsdfvalidate
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

include(CTest)
if(BUILD_TESTING AND NEXSDF_BUILD_TESTS)
    add_executable(nexsdf_unit_tests tests/unit_tests.cpp)
    target_link_libraries(nexsdf_unit_tests PRIVATE NexSdf::Query)
    target_compile_definitions(nexsdf_unit_tests PRIVATE
        NEXSDF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME nexsdf.unit COMMAND nexsdf_unit_tests)
    set_tests_properties(nexsdf.unit PROPERTIES LABELS unit)

    add_executable(nexsdf_c_api_tests tests/c_api_tests.c)
    target_link_libraries(nexsdf_c_api_tests PRIVATE NexSdf::Query)
    set_target_properties(nexsdf_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME nexsdf.c_api COMMAND nexsdf_c_api_tests)
    set_tests_properties(nexsdf.c_api PROPERTIES LABELS unit)

    add_executable(nexsdf_model_tests tests/model_tests.cpp)
    target_link_libraries(nexsdf_model_tests PRIVATE NexSdf::Query)
    target_compile_definitions(nexsdf_model_tests PRIVATE
        NEXSDF_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_test(NAME nexsdf.models COMMAND nexsdf_model_tests)
    set_tests_properties(nexsdf.models PROPERTIES LABELS regression)

    add_executable(nexsdf_influence_tests tests/influence_tests.cpp)
    target_link_libraries(nexsdf_influence_tests PRIVATE NexSdf::Query)
    target_compile_definitions(nexsdf_influence_tests PRIVATE
        NEXSDF_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_test(NAME nexsdf.influence COMMAND nexsdf_influence_tests)
    set_tests_properties(nexsdf.influence PROPERTIES LABELS regression)

    if(NEXSDF_BUILD_TOOLS)
        add_test(NAME nexsdf.model_catalog
            COMMAND ${CMAKE_COMMAND}
                -DAUDITOR=$<TARGET_FILE:nexsdfmodelaudit>
                -DMODEL_DIR=${CMAKE_CURRENT_SOURCE_DIR}/models
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_catalog_smoke.cmake)
        set_tests_properties(nexsdf.model_catalog PROPERTIES LABELS regression)

        add_test(NAME nexsdf.validation
            COMMAND ${CMAKE_COMMAND}
                -DVALIDATOR=$<TARGET_FILE:nexsdfvalidate>
                -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/models/pycoco/obj_library/cube.obj
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/validation-smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/validation_smoke.cmake)
        set_tests_properties(nexsdf.validation PROPERTIES LABELS regression)
    endif()

    if(NEXSDF_BUILD_TOOLS)
        add_test(NAME nexsdf.visualization
            COMMAND ${CMAKE_COMMAND}
                -DGENERATOR=$<TARGET_FILE:nexsdfgen>
                -DVISUALIZER=$<TARGET_FILE:nexsdfviz>
                -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/models/pycoco/obj_library/cube.obj
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/visualization-smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/visualization_smoke.cmake)
        set_tests_properties(nexsdf.visualization PROPERTIES LABELS integration)
    endif()

    add_executable(nexsdf_exact_benchmark tests/benchmark_exact.cpp)
    target_link_libraries(nexsdf_exact_benchmark PRIVATE NexSdf::Query)
    target_compile_definitions(nexsdf_exact_benchmark PRIVATE
        NEXSDF_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_test(NAME nexsdf.benchmark.exact COMMAND nexsdf_exact_benchmark)
    set_tests_properties(nexsdf.benchmark.exact PROPERTIES LABELS benchmark)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS NexSdfQuery EXPORT NexSdfQueryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NexDynSdf)
install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NexDynSdf/docs)
install(DIRECTORY models/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NexDynSdf/models)
install(EXPORT NexSdfQueryTargets
    FILE NexSdfQueryTargets.cmake
    NAMESPACE NexSdf::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NexDynSdf)
configure_package_config_file(
    cmake/NexDynSdfConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NexDynSdfConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NexDynSdf)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NexDynSdfConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NexDynSdfConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NexDynSdfConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NexDynSdf)
