set(MOVEIT_LIB_NAME moveit_planning_interface)

add_library(${MOVEIT_LIB_NAME} SHARED
  src/planning_interface.cpp
  src/planning_response.cpp
)
target_include_directories(moveit_planning_interface PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/moveit_core>
)
set_target_properties(moveit_planning_interface PROPERTIES VERSION "${${PROJECT_NAME}_VERSION}")
ament_target_dependencies(moveit_planning_interface
  moveit_msgs
  urdf
  urdfdom
  urdfdom_headers
)
target_link_libraries(${MOVEIT_LIB_NAME}
  moveit_robot_trajectory
  moveit_robot_state
  moveit_utils
)

install(DIRECTORY include/ DESTINATION include/moveit_core)
