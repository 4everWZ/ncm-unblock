add_library(ncm_runtime_probe_lib STATIC
  pe_image.cpp
  process_snapshot.cpp
)

target_link_libraries(ncm_runtime_probe_lib
  PUBLIC ncm_project_options
  PRIVATE crypt32 iphlpapi version wintrust
)

target_include_directories(ncm_runtime_probe_lib
  PUBLIC "${PROJECT_SOURCE_DIR}/include"
)

add_executable(ncm_runtime_probe main.cpp)
target_link_libraries(ncm_runtime_probe PRIVATE ncm_runtime_probe_lib ncm_project_options)

install(TARGETS ncm_runtime_probe RUNTIME DESTINATION .)
