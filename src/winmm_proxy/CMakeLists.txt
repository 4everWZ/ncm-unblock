set(NCM_WINMM_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/winmm.exports")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

# The manifest changes only when the pinned export surface is recaptured, so the
# sources are generated at configure time and reconfigured when it changes.
file(MAKE_DIRECTORY "${generated_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DMANIFEST=${NCM_WINMM_MANIFEST}"
    "-DOUT_DIR=${generated_dir}"
    "-DPREFIX=winmm"
    "-DBACKEND_MODULE=WINMM"
    -P "${PROJECT_SOURCE_DIR}/cmake/GenerateExportForwarder.cmake"
  RESULT_VARIABLE generation_status
  OUTPUT_VARIABLE generation_output
  ERROR_VARIABLE generation_output
)
if(NOT generation_status EQUAL 0)
  message(FATAL_ERROR "WinMM export forwarder generation failed: ${generation_output}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NCM_WINMM_MANIFEST}")

# Consumed by the synthetic parity fixtures under tests/.
set(NCM_WINMM_GENERATED_DIR "${generated_dir}" PARENT_SCOPE)

# The pinned surface as data. Test code reads it without linking the resolver,
# so a probe never needs a backend-path implementation.
add_library(ncm_winmm_export_table STATIC "${generated_dir}/winmm_export_table.cpp")
target_link_libraries(ncm_winmm_export_table PUBLIC ncm_project_options)
target_include_directories(ncm_winmm_export_table PUBLIC "${PROJECT_SOURCE_DIR}/include")

# The resolver. Each hosting DLL must also supply `ncm_winmm_backend_path`.
add_library(ncm_winmm_forwarder STATIC forwarder_core.cpp)
target_link_libraries(ncm_winmm_forwarder PUBLIC ncm_winmm_export_table ncm_project_options)

# The production-shaped proxy. Building it does not deploy it: placing a proxy
# in an NCM installation waits on the reversible deployment contract.
add_library(ncm_winmm_proxy SHARED
  system_backend_path.cpp
  "${generated_dir}/winmm_proxy_thunks.cpp"
  "${generated_dir}/winmm_proxy.def"
)
target_link_libraries(ncm_winmm_proxy PRIVATE ncm_winmm_forwarder ncm_project_options)
set_target_properties(ncm_winmm_proxy PROPERTIES OUTPUT_NAME "winmm")
