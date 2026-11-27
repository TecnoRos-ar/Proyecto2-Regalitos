cmake_minimum_required(VERSION 3.20)
project(regalitos_backend CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Dependencias ----
find_package(Crow CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# ---- Binario principal ----
# Librería QR (Nayuki, dominio público)
add_library(qrcodegen STATIC third_party/qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC third_party)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp)

add_executable(regalitos ${SRC})

target_include_directories(regalitos PRIVATE src)

target_link_libraries(regalitos PRIVATE
    Crow::Crow
    libpqxx::pqxx
    nlohmann_json::nlohmann_json
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    unofficial-sodium::sodium
    CURL::libcurl
    qrcodegen
)

target_compile_options(regalitos PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

# ---- Tests ----
add_executable(tests tests/test_calc.cpp src/domain/calc.cpp)
target_include_directories(tests PRIVATE src)
target_link_libraries(tests PRIVATE doctest::doctest)

enable_testing()
add_test(NAME domain_calc COMMAND tests)
