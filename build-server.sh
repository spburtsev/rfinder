if [ ! -d "out" ]; then
  mkdir out
fi

clang++ -O2 -Wall server.cpp fs.cpp -o out/rfinder-server
