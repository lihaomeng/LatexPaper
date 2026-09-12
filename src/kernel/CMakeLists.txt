add_library(lol_kernel STATIC src/kkernel.cpp)
target_include_directories(lol_kernel PUBLIC include)
lol_register(lol_kernel kernel kernel)

