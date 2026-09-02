# Investigation tooling for the playback-traffic attribution experiment. The
# census never routes traffic and is not part of the release surface.
add_library(ncm_network_stack_census STATIC census.cpp)
target_link_libraries(ncm_network_stack_census PUBLIC ncm_project_options)
target_include_directories(
  ncm_network_stack_census PUBLIC "${PROJECT_SOURCE_DIR}/include")

# A census-flavored `winmm.dll`. It carries the same generated thunks, the same
# forwarder, and the same absolute-system-path backend as the production proxy,
# so staging it into an isolated client changes the observation and nothing
# else. It is written to its own directory so it can never be mistaken for the
# release proxy.
add_library(ncm_winmm_census_proxy SHARED
  census_dll_main.cpp
  "${PROJECT_SOURCE_DIR}/src/winmm_proxy/system_backend_path.cpp"
  "${NCM_WINMM_GENERATED_DIR}/winmm_proxy_thunks.cpp"
  "${NCM_WINMM_GENERATED_DIR}/winmm_proxy.def"
)
target_link_libraries(
  ncm_winmm_census_proxy
  PRIVATE ncm_network_stack_census ncm_bootstrap ncm_winmm_forwarder ncm_project_options
)
set_target_properties(ncm_winmm_census_proxy PROPERTIES
  OUTPUT_NAME "winmm"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/census_proxy"
)
