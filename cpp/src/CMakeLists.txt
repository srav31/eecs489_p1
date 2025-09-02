add_executable(iPerfer main.cpp)
set_target_properties(iPerfer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Add your setup code above this line, then uncomment the line below

target_link_libraries(iPerfer spdlog::spdlog cxxopts::cxxopts)