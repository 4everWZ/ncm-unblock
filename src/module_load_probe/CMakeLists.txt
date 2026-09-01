add_library(ncm_module_load_probe_lib STATIC module_load_probe.cpp)
target_link_libraries(
  ncm_module_load_probe_lib
  PUBLIC ncm_project_options
  PRIVATE ncm_runtime_probe_lib
)
target_include_directories(
  ncm_module_load_probe_lib
  PUBLIC "${PROJECT_SOURCE_DIR}/include"
)

add_executable(ncm_module_load_probe main.cpp)
target_link_libraries(
  ncm_module_load_probe
  PRIVATE ncm_module_load_probe_lib ncm_project_options
)

install(TARGETS ncm_module_load_probe RUNTIME DESTINATION investigation)
