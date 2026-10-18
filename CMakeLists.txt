cmake_minimum_required(VERSION 3.10)
project(pil VERSION 0.9.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall)

include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB SOURCES ${PROJECT_SOURCE_DIR}/source/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
   "PIL_MAJOR=${PROJECT_VERSION_MAJOR}"
   "PIL_MINOR=${PROJECT_VERSION_MINOR}"
   "PIL_PATCH=${PROJECT_VERSION_PATCH}"
)
