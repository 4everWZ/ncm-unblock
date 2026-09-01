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
