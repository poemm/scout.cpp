
The C++ file `scout.cpp` implements the [Scout spec](https://ethresear.ch/t/phase-2-execution-prototyping-engine-ewasm-scout/5509) to parse yaml test files, and to execute the Wasm tests.


# Dependencies

We use `yaml-cpp` to parse Scout test yaml format and `wabt` to execute Wasm. If the dependencies are not already present, then download them with `git clone`. If you don't download these dependencies into the main directory of this project, then you will have to adjust paths to them below. CMake is required to compile `yaml-cpp` and `wabt`, and optional to compile `scout.cpp`.

```
cd scout.cpp
git clone https://github.com/jbeder/yaml-cpp.git
cd yaml-cpp && git checkout 587b24e2eedea1afa21d79419008ca5f7bda3bf4 . && cd ..
git clone https://github.com/WebAssembly/wabt.git
cd wabt && git checkout 8be933ef8c1a6539823b0ed77b3a41524888e19d . && cd ..
```


# Compile

Two options.

Option 1) CMake for everything.

```
# from scout.cpp repo directory
# make sure that dependencies are in this directory, otherwise adjust paths in CMakeLists.txt
mkdir build && cd build
cmake -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release ..
make	# this outputs scout.exec, and also builds wabt and yaml-cpp
cp scout.exec ..
cd ..
```

Option 2) g++ or clang++ for scout.cpp.

```
# from scout.cpp repo directory

# compile yaml-cpp
cd yaml-cpp/
mkdir build && cd build
cmake ..
make	 # will output libyaml-cpp.a
cd ../..

# compile wabt
cd wabt
mkdir build && cd build
cmake -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release ..
make	# will output library libwabt.a
cd ../..

# compile scout.cpp, adjust paths to includes and libraries as needed
g++ scout.cpp -Iwabt/ -Iwabt/build/ -Iyaml-cpp/include/ -Lwabt/build/ -Lyaml-cpp/build/ -lwabt -lyaml-cpp -static -o scout.exec
# this outputs scout.exec
```


# Execute

```
# from scout.cpp repo directory
scout.exec helloworld.yaml
# warning: yaml files specify path to wasm files relative to scout.exec, everything is in the same directory for now
```
