
Install cygwin64 2.6.0
----------------------
```
 Devel/clang              3.8.1-1 
      /cmake              3.6.2-1
      /gcc-core           5.4.0-1
      /gcc-g++            5.4.0-1
      /git                2.8.3-1
      /pkg-config         0.29.1-1
      /swig               3.0.7-1
  Libs/libedit-devel      20130712-1
      /libiconv-devel     1.14-3
      /libicu-devel       57.1-1
      /libncurses-devel   6.0-7.20160806
      /libsqlite3_0       3.14.1-1
      /libstdc++6         5.4.0-1
      /libuuid-devel      2.25.2-2
      /libxml2-devel      2.9.4-1
```

Patch gcc header
----------------
  
 - The header file **`sys/unistd.h`** should be modified. (avoid use of keyword '__block')
```
  Edit /usr/include/sys/unistd.h Line 53
    Change the string '__block' to 'block'
-void    _EXFUN(encrypt, (char *__block, int __edflag)); 
+void    _EXFUN(encrypt, (char *block, int __edflag));
```

Download sources
----------------
```
  export WORK_DIR=<working directory>
  cd $WORK_DIR
  
  git clone https://github.com/tinysun212/swift-windows.git swift
  git clone https://github.com/tinysun212/swift-llvm-cygwin.git llvm
  git clone https://github.com/tinysun212/swift-clang-cygwin.git clang
  git clone https://github.com/tinysun212/swift-corelibs-foundation.git swift-corelibs-foundation
  git clone https://github.com/apple/swift-cmark.git cmark
  git clone https://github.com/ninja-build/ninja.git

  # You should replace the YYYYMMDD to proper value. 
  cd swift; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd llvm; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd clang; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd swift-corelibs-foundation; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd cmark; git checkout 6873b; cd ..
  cd ninja; git checkout 2eb1cc9; cd ..
```

Build
-----
```
  cd $WORK_DIR/swift
  utils/build-script -R --build-swift-static-stdlib --foundation
```

Build and Install XCTest
------------------------
```
Backup and Remove lib/swift/CoreFoundation

utils/build-script --release --assertions --xctest -- --skip-test-cmark --skip-test-swift --skip-test-foundation --skip-build-benchmarks --skip-build-llvm

cd $WORKDIR/build/Ninja-ReleaseAssert/xctest-cygwin-x86_64
cp -p libXCTest.dll $WORKDIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/bin
cp -p libXCTest.dll $WORKDIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/lib/swift/cygwin
cp -p XCTest.swiftdoc XCTest.swiftmodule $WORKDIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/lib/swift/cygwin/x86_64
```

Build and Install Swift Package Manager
---------------------------------------
```
Recover CoreFoundation
XCTest should be installed

utils/build-script -R --swiftpm --llbuild --skip-build-foundation --skip-build-llvm  -j 3

cd $WORKDIR/build/Ninja-ReleaseAssert/swiftpm-cygwin-x86_64/.bootstrap/bin
cp -p swift-build.exe swift-package.exe swift-test.exe $WORKDIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/bin
cd $WORKDIR/build/Ninja-ReleaseAssert/swiftpm-cygwin-x86_64/.bootstrap/lib/swift
cp -rp pm $WORKDIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/lib/swift
```
