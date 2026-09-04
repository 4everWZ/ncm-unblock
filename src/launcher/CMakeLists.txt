add_library(ncm_launcher_process STATIC managed_process.cpp)
target_link_libraries(ncm_launcher_process PUBLIC ncm_project_options)
target_include_directories(ncm_launcher_process PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_library(ncm_launcher_loopback_ports STATIC loopback_port_pair.cpp)
target_link_libraries(ncm_launcher_loopback_ports PUBLIC ncm_project_options ws2_32)
target_include_directories(ncm_launcher_loopback_ports PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_library(ncm_launcher_mitm_certs STATIC mitm_certs.cpp)
target_link_libraries(ncm_launcher_mitm_certs PUBLIC ncm_project_options crypt32)
target_include_directories(ncm_launcher_mitm_certs PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_library(ncm_launcher_unm_sidecar STATIC unm_sidecar.cpp)
target_link_libraries(
  ncm_launcher_unm_sidecar
  PUBLIC ncm_launcher_process ncm_launcher_loopback_ports ncm_project_options
  PRIVATE iphlpapi ws2_32
)
target_include_directories(ncm_launcher_unm_sidecar PUBLIC "${PROJECT_SOURCE_DIR}/include")
