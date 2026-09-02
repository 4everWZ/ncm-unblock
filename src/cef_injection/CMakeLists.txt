add_library(ncm_cef_injection STATIC app_wrapper.cpp import_hook.cpp)
target_link_libraries(ncm_cef_injection PUBLIC ncm_project_options)
target_include_directories(ncm_cef_injection PUBLIC "${PROJECT_SOURCE_DIR}/include")
