cmake_minimum_required (VERSION 3.10)
project (awaituv)

if (MSVC)
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -await /std:c++lastest")
    link_libraries (libuv libcurl)
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines-ts -std=c++2a -stdlib=libc++")
    find_package (CURL REQUIRED)
    link_libraries (${CURL_LIBRARIES})
    include_directories (inc /usr/local/include)
    link_directories(/usr/local/lib)
    link_libraries (uv)
endif ()

#SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines-ts -stdlib=libc++ -std=c++2a -Xclang -ast-print -fsyntax-only")


add_subdirectory (samples)




