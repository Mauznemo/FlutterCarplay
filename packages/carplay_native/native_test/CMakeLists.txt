# Native unit tests: the parts of the core that run without Flutter, an adapter or a
# phone. Standalone on purpose, so CI can build them with nothing but a compiler.
#
#   tools/native-test.sh
#
# Not part of the plugin build.
cmake_minimum_required(VERSION 3.16)
project(carplay_native_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_executable(protocol_test
  protocol_test.cc
  "${CP_SRC}/dongle/protocol.cc"
)
target_include_directories(protocol_test PRIVATE "${CP_SRC}")
target_compile_options(protocol_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME protocol COMMAND protocol_test)

add_executable(stream_parser_test
  stream_parser_test.cc
  "${CP_SRC}/dongle/protocol.cc"
  "${CP_SRC}/dongle/stream_parser.cc"
)
target_include_directories(stream_parser_test PRIVATE "${CP_SRC}")
target_compile_options(stream_parser_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME stream_parser COMMAND stream_parser_test)

add_executable(frame_size_test
  frame_size_test.cc
  "${CP_SRC}/frame_size.cc"
)
target_include_directories(frame_size_test PRIVATE "${CP_SRC}")
target_compile_options(frame_size_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frame_size COMMAND frame_size_test)

add_executable(nmea_test
  nmea_test.cc
  "${CP_SRC}/dongle/nmea.cc"
)
target_include_directories(nmea_test PRIVATE "${CP_SRC}")
target_compile_options(nmea_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME nmea COMMAND nmea_test)
