if(NOT EXISTS "${AUDITOR}")
    message(FATAL_ERROR "nexsdfmodelaudit does not exist: ${AUDITOR}")
endif()
if(NOT IS_DIRECTORY "${MODEL_DIR}")
    message(FATAL_ERROR "model catalog does not exist: ${MODEL_DIR}")
endif()

execute_process(
    COMMAND "${PYTHON_EXECUTABLE}"
        "${MODEL_DIR}/../scripts/regenerate_reference_models.py"
        --output-dir "${MODEL_DIR}/sdfmodel" --check
    RESULT_VARIABLE regeneration_result
    OUTPUT_VARIABLE regeneration_output
    ERROR_VARIABLE regeneration_error)
if(NOT regeneration_result EQUAL 0)
    message(FATAL_ERROR
        "generated reference models are stale:\n${regeneration_output}\n${regeneration_error}")
endif()

execute_process(
    COMMAND "${AUDITOR}" "${MODEL_DIR}" --expect-files 33 --expect-ready 32
    RESULT_VARIABLE audit_result
    OUTPUT_VARIABLE audit_output
    ERROR_VARIABLE audit_error)
if(NOT audit_result EQUAL 0)
    message(FATAL_ERROR "model catalog audit failed:\n${audit_output}\n${audit_error}")
endif()

foreach(required_path
        "pycoco/obj_model/complex_geometry/PressureLubricatedCam.obj"
        "sdfmodel/cam.nsm"
        "sdfmodel/gear.nsm"
        "sdfmodel/cam.stl"
        "nagata/cone.nsm"
        "sdflib/Gear.obj")
    string(FIND "${audit_output}" "${required_path}\t" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "catalog audit did not include ${required_path}")
    endif()
endforeach()

string(REGEX MATCH
    "pycoco/obj_model/complex_geometry/PressureLubricatedCam\\.obj\tobj\t3672\t7356\t1\t1\t1\t"
    cam_ready_row "${audit_output}")
if(cam_ready_row STREQUAL "")
    message(FATAL_ERROR "pressure-lubricated cam is not the expected runtime-ready mesh")
endif()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/model-catalog-audit.tsv" "${audit_output}")

set(committed_audit "${MODEL_DIR}/AUDIT.tsv")
if(NOT EXISTS "${committed_audit}")
    message(FATAL_ERROR "committed model audit does not exist: ${committed_audit}")
endif()
file(READ "${committed_audit}" committed_audit_output)
if(NOT committed_audit_output STREQUAL audit_output)
    message(FATAL_ERROR
        "models/AUDIT.tsv is stale; run scripts/update_model_audit.ps1")
endif()

set(catalog "${MODEL_DIR}/CATALOG.tsv")
if(NOT EXISTS "${catalog}")
    message(FATAL_ERROR "model catalog metadata does not exist: ${catalog}")
endif()
file(STRINGS "${catalog}" catalog_lines)
list(LENGTH catalog_lines catalog_line_count)
if(NOT catalog_line_count EQUAL 35)
    message(FATAL_ERROR "expected catalog header plus 34 assets, found ${catalog_line_count} lines")
endif()
list(REMOVE_AT catalog_lines 0)
foreach(line IN LISTS catalog_lines)
    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 11)
        message(FATAL_ERROR "catalog row has ${field_count} fields: ${line}")
    endif()
    list(GET fields 0 relative_path)
    list(GET fields 9 expected_hash)
    list(GET fields 10 expected_size)
    set(asset "${MODEL_DIR}/${relative_path}")
    if(NOT EXISTS "${asset}")
        message(FATAL_ERROR "catalogued asset does not exist: ${relative_path}")
    endif()
    file(SHA256 "${asset}" actual_hash)
    string(TOLOWER "${actual_hash}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "catalogued asset hash differs: ${relative_path}")
    endif()
    file(SIZE "${asset}" actual_size)
    if(NOT actual_size EQUAL expected_size)
        message(FATAL_ERROR "catalogued asset size differs: ${relative_path}")
    endif()
endforeach()
