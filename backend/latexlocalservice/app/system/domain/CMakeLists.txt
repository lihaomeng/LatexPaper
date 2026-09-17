add_library(lol_system_domain STATIC src/ksystemdomain.cpp)
target_include_directories(lol_system_domain PUBLIC include)
target_link_libraries(lol_system_domain PRIVATE lol_kernel)
lol_register(lol_system_domain domain system)

