add_library(ncm_cef_probe_lib STATIC
  api_revision.cpp
)

target_link_libraries(ncm_cef_probe_lib
  PUBLIC ncm_project_options
  PRIVATE ncm_runtime_probe_lib
)

target_include_directories(ncm_cef_probe_lib
  PUBLIC "${PROJECT_SOURCE_DIR}/include"
)

add_executable(ncm_cef_probe main.cpp)
target_link_libraries(ncm_cef_probe PRIVATE ncm_cef_probe_lib ncm_project_options)

install(TARGETS ncm_cef_probe RUNTIME DESTINATION .)
