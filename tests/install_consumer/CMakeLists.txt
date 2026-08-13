cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false;VcpkgEnableManifest=false")
project(NexDynSdfInstallConsumer LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

find_package(NexDynSdf 0.1 REQUIRED CONFIG)

add_executable(nexsdf_install_consumer main.c)
target_link_libraries(nexsdf_install_consumer PRIVATE NexSdf::Query)
set_target_properties(nexsdf_install_consumer PROPERTIES LINKER_LANGUAGE CXX)
if(MSVC)
    set_property(TARGET nexsdf_install_consumer PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
