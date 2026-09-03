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

add_executable(config_settings_tests config_settings_tests.cpp)
target_link_libraries(config_settings_tests PRIVATE ncm_config ncm_project_options)
add_test(NAME config_settings_tests COMMAND config_settings_tests)

add_executable(ncm_session_test_client ncm_session_test_client.cpp)
target_link_libraries(ncm_session_test_client PRIVATE ncm_project_options)

add_executable(ncm_session_tests ncm_session_tests.cpp)
target_link_libraries(
  ncm_session_tests PRIVATE ncm_launcher_ncm_session ncm_project_options
)
add_test(
  NAME ncm_session_tests
  COMMAND ncm_session_tests $<TARGET_FILE:ncm_session_test_client>
)
