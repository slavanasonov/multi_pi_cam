cmake_minimum_required (VERSION 2.8) 
project (raspicam_test)
find_package(raspicam REQUIRED)
add_executable (snap_raspicam snap_raspicam.cpp)  
target_link_libraries (snap_raspicam ${raspicam_LIBS})

