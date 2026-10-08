cmake_minimum_required(VERSION 3.21)
project(yue2-abcedit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

add_executable(yue2-abcedit
  src/main.cpp
  src/audio/PreviewSound.cpp
  src/core/AbcDialect.cpp
  src/core/AbcChecker.cpp
  src/core/RefusalLog.cpp
  src/core/SongModel.cpp
  src/import/MidiImport.cpp
  src/import/StandardAbcReader.cpp
  src/convert/ChordFold.cpp
  src/convert/Quantizer.cpp
  src/convert/Exporter.cpp
  src/ui/MainWindow.cpp
)

target_include_directories(yue2-abcedit PRIVATE src)

target_link_libraries(yue2-abcedit PRIVATE Qt6::Widgets Qt6::Multimedia)

enable_testing()

add_executable(checker_test
  tests/checker_test.cpp
  src/core/AbcDialect.cpp
  src/core/AbcChecker.cpp
)
target_include_directories(checker_test PRIVATE src)
add_test(NAME checker_parity
         COMMAND checker_test ${CMAKE_SOURCE_DIR}/example_code)
set_tests_properties(checker_parity PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(pipeline_test
  tests/pipeline_test.cpp
  src/core/AbcDialect.cpp
  src/core/AbcChecker.cpp
  src/core/RefusalLog.cpp
  src/core/SongModel.cpp
  src/import/MidiImport.cpp
  src/import/StandardAbcReader.cpp
  src/convert/ChordFold.cpp
  src/convert/Quantizer.cpp
  src/convert/Exporter.cpp
)
target_include_directories(pipeline_test PRIVATE src)
add_test(NAME pipeline_mid2abc
         COMMAND pipeline_test ${CMAKE_SOURCE_DIR}/tests/data)
set_tests_properties(pipeline_mid2abc PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(editor_test
  tests/editor_test.cpp
  src/audio/PreviewSound.cpp
  src/ui/MainWindow.cpp
  src/core/AbcDialect.cpp
  src/core/AbcChecker.cpp
  src/core/RefusalLog.cpp
  src/core/SongModel.cpp
  src/import/MidiImport.cpp
  src/import/StandardAbcReader.cpp
  src/convert/ChordFold.cpp
  src/convert/Quantizer.cpp
  src/convert/Exporter.cpp
)
target_include_directories(editor_test PRIVATE src)
target_link_libraries(editor_test PRIVATE Qt6::Widgets Qt6::Multimedia)
add_test(NAME editor_open_edit
         COMMAND editor_test)
set_tests_properties(editor_open_edit PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(preview_test
  tests/preview_test.cpp
  src/audio/PreviewSound.cpp
  src/core/AbcDialect.cpp
  src/core/AbcChecker.cpp
)
target_include_directories(preview_test PRIVATE src)
add_test(NAME preview_render
         COMMAND preview_test)
set_tests_properties(preview_render PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(yue2-abcconvert
  src/cli/AbcConvert.cpp
  src/core/AbcDialect.cpp
  src/core/AbcChecker.cpp
  src/core/RefusalLog.cpp
  src/core/SongModel.cpp
  src/import/MidiImport.cpp
  src/import/StandardAbcReader.cpp
  src/convert/ChordFold.cpp
  src/convert/Quantizer.cpp
  src/convert/Exporter.cpp
)
target_include_directories(yue2-abcconvert PRIVATE src)
add_test(NAME cli_convert
         COMMAND yue2-abcconvert --force tests/data/scale.mid /tmp/opencode/cli-scale.abc /tmp/opencode/cli-scale.log)
set_tests_properties(cli_convert PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
