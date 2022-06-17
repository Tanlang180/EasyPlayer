# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.10.2)
project("easy_player")

# 导入ffmpeg
# 定义文件目录变量名
set(ffmpeg_lib_dir ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI})
set(ffmpeg_head_dir ${CMAKE_SOURCE_DIR}/ffmpeg)

# 导入头文件目录 也可以不导入
include_directories(
        ${ffmpeg_head_dir}
)



# 导入so文件
add_library(avcodec_lib SHARED IMPORTED)
set_target_properties(avcodec_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libavcodec.so)

add_library(avdevice_lib SHARED IMPORTED)
set_target_properties(avdevice_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libavdevice.so)

add_library(avfilter_lib SHARED IMPORTED)
set_target_properties(avfilter_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libavfilter.so)

add_library(avformat_lib SHARED IMPORTED)
set_target_properties(avformat_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libavformat.so)

add_library(avutil_lib SHARED IMPORTED)
set_target_properties(avutil_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libavutil.so)

add_library(postproc SHARED IMPORTED)
set_target_properties(postproc PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libpostproc.so)

add_library(swresample_lib SHARED IMPORTED)
set_target_properties(swresample_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libswresample.so)

add_library(swscale_lib SHARED IMPORTED)
set_target_properties(swscale_lib PROPERTIES IMPORTED_LOCATION
        ${ffmpeg_lib_dir}/libswscale.so)

find_library(log-lib log)

# 生成一个ffmpeg_lib动态库
file(GLOB src_files *.cpp)
add_library(ffmpeg_lib SHARED ${src_files})
# 关联目标资源
target_link_libraries(
        # 指定目标库
        ffmpeg_lib
        # 关联目标
        ${log-lib}  # liblog.so
        z           # libz.so
        android

        avcodec_lib avformat_lib avutil_lib swresample_lib swscale_lib OpenSLES

)