cmake_minimum_required(VERSION 3.20)
project(ningzhou LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源码为 UTF-8（含中文），可执行文件也使用 UTF-8 运行期编码，
# 这样 std::string 与 printf 直接输出中文不会乱码。
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()

# 静态链接 GCC 运行时（libstdc++ / libgcc / libwinpthread）。
# 不加的话 exe 依赖 ucrt64\bin 下的 DLL，直接双击会以
# 0xC0000135（找不到 DLL）静默退出，什么都不打印。
# 静态链接后 exe 可以单独拷走运行，代价是体积大一点（约 +2MB）。
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

add_compile_definitions($<$<CONFIG:Debug>:NZ_DEBUG=1>)
if(WIN32)
    # Windows headers must not replace std::min/std::max/numeric_limits::max.
    add_compile_definitions(NOMINMAX)
endif()
if(NOT MSVC)
    add_compile_options($<$<CONFIG:Debug>:-g> $<$<CONFIG:Debug>:-O0>
                        $<$<NOT:$<CONFIG:Debug>>:-O2>)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src      # core/base.h 等
    ${CMAKE_CURRENT_SOURCE_DIR}          # data/reference_tables.h
)

# ---- 核心库：全部游戏规则 ----
# CONFIGURE_DEPENDS 很关键：不加的话，新增 .cpp 不会被自动纳入编译，
# ninja 只会说 "no work to do"，你会以为改了代码其实根本没编进去。
file(GLOB_RECURSE NZ_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp)
file(GLOB_RECURSE NZ_CORE_HEADERS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h)
file(GLOB_RECURSE NZ_CORE_DOCS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/core/README.md)
file(GLOB NZ_PROJECT_DOCS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md)
set_source_files_properties(${NZ_CORE_DOCS} ${NZ_PROJECT_DOCS} PROPERTIES HEADER_FILE_ONLY TRUE)
add_library(nzcore STATIC ${NZ_CORE_SOURCES} ${NZ_CORE_HEADERS} ${NZ_CORE_DOCS} ${NZ_PROJECT_DOCS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src/core PREFIX "Backend"
             FILES ${NZ_CORE_SOURCES} ${NZ_CORE_HEADERS} ${NZ_CORE_DOCS})
source_group("Documentation" FILES ${NZ_PROJECT_DOCS})
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- 主程序：控制台界面 ----
add_executable(ningzhou ${CMAKE_CURRENT_SOURCE_DIR}/src/ui.cpp)
target_link_libraries(ningzhou PRIVATE nzcore)

# ---- Web 服务：原版界面 + C++ 规则 ----
# 这是推荐的玩法：复用 JS 版的 HTML/CSS，C++ 只当规则引擎。
add_executable(ningzhou_web ${CMAKE_CURRENT_SOURCE_DIR}/src/web_main.cpp)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ningzhou_web)
if(MSVC)
    # Keep desktop/mobile UI sources visible in the IDE without compiling them.
    file(GLOB_RECURSE NZ_WEB_SOURCES CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/web/*.mjs
         ${CMAKE_CURRENT_SOURCE_DIR}/web/*.css
         ${CMAKE_CURRENT_SOURCE_DIR}/web/*.html)
    set_source_files_properties(${NZ_WEB_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(ningzhou_web PRIVATE ${NZ_WEB_SOURCES})
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/web PREFIX "Web UI" FILES ${NZ_WEB_SOURCES})
endif()
find_package(Threads REQUIRED)
target_link_libraries(ningzhou_web PRIVATE nzcore Threads::Threads)
if(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # 服务器上不一定装了 libstdc++，静态链接 C++ 运行时，只依赖系统 glibc
    target_link_options(ningzhou_web PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(WIN32)
    target_link_libraries(ningzhou_web PRIVATE ws2_32)
endif()

# 把前端文件拷到可执行文件旁边的 web/ 目录，
# 这样 exe 可以单独拷走运行（前端跟着走）。
add_custom_target(nz_web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web
            $<TARGET_FILE_DIR:ningzhou_web>/web
    DEPENDS ningzhou_web
    COMMENT "同步前端文件到 web/")

# ---- 自测程序 ----
file(GLOB NZ_TEST_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
if(NZ_TEST_SOURCES)
    add_executable(ningzhou_tests ${NZ_TEST_SOURCES})
    target_link_libraries(ningzhou_tests PRIVATE nzcore)
    enable_testing()
    add_test(NAME ningzhou_core COMMAND ningzhou_tests)
    if(WIN32)
        # Keep disposable fixtures inside this checkout. UTF-8 path boundaries
        # remain covered, without requiring access to another user's Temp tree.
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test-temp)
        set(NZ_TEST_TEMP_PATH ${CMAKE_CURRENT_BINARY_DIR}/test-temp)
        find_program(NZ_POWERSHELL NAMES pwsh powershell)
        if(NZ_POWERSHELL)
            execute_process(COMMAND ${NZ_POWERSHELL} -NoProfile -File
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/test-temp-path.ps1
                -Path ${NZ_TEST_TEMP_PATH}
                RESULT_VARIABLE NZ_TEST_TEMP_STATUS OUTPUT_VARIABLE NZ_TEST_TEMP_SHORT
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NZ_TEST_TEMP_STATUS EQUAL 0 AND NOT NZ_TEST_TEMP_SHORT STREQUAL "")
                file(TO_CMAKE_PATH "${NZ_TEST_TEMP_SHORT}" NZ_TEST_TEMP_PATH)
            endif()
        endif()
        set_tests_properties(ningzhou_core PROPERTIES ENVIRONMENT
            "TEMP=${NZ_TEST_TEMP_PATH};TMP=${NZ_TEST_TEMP_PATH}")
    endif()
endif()
