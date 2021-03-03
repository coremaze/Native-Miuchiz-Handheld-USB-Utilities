#!/bin/bash

start_dir=$(dirname $(realpath $0))
build_dir="$start_dir/build"

cd "$start_dir"

if [ -d "$build_dir" ]; then
    rm -rf "$build_dir"
fi

mkdir "$build_dir"

for file in "CMakeToolchains"/*.cmake; do
    full_toolchain_path=$(realpath $file)
    toolchain_name=$(basename "$full_toolchain_path" .cmake)
    cd "$build_dir"
    mkdir "$toolchain_name"
    cd "$toolchain_name"
    for type in "Release" "Debug"; do
        mkdir "$type";
        cd "$type"
        cmake -DCMAKE_TOOLCHAIN_FILE="$full_toolchain_path" -DCMAKE_BUILD_TYPE="$type" "$start_dir"
        make -j
        cd ..
    done

    cd "$start_dir"
done