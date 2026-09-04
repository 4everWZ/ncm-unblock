add_library(ncm_host_watch STATIC ncm_watch.cpp)
target_link_libraries(ncm_host_watch PUBLIC ncm_project_options)
target_include_directories(ncm_host_watch PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(unm-host WIN32 main.cpp)
target_link_libraries(
  unm-host
  PRIVATE ncm_host_watch ncm_launcher_unm_sidecar ncm_project_options shell32
)

install(TARGETS unm-host RUNTIME DESTINATION native COMPONENT Runtime)
