add_executable(runtime_probe_tests runtime_probe_tests.cpp)
target_link_libraries(runtime_probe_tests PRIVATE ncm_runtime_probe_lib ncm_project_options)

add_test(NAME runtime_probe_tests COMMAND runtime_probe_tests)

add_executable(proxy_observation_tests proxy_observation_tests.cpp)
target_link_libraries(proxy_observation_tests PRIVATE ncm_proxy_observation ncm_project_options)

add_test(NAME proxy_observation_tests COMMAND proxy_observation_tests)

add_executable(managed_process_test_child managed_process_test_child.cpp)
target_link_libraries(managed_process_test_child PRIVATE ncm_project_options ws2_32)

add_executable(managed_process_tests managed_process_tests.cpp)
target_link_libraries(managed_process_tests PRIVATE ncm_launcher_process ncm_project_options)

add_test(
  NAME managed_process_tests
  COMMAND managed_process_tests $<TARGET_FILE:managed_process_test_child>
)

add_executable(loopback_port_pair_tests loopback_port_pair_tests.cpp)
target_link_libraries(
  loopback_port_pair_tests
  PRIVATE ncm_launcher_loopback_ports ncm_launcher_process ncm_project_options ws2_32
)

add_test(
  NAME loopback_port_pair_tests
  COMMAND loopback_port_pair_tests $<TARGET_FILE:managed_process_test_child>
)

add_executable(unm_sidecar_tests unm_sidecar_tests.cpp)
target_link_libraries(
  unm_sidecar_tests
  PRIVATE ncm_launcher_unm_sidecar ncm_launcher_loopback_ports ncm_project_options
)

add_test(
  NAME unm_sidecar_tests
  COMMAND unm_sidecar_tests $<TARGET_FILE:managed_process_test_child>
)

add_library(module_load_probe_fixture SHARED module_load_probe_fixture.cpp)
target_link_libraries(module_load_probe_fixture PRIVATE ncm_project_options)

add_executable(module_load_probe_fixture_target module_load_probe_fixture_target.cpp)
target_link_libraries(
  module_load_probe_fixture_target
  PRIVATE module_load_probe_fixture ncm_project_options
)

add_executable(module_load_probe_tests module_load_probe_tests.cpp)
target_link_libraries(
  module_load_probe_tests
  PRIVATE ncm_module_load_probe_lib ncm_project_options ole32
)

add_test(
  NAME module_load_probe_tests
  COMMAND module_load_probe_tests
    $<TARGET_FILE:module_load_probe_fixture_target>
    $<TARGET_FILE:module_load_probe_fixture>
)

# Synthetic WinMM parity fixtures. Each one is named winmm.dll and is emitted
# into its own directory so no test executable can pick one up from its own
# application directory.
function(ncm_add_winmm_fixture target directory)
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "winmm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${directory}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${directory}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${directory}"
  )
endfunction()

# Stands in for the system module: the same 193-entry surface, including the
# shared entry point behind the unnamed ordinal.
add_library(winmm_backend_fixture SHARED
  "${NCM_WINMM_GENERATED_DIR}/winmm_backend.cpp"
  "${NCM_WINMM_GENERATED_DIR}/winmm_backend.def"
)
target_link_libraries(winmm_backend_fixture PRIVATE ncm_project_options)
target_include_directories(winmm_backend_fixture PRIVATE "${NCM_WINMM_GENERATED_DIR}")
ncm_add_winmm_fixture(winmm_backend_fixture winmm_backend)

# The proxy under test, differing from the production DLL only in where its
# backend path comes from.
add_library(winmm_proxy_fixture SHARED
  winmm_fixture_backend_path.cpp
  "${NCM_WINMM_GENERATED_DIR}/winmm_proxy_thunks.cpp"
  "${NCM_WINMM_GENERATED_DIR}/winmm_proxy.def"
)
target_link_libraries(winmm_proxy_fixture PRIVATE ncm_winmm_forwarder ncm_project_options)
ncm_add_winmm_fixture(winmm_proxy_fixture winmm_proxy)

# The falsified alternative, kept buildable so the claim stays testable.
# `link.exe` binds each `.def` forwarder against an import library before it
# emits the forwarder string. The Windows SDK winmm.lib does not cover the whole
# pinned surface, so the control binds against the synthetic backend's import
# library instead and still emits `WINMM.<entry>` forwarder strings.
add_library(winmm_selfforward_fixture SHARED
  "${NCM_WINMM_GENERATED_DIR}/winmm_selfforward.cpp"
  "${NCM_WINMM_GENERATED_DIR}/winmm_selfforward.def"
)
target_link_libraries(winmm_selfforward_fixture PRIVATE ncm_project_options winmm_backend_fixture)
ncm_add_winmm_fixture(winmm_selfforward_fixture winmm_selfforward)

add_executable(winmm_forwarder_probe winmm_forwarder_probe.cpp)
target_link_libraries(winmm_forwarder_probe PRIVATE ncm_winmm_export_table ncm_project_options)
target_include_directories(winmm_forwarder_probe PRIVATE "${NCM_WINMM_GENERATED_DIR}")

add_executable(winmm_forwarder_tests winmm_forwarder_tests.cpp)
target_link_libraries(winmm_forwarder_tests PRIVATE ncm_winmm_export_table ncm_project_options)
target_include_directories(winmm_forwarder_tests PRIVATE "${NCM_WINMM_GENERATED_DIR}")
add_dependencies(winmm_forwarder_tests
  winmm_forwarder_probe winmm_proxy_fixture winmm_backend_fixture winmm_selfforward_fixture
)

add_test(
  NAME winmm_forwarder_tests
  COMMAND winmm_forwarder_tests
    $<TARGET_FILE:winmm_forwarder_probe>
    $<TARGET_FILE:winmm_proxy_fixture>
    $<TARGET_FILE:winmm_backend_fixture>
    $<TARGET_FILE:winmm_selfforward_fixture>
)
