cmake_minimum_required(VERSION 3.20)
project(UnityFixture LANGUAGES CXX)
add_library(fixture SHARED fixture.cpp)
target_compile_features(fixture PRIVATE cxx_std_17)
