# BUILDING

This Application: **mail**
Version: **0.1.2**

## Local build

```bash
# one-shot build + install
./build.sh install
```

Or run the steps manually:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc || sysctl -n hw.ncpu || echo 4)"
sudo cmake --install .
```


## Install dependencies (from `deps.libraries`)

### System packages (required)
```bash
sudo apt-get update && sudo apt-get install -y build-essential libcurl4-openssl-dev libjansson-dev libssl-dev zlib1g-dev
```

### Development tooling (optional)
```bash
sudo apt-get update && sudo apt-get install -y gdb perl python3 python3-pip python3-venv valgrind
```

### 3rd-party/libuv

### lexbor

Clone & build:

```bash
git clone --depth 1 --branch v2.3.0 --single-branch "https://github.com/lexbor/lexbor.git" "lexbor"
cd "lexbor"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DLEXBOR_BUILD_TESTS=OFF -DLEXBOR_BUILD_EXAMPLES=OFF
cmake --build build -j"$(nproc)"
${SUDO}cmake --install build
cd ..
rm -rf "lexbor"
```

### libjansson-dev

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libjansson-dev
```

### libjwt

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/benmcollins/libjwt.git" "libjwt"
cd "libjwt"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local}
cmake --build build -j"$(nproc)"
${SUDO}cmake --install build
cd ..
rm -rf "libjwt"
```

### libuv

Clone & build:

```bash
git clone --depth 1 --branch v1.48.0 --single-branch "https://github.com/libuv/libuv.git" "libuv"
cd "libuv"
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DBUILD_TESTING=OFF
cmake --build build -j"$(nproc)"
${SUDO}cmake --install build
cd ..
rm -rf "libuv"
```

### OpenSSL

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libssl-dev
```

### sqlite3

Clone & build:

```bash
git clone --depth 1 --branch master --single-branch "https://github.com/sqlite/sqlite.git" "sqlite3"
cd "sqlite3"
./configure --prefix=${PREFIX:-/usr/local} CFLAGS="-O2 -DSQLITE_ENABLE_FTS5=1"
make -j"$(nproc)"
${SUDO}make install
cd ..
rm -rf "sqlite3"
```

### the-macro-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/the-macro-library.git" "the-macro-library"
cd "the-macro-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "the-macro-library"
```

### a-memory-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-memory-library.git" "a-memory-library"
cd "a-memory-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-memory-library"
```

### a-json-sax-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-json-sax-library.git" "a-json-sax-library"
cd "a-json-sax-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-json-sax-library"
```

### a-json-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-json-library.git" "a-json-library"
cd "a-json-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-json-library"
```

### the-lz4-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/the-lz4-library.git" "the-lz4-library"
cd "the-lz4-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "the-lz4-library"
```

### Threads

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y build-essential
```

### ZLIB

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y zlib1g-dev
```

### CURL

Install via package manager:

```bash
sudo apt-get update && sudo apt-get install -y libcurl4-openssl-dev
```

### a-curl-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-curl-library.git" "a-curl-library"
cd "a-curl-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-curl-library"
```

### h2o

Clone & build:

```bash
git clone --depth 1 --branch v2.2.6 --single-branch "https://github.com/h2o/h2o.git" "h2o"
cd "h2o"
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${PREFIX:-/usr/local} -DWITH_MRUBY=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j"$(nproc)"
${SUDO}cmake --install build
cd ..
rm -rf "h2o"
```

### h2o-c-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/h2o-c-library.git" "h2o-c-library"
cd "h2o-c-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "h2o-c-library"
```

### the-io-library

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/the-io-library.git" "the-io-library"
cd "the-io-library"
./build.sh clean
./build.sh install
cd ..
rm -rf "the-io-library"
```

### a-curl-gcloud-plugin

Clone & build:

```bash
git clone --depth 1 --single-branch "https://github.com/contactandyc/a-curl-gcloud-plugin.git" "a-curl-gcloud-plugin"
cd "a-curl-gcloud-plugin"
./build.sh clean
./build.sh install
cd ..
rm -rf "a-curl-gcloud-plugin"
```

