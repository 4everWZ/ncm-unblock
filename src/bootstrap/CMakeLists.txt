add_library(ncm_bootstrap STATIC deferred_start.cpp)
target_link_libraries(ncm_bootstrap PUBLIC ncm_project_options)
target_include_directories(ncm_bootstrap PUBLIC "${PROJECT_SOURCE_DIR}/include")
