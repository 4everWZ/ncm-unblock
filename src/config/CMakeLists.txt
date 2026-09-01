add_library(ncm_config STATIC settings.cpp)
target_link_libraries(ncm_config PUBLIC ncm_project_options)
target_include_directories(ncm_config PUBLIC "${PROJECT_SOURCE_DIR}/include")
