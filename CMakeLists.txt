cmake_minimum_required(VERSION 3.20)
project(ningzhou LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源码为 UTF-8（含中文），可执行文件也使用 UTF-8 运行期编码，
# 这样 std::string 与 printf 直接输出中文不会乱码。
add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)

# 静态链接 GCC 运行时（libstdc++ / libgcc / libwinpthread）。
# 不加的话 exe 依赖 ucrt64\bin 下的 DLL，直接双击会以
# 0xC0000135（找不到 DLL）静默退出，什么都不打印。
# 静态链接后 exe 可以单独拷走运行，代价是体积大一点（约 +2MB）。
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
    add_compile_definitions(NZ_DEBUG=1)
else()
    add_compile_options(-O2 -Wall -Wextra)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src      # core/base.h 等
    ${CMAKE_CURRENT_SOURCE_DIR}          # data/reference_tables.h
)

# ---- 核心库：全部游戏规则 ----
# CONFIGURE_DEPENDS 很关键：不加的话，新增 .cpp 不会被自动纳入编译，
# ninja 只会说 "no work to do"，你会以为改了代码其实根本没编进去。
file(GLOB NZ_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp)
add_library(nzcore STATIC ${NZ_CORE_SOURCES})

# ---- 主程序：控制台界面 ----
add_executable(ningzhou ${CMAKE_CURRENT_SOURCE_DIR}/src/ui.cpp)
target_link_libraries(ningzhou PRIVATE nzcore)

# ---- Web 服务：原版界面 + C++ 规则 ----
# 这是推荐的玩法：复用 JS 版的 HTML/CSS，C++ 只当规则引擎。
add_executable(ningzhou_web ${CMAKE_CURRENT_SOURCE_DIR}/src/web_main.cpp)
target_link_libraries(ningzhou_web PRIVATE nzcore)
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
endif()
