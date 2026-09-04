add_executable(managed_process_test_child managed_process_test_child.cpp)
target_link_libraries(managed_process_test_child PRIVATE ncm_project_options ws2_32)

add_executable(managed_process_test_owner managed_process_test_owner.cpp)
target_link_libraries(
  managed_process_test_owner PRIVATE ncm_launcher_process ncm_project_options
)

add_executable(managed_process_tests managed_process_tests.cpp)
target_link_libraries(managed_process_tests PRIVATE ncm_launcher_process ncm_project_options)

add_test(
  NAME managed_process_tests
  COMMAND managed_process_tests
    $<TARGET_FILE:managed_process_test_child>
    $<TARGET_FILE:managed_process_test_owner>
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

add_executable(ncm_watch_test_client ncm_watch_test_client.cpp)
target_link_libraries(ncm_watch_test_client PRIVATE ncm_project_options)

add_executable(ncm_watch_tests ncm_watch_tests.cpp)
target_link_libraries(ncm_watch_tests PRIVATE ncm_host_watch ncm_project_options)

add_test(
  NAME ncm_watch_tests
  COMMAND ncm_watch_tests $<TARGET_FILE:ncm_watch_test_client>
)
