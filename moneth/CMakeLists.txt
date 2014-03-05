cmake_policy(SET CMP0015 NEW)

aux_source_directory(. SRC_LIST)

include_directories(../libethereum)
link_directories(../libethereum)

add_executable(moneth ${SRC_LIST})

if (${TARGET_PLATFORM} STREQUAL "w64")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
	target_link_libraries(moneth gcc)
	target_link_libraries(moneth gdi32)
	target_link_libraries(moneth ws2_32)
	target_link_libraries(moneth mswsock)
	target_link_libraries(moneth shlwapi)
	target_link_libraries(moneth iphlpapi)
	target_link_libraries(moneth cryptopp)
	target_link_libraries(moneth boost_system-mt-s)
	target_link_libraries(moneth boost_filesystem-mt-s)
	target_link_libraries(moneth boost_thread_win32-mt-s)
	set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)
else ()
	target_link_libraries(moneth ${CRYPTOPP_LIBRARIES})
	target_link_libraries(moneth boost_system)
	target_link_libraries(moneth boost_filesystem)
	find_package(Threads REQUIRED)
	target_link_libraries(moneth ${CMAKE_THREAD_LIBS_INIT})
endif ()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	include_directories(/usr/local/include)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

target_link_libraries(moneth ethereum)
target_link_libraries(moneth miniupnpc)
target_link_libraries(moneth leveldb)
target_link_libraries(moneth gmp)
target_link_libraries(moneth mongoclient)

install( TARGETS moneth DESTINATION bin )

