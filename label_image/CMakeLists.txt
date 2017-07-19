cmake_minimum_required(VERSION 2.8.7)

set( CMAKE_VERBOSE_MAKEFILE off )
set( CMAKE_BUILD_TYPE Debug )
set(CMAKE_CXX_STANDARD 14)

project(label_image C CXX)
set(IMAGE_SRCS cv_process.cpp  main.cc)
add_executable(label_image ${IMAGE_SRCS}) 

if(MSVC)
set(TENSORFLOW_FOLDER /data/pzl/tensorflow)
else()
set(TENSORFLOW_FOLDER /data/pzl/tensorflow)
endif()

# Add modules
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include_directories(${TENSORFLOW_FOLDER})

if(MSVC)
else()

include_directories (
    /usr/local/include
    /usr/include
    /usr/include/openblas
    /usr/include/python2.7
    /usr/local/cuda/include
    )
include_directories(
    "${TENSORFLOW_FOLDER}/output/output/external/eigen_archive"
    "${TENSORFLOW_FOLDER}/output/output/external/protobuf/src/"
    "${TENSORFLOW_FOLDER}/output/output/execroot/tensorflow/bazel-out/local_linux-opt/genfiles/external/org_tensorflow"
    "${TENSORFLOW_FOLDER}/output/output/execroot/tensorflow/bazel-out/local-opt/genfiles"
    "${TENSORFLOW_FOLDER}/output/output/execroot/tensorflow/bazel-out/local_linux-opt/genfiles"

    )

target_link_libraries(label_image 
    ${TENSORFLOW_FOLDER}/bazel-bin/tensorflow/libtensorflow_all.so
    "${TENSORFLOW_FOLDER}/output/output/execroot/tensorflow/bazel-out/local_linux-opt/bin/external/jpeg/libsimd_x86_64.a"
    "${TENSORFLOW_FOLDER}/output/output/execroot/tensorflow/bazel-out/local_linux-opt/bin/external/jpeg/libjpeg.a"
    )
endif()

find_package(OpenCV QUIET COMPONENTS core highgui imgproc imgcodecs)
if(NOT OpenCV_FOUND) # if not OpenCV 3.x, then imgcodecs are not found
    find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc)
endif()
if(OpenCV_FOUND)
    include_directories(${OpenCV_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})
endif()


#include(Protobuf)
#include(Eigen)

# Eigen
#find_package(Eigen REQUIRED)
#include_directories(${Eigen_INCLUDE_DIRS})
# Protobuf
#find_package(Protobuf REQUIRED)
#include_directories(${Protobuf_INCLUDE_DIRS})
#target_link_libraries(label_image ${Protobuf_LIBRARIES})

