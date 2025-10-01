# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/usr/local/picosdk/tools/pioasm"
  "/home/IAS0360_lab_excercises_2025/project/build/pioasm"
  "/home/IAS0360_lab_excercises_2025/project/build/pioasm-install"
  "/home/IAS0360_lab_excercises_2025/project/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/tmp"
  "/home/IAS0360_lab_excercises_2025/project/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp"
  "/home/IAS0360_lab_excercises_2025/project/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src"
  "/home/IAS0360_lab_excercises_2025/project/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/IAS0360_lab_excercises_2025/project/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/IAS0360_lab_excercises_2025/project/build/pico-sdk/src/rp2_common/pico_cyw43_driver/pioasm/src/pioasmBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
