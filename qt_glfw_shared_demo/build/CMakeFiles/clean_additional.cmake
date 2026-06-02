# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "RelWithDebInfo")
  file(REMOVE_RECURSE
  "producer_qt/CMakeFiles/producer_qt_autogen.dir/AutogenUsed.txt"
  "producer_qt/CMakeFiles/producer_qt_autogen.dir/ParseCache.txt"
  "producer_qt/producer_qt_autogen"
  )
endif()
