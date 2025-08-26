cmake --build ../build --target exchange_server && clear && ../bin/exchange_server

# cmake -S . -B ../build-asan -DENABLE_ASAN=ON
# cmake --build ../build-asan --target exchange_server && clear && ../bin/exchange_server
