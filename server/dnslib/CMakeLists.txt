project(dns CXX)

add_library(dns STATIC dns.cxx)
target_compile_features(dns PUBLIC cxx_std_17)
target_include_directories(dns PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
