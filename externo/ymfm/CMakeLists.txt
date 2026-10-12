# ymfm (BSD-3), commit 81aec25 de github.com/aaronsgiles/ymfm (tarball SHA256
# 9bcf001b30f53754c49cf2fb864792a64702b8aeebb105baa908ad5dd33f443f). Solo los archivos del YM2610
# (familia OPN + ADPCM + SSG), sin modificar.
add_library(ymfm STATIC ymfm_opn.cpp ymfm_adpcm.cpp ymfm_ssg.cpp)
target_include_directories(ymfm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(ymfm PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(ymfm PRIVATE -w)
