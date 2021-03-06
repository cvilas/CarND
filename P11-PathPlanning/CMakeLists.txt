project(Path_Planning)

cmake_minimum_required (VERSION 3.5)
SET( CMAKE_CXX_FLAGS -Wno-missing-braces)

add_definitions(-std=c++11)
set(CXX_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS, "${CXX_FLAGS}")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  include_directories(/usr/local/include)
  include_directories(/usr/local/opt/openssl/include)
  link_directories(/usr/local/lib)
  link_directories(/usr/local/opt/openssl/lib)
  link_directories(/usr/local/Cellar/libuv/1.11.0/lib)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin") 

include_directories(src/Eigen-3.3)
set(sources
  README.md
  src/todo.md
  data/highway_map.csv
  src/constants.h
  src/helpers.h
  src/helpers.cpp
  src/spline.h
  src/trajectory_planner.h
  src/trajectory_planner.cpp
  src/behaviour.h
  src/behaviour.cpp
  src/behaviour_planner.h
  src/behaviour_planner.cpp
  src/behaviour_predictor.h
  src/behaviour_predictor.cpp
  src/waypoint.h
  src/vehicle.h
  src/vehicle_model.h
  src/vehicle_model.cpp
  src/integrator.h)
add_executable(path_planning src/main.cpp ${sources})
target_link_libraries(path_planning z ssl uv uWS)

