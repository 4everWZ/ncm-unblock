add_library(ncm_proxy_observation STATIC request_observation.cpp)
target_link_libraries(ncm_proxy_observation PUBLIC ncm_project_options)
target_include_directories(ncm_proxy_observation PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(ncm_proxy_observer main.cpp)
target_link_libraries(ncm_proxy_observer PRIVATE ncm_proxy_observation ncm_project_options ws2_32)

install(TARGETS ncm_proxy_observer RUNTIME DESTINATION .)
