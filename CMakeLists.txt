PROJECT(bit_bench)

cmake_minimum_required(VERSION 2.8)

# A warning appeared because cmake must look for cppunit.
# The warning told me to add this :
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.1 -std=c99 -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3")

# append external library SIMDCompressionAndIntersection

add_custom_target(SIMDCompLib
    COMMAND make libSIMDCompressionAndIntersection.a
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/SIMDCompressionAndIntersection/
)

# append external library libvbyte

add_custom_target(libvbyte
    COMMAND make vbyte
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/libvbyte/
)


include_directories("SIMDCompressionAndIntersection/include")
include_directories("libvbyte")

add_executable(bit_bench bit_pack_test.cpp)

# static libraries dependences
target_link_libraries(bit_bench ${PROJECT_SOURCE_DIR}/SIMDCompressionAndIntersection/libSIMDCompressionAndIntersection.a ${PROJECT_SOURCE_DIR}/libvbyte/libvbyte.a)

add_dependencies(bit_bench SIMDCompLib libvbyte)





