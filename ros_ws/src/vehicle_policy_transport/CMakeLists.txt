cmake_minimum_required(VERSION 3.8)
project(vehicle_policy_transport)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES GNU|Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(Protobuf REQUIRED)

set(PROTOCOL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../protocol/policy_protocol.proto")
protobuf_generate_cpp(PROTOCOL_SOURCES PROTOCOL_HEADERS "${PROTOCOL_FILE}")

add_library(${PROJECT_NAME}
  src/mock_policy_transport.cpp
  src/unix_socket_policy_transport.cpp
  ${PROTOCOL_SOURCES}
)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME} geometry_msgs)
target_link_libraries(${PROJECT_NAME} protobuf::libprotobuf)

install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/policy_protocol.pb.h" DESTINATION include)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(geometry_msgs Protobuf)
ament_package()
