
Install cygwin64 2.6.0
----------------------
```
 Devel/automake           9-1
      /clang              3.8.1-1 
      /cmake              3.6.2-1
      /gcc-core           5.4.0-1
      /gcc-g++            5.4.0-1
      /git                2.8.3-1
      /pkg-config         0.29.1-1
      /swig               3.0.7-1
  Libs/libcurl-devel      7.50.3-1
      /libedit-devel      20130712-1
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
+void    _EXFUN(encrypt, (char *, int __edflag));
```
```
  $ cd /usr/lib/gcc/i686-pc-cygwin
  $ ln -s 5.4.0 4.7.3
  $ cd /usr/lib/gcc/i686-pc-cygwin/5.4.0/include/c++
  $ ln -s x86_64-pc-cygwin i686-pc-cygwin

  $ vi /usr/lib/gcc/i686-pc-cygwin/4.7.3/include/c++/type_traits
    //#if !defined(__STRICT_ANSI__) && defined(_GLIBCXX_USE_FLOAT128)
    #if 0
      template<>
        struct __is_floating_point_helper<__float128>
        : public true_type { };
    #endif
```

Download sources
----------------
```
  export WORK_DIR=/cgydrive/<working directory>
  cd $WORK_DIR
  
  git clone https://github.com/tinysun212/swift-windows.git swift
  git clone https://github.com/tinysun212/swift-llvm-cygwin.git llvm
  git clone https://github.com/tinysun212/swift-clang-cygwin.git clang
  git clone https://github.com/tinysun212/swift-corelibs-foundation.git swift-corelibs-foundation
  git clone https://github.com/tinysun212/swift-corelibs-xctest.git swift-corelibs-xctest
  git clone https://github.com/tinysun212/swift-llbuild.git llbuild
  git clone https://github.com/tinysun212/swift-package-manager.git swiftpm
  git clone https://github.com/apple/swift-cmark.git cmark
  git clone https://github.com/ninja-build/ninja.git

  # You should replace the YYYYMMDD to proper value. 
  cd swift; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd llvm; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd clang; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd swift-corelibs-foundation; git checkout swift-cygwin-YYYYMMDD ; cd ..
  cd swift-corelibs-xctest; git checkout swift-cygwin-YYYYMMDD ; cd ..
  
  cd cmark; git checkout 6873b; cd ..
  cd ninja; git checkout 2eb1cc9; cd ..
```

Build Compiler and Foundation
-----------------------------
```
  cd $WORK_DIR/swift
  utils/build-script -R --build-swift-static-stdlib --foundation --skip-build-libdispatch
```

Build XCTest
------------------------
```
  # Ensure the Foundation is not yet installed
  cd $WORK_DIR/swift
  utils/build-script --release --assertions --xctest -- --skip-test-cmark --skip-test-swift --skip-test-foundation --skip-build-benchmarks --skip-build-llvm --skip-build-libdispatch --skip-build-foundation
```

Install Foundation
------------------
```
  export INSTALL_DIR=$WORK_DIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64

  cd $WORK_DIR/build/Ninja-ReleaseAssert/foundation-cygwin-x86_64/Foundation
  cp -p libFoundation.dll $INSTALL_DIR/bin
  cp -rp usr/lib/swift/CoreFoundation $INSTALL_DIR/lib/swift
  cp -p libFoundation.dll $INSTALL_DIR/lib/swift/cygwin
  cp -p Foundation.swiftdoc Foundation.swiftmodule $INSTALL_DIR/lib/swift/cygwin/x86_64
```

Intall XCTest
-------------
```
  cd $WORK_DIR/build/Ninja-ReleaseAssert/xctest-cygwin-x86_64
  cp -p libXCTest.dll $INSTALL_DIR/bin
  cp -p libXCTest.dll $INSTALL_DIR/lib/swift/cygwin
  cp -p XCTest.swiftdoc XCTest.swiftmodule $INSTALL_DIR/lib/swift/cygwin/x86_64
```

Build Swift Package Manager
---------------------------
```
  # Ensure the Foundation and XCTest is installed
  
  # Add path to swift compiler
  export PATH=$PATH:$WORK_DIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/bin

  cd $WORK_DIR/swift
  utils/build-script -R --swiftpm --llbuild --skip-build-foundation --skip-build-llvm --skip-build-libdispatch -j 3
```

Install Swift Package Manager
-----------------------------
```
  cd $WORK_DIR/build/Ninja-ReleaseAssert/swiftpm-cygwin-x86_64/.bootstrap/bin
  cp -p swift-build.exe swift-package.exe swift-test.exe $WORK_DIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/bin
  cd $WORK_DIR/build/Ninja-ReleaseAssert/swiftpm-cygwin-x86_64/.bootstrap/lib/swift
  cp -rp pm $WORK_DIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/lib/swift
  
  cd $WORK_DIR/build/Ninja-ReleaseAssert/llbuild-cygwin-x86_64/bin
  cp -p swift-build-tool.exe $WORK_DIR/build/Ninja-ReleaseAssert/swift-cygwin-x86_64/bin
```

Build LLDB
----------
```
  cd $WORK_DIR
  lldb/scripts/build-swift-cmake.py --release
```