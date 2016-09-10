
Install cygwin64 2.5.2
----------------------
```
 Devel/clang              3.7.1-1 
      /cmake              3.3.2-1
      /gcc-core           5.4.0-1
      /gcc-g++            5.4.0-1
      /git                2.8.3-1
      /pkg-config         0.29-1
      /swig               3.0.7-1
  Libs/libedit-devel      20130712-1
      /libiconv-devel     1.14-3
      /libicu-devel       57.1-1
      /libncurses-devel   6.0-5.20160423
      /libsqlite3_0       3.13.0-1
      /libstdc++6         5.4.0-1
      /libuuid-devel      2.25.2-2
      /libxml2-devel      2.9.3-1
```

Patch gcc header
----------------
  
 - The header file **`c++config.h`** should be modified. (insert 3 lines)
```
  Edit /usr/lib/gcc/x86_64-pc-cygwin/5.4.0/include/c++/x86_64-pc-cygwin/bits/c++config.h Line 980
    Insert three lines which undefine _GLIBCXX_HAVE_TLS as follows
      #define _GLIBCXX_HAVE_TLS 1
->    #if defined (__clang__)
->    #undef _GLIBCXX_HAVE_TLS
->    #endif
``` 
 - The header file **`sys/unistd.h`** should be modified. (avoid use of keyword '__block')
```
  Edit /usr/include/sys/unistd.h Line 53
    Change the string '__block' to 'block'
-void    _EXFUN(encrypt, (char *__block, int __edflag)); 
+void    _EXFUN(encrypt, (char *block, int __edflag));
```

Patch clang header
------------------

  The header file 'limits.h' must be modified.
```
  Edit /usr/lib/clang/3.7.1/include/limits.h Line 28
    Insert a line which defines _GCC_NEXT_LIMITS_H as follows
      #ifndef __CLANG_LIMITS_H
      #define __CLANG_LIMITS_H

->    #define _GCC_NEXT_LIMITS_H
      /* The system's limits.h may, in turn, try to #include_next GCC's limits.h.
         Avert this #include_next madness. */
```

Patch cmake
-----------

  Without this patch, the import libraries will not be generated.
  (This patch is already applied to the CMake 3.5.2 build, but not yet to our 3.3.2.)
```
   Create two files as follows (each has one line)
 
     /usr/share/cmake-3.3.2/Modules/Platform/CYGWIN-Clang-C.cmake
       include(Platform/CYGWIN-GNU-C)
 
     /usr/share/cmake-3.3.2/Modules/Platform/CYGWIN-Clang-CXX.cmake
       include(Platform/CYGWIN-GNU-CXX)
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
