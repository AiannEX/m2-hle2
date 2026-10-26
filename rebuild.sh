cmake -S . -B build-sdl -DSDL3_DIR=/usr/local/lib/cmake/SDL3 -DM2HLE_FRONTEND=sdl3
cmake --build build-sdl --config Release -j