if(NOT EXISTS "${VALIDATOR}")
    message(FATAL_ERROR "nexsdfvalidate does not exist: ${VALIDATOR}")
endif()
if(NOT EXISTS "${MODEL}")
    message(FATAL_ERROR "validation model does not exist: ${MODEL}")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(first "${OUTPUT_DIR}/first.tsv")
set(second "${OUTPUT_DIR}/second.tsv")
foreach(output IN ITEMS "${first}" "${second}")
    execute_process(
        COMMAND "${VALIDATOR}" "${MODEL}" "${output}"
            --representation grid --reconstruction trilinear --resolution 8
            --field-samples 128 --surface-samples 128
            --query-repetitions 2 --seed 20260813
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_output
        ERROR_VARIABLE validation_error)
    if(NOT validation_result EQUAL 0)
        message(FATAL_ERROR "validation failed: ${validation_output}${validation_error}")
    endif()
endforeach()

file(STRINGS "${first}" lines)
list(LENGTH lines line_count)
if(NOT line_count EQUAL 2)
    message(FATAL_ERROR "validation TSV must contain one header and one result row")
endif()
list(GET lines 0 header)
list(GET lines 1 result_row)
string(TOLOWER "${result_row}" lower_result_row)
if(lower_result_row MATCHES "(^|\t)-?(nan|inf)(\t|$)")
    message(FATAL_ERROR "validation TSV contains a non-finite metric")
endif()
foreach(column IN ITEMS
        "model_hash_fnv1a64" "process_peak_working_set_bytes"
        "distance_max" "gradient_max" "normal_angle_max_deg"
        "eikonal_max" "mesh_to_field_max" "field_to_mesh_max"
        "symmetric_surface_max")
    string(FIND "${header}" "${column}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "validation TSV is missing required column: ${column}")
    endif()
endforeach()

# Timings and process memory are intentionally measured rather than expected to
# repeat. Strip them before checking that all deterministic inputs and metrics do.
file(STRINGS "${first}" first_lines)
file(STRINGS "${second}" second_lines)
list(GET first_lines 1 first_row)
list(GET second_lines 1 second_row)
string(REPLACE "\t" ";" first_fields "${first_row}")
string(REPLACE "\t" ";" second_fields "${second_row}")
list(LENGTH first_fields first_count)
list(LENGTH second_fields second_count)
if(NOT first_count EQUAL second_count)
    message(FATAL_ERROR "repeated validation rows have different field counts")
endif()
math(EXPR last_index "${first_count} - 1")
foreach(index RANGE 0 ${last_index})
    if(index EQUAL 20 OR index EQUAL 21 OR index EQUAL 22 OR index EQUAL 24)
        continue()
    endif()
    list(GET first_fields ${index} first_value)
    list(GET second_fields ${index} second_value)
    if(NOT first_value STREQUAL second_value)
        message(FATAL_ERROR "validation metric ${index} is not deterministic")
    endif()
endforeach()
