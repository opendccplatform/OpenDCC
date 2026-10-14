add_subdirectory(jsoncpp)
add_subdirectory(tiny_process)

install(DIRECTORY cli11 DESTINATION "include/opendcc/base/vendor")
install(DIRECTORY cpptoml DESTINATION "include/opendcc/base/vendor")
install(DIRECTORY cppzmq DESTINATION "include/opendcc/base/vendor")
install(DIRECTORY eventpp DESTINATION "include/opendcc/base/vendor")
install(DIRECTORY nonstd DESTINATION "include/opendcc/base/vendor")
install(DIRECTORY tsl DESTINATION "include/opendcc/base/vendor")

install(DIRECTORY tl DESTINATION "include/opendcc/base/vendor")
