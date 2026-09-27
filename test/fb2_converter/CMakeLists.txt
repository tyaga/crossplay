find_package(EXPAT REQUIRED)

add_executable(Fb2ConverterTest
  Fb2ConverterTest.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2Converter.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2Xml.cpp
  ${REPO_ROOT}/lib/Fb2/ZipStoreWriter.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
)
target_include_directories(Fb2ConverterTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(Fb2ConverterTest PRIVATE crosspoint_test_common EXPAT::EXPAT GTest::gtest_main)
gtest_discover_tests(Fb2ConverterTest)
