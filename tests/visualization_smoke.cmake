if(NOT EXISTS "${GENERATOR}")
    message(FATAL_ERROR "nexsdfgen does not exist: ${GENERATOR}")
endif()
if(NOT EXISTS "${VISUALIZER}")
    message(FATAL_ERROR "nexsdfviz does not exist: ${VISUALIZER}")
endif()
if(NOT EXISTS "${MODEL}")
    message(FATAL_ERROR "visualization model does not exist: ${MODEL}")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(asset "${OUTPUT_DIR}/cube.nsdf")
execute_process(
    COMMAND "${GENERATOR}" "${MODEL}" "${asset}"
        --representation grid --reconstruction trilinear --resolution 12
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error)
if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "asset generation failed: ${generate_output}${generate_error}")
endif()

foreach(mode distance normal gradient-error depth error)
    set(image "${OUTPUT_DIR}/${mode}.ppm")
    set(repeated_image "${OUTPUT_DIR}/${mode}-repeated.ppm")
    execute_process(
        COMMAND "${VISUALIZER}" "${asset}" "${image}"
            --axis z --resolution 32 --mode "${mode}"
        RESULT_VARIABLE visualize_result
        OUTPUT_VARIABLE visualize_output
        ERROR_VARIABLE visualize_error)
    if(NOT visualize_result EQUAL 0)
        message(FATAL_ERROR "${mode} visualization failed: ${visualize_output}${visualize_error}")
    endif()
    if(NOT EXISTS "${image}")
        message(FATAL_ERROR "${mode} visualization did not create ${image}")
    endif()
    file(SIZE "${image}" image_size)
    if(image_size LESS 3000)
        message(FATAL_ERROR "${mode} visualization is unexpectedly small: ${image_size}")
    endif()
    file(READ "${image}" image_magic LIMIT 2 HEX)
    if(NOT image_magic STREQUAL "5036")
        message(FATAL_ERROR "${mode} visualization is not a binary PPM")
    endif()
    execute_process(
        COMMAND "${VISUALIZER}" "${asset}" "${repeated_image}"
            --axis z --resolution 32 --mode "${mode}"
        RESULT_VARIABLE repeated_result
        OUTPUT_VARIABLE repeated_output
        ERROR_VARIABLE repeated_error)
    if(NOT repeated_result EQUAL 0)
        message(FATAL_ERROR "repeated ${mode} visualization failed: ${repeated_output}${repeated_error}")
    endif()
    file(SHA256 "${image}" image_hash)
    file(SHA256 "${repeated_image}" repeated_hash)
    if(NOT image_hash STREQUAL repeated_hash)
        message(FATAL_ERROR "${mode} visualization output is not deterministic")
    endif()
endforeach()
