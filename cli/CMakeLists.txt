project(cli VERSION 1.0)
aux_source_directory(. src_file)
add_library(cli SHARED ${src_file})
target_link_libraries(${PROJECT_NAME} readline history)
