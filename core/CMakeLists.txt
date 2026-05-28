cmake_minimum_required(VERSION 3.18)
project(oscar_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(OSCAR_USE_OPENCV "Link OpenCV (camera + video writer + JPEG encode)" ON)
option(OSCAR_USE_ONNX   "Link ONNX Runtime (real YOLO inference)" OFF)

include(FetchContent)

# cpp-httplib (header only)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann json (header only)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

# sqlite3 (system)
find_package(SQLite3 REQUIRED)

# Sources
set(OSCAR_SRC
    main.cpp
    camera/CameraSource.cpp
    inference/Detector.cpp
    tracking/Tracker.cpp
    localization/Localizer.cpp
    storage/Storage.cpp
    server/Server.cpp
)

add_executable(oscar_core ${OSCAR_SRC})

target_include_directories(oscar_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(oscar_core PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    ${SQLite3_LIBRARIES}
    pthread
)

if(OSCAR_USE_OPENCV)
    find_package(OpenCV REQUIRED)
    target_compile_definitions(oscar_core PRIVATE OSCAR_HAVE_OPENCV=1)
    target_include_directories(oscar_core PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(oscar_core PRIVATE ${OpenCV_LIBS})
    message(STATUS "OSCAR: OpenCV enabled (${OpenCV_VERSION})")
else()
    message(STATUS "OSCAR: OpenCV disabled (synthetic frames + no video writer)")
endif()

if(OSCAR_USE_ONNX)
    if(NOT DEFINED ENV{ONNXRUNTIME_DIR})
        message(WARNING "OSCAR_USE_ONNX=ON but ONNXRUNTIME_DIR not set; falling back to stub")
    else()
        target_compile_definitions(oscar_core PRIVATE OSCAR_HAVE_ONNX=1)
        target_include_directories(oscar_core PRIVATE $ENV{ONNXRUNTIME_DIR}/include)
        target_link_directories(oscar_core PRIVATE $ENV{ONNXRUNTIME_DIR}/lib)
        target_link_libraries(oscar_core PRIVATE onnxruntime)
        message(STATUS "OSCAR: ONNX Runtime enabled ($ENV{ONNXRUNTIME_DIR})")
    endif()
endif()

install(TARGETS oscar_core RUNTIME DESTINATION bin)
