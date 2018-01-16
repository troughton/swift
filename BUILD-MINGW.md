# Build - Swift for Windows (MinGW)

Environment
----------------

1. Microsoft Windows 10 (64bit)
2. Run in "Msys64 MinGW64 Shell" in ADMINISTRATOR mode
   (Do not use the OS default "Command Prompt")

Choose any directory for working directory
```
export WORK_DIR=<Your working directory>
```

Install Packages
----------------------
```
pacman -S mingw-w64-x86_64-cmake       # 3.7.2-2
pacman -S mingw-w64-x86_64-ninja       # 1.7.2-1
pacman -S mingw-w64-x86_64-clang       # 3.9.1-3
pacman -S mingw-w64-x86_64-icu         # 57.1-2
pacman -S mingw-w64-x86_64-libxml2     # 2.9.4-4
pacman -S mingw-w64-x86_64-wineditline # 2.201-1
pacman -S mingw-w64-x86_64-winpthreads # git-5.0.0.4816.8692be6a-1
pacman -S mingw-w64-x86_64-pkg-config  # 0.29.2-1
pacman -S mingw-w64-x86_64-dlfcn       # 1.0.0-2
pacman -S make                         # 4.2.1
pacman -S python                       # 3.4.5-1
pacman -S python2                      # 2.7.13-1
```

Patch gcc header
----------------
  
 - The header file **`sys/unistd.h`** should be modified. (avoid use of keyword '__block')
```
  Edit /usr/include/sys/unistd.h Line 53
    Change the string '__block' to ''
-void    _EXFUN(encrypt, (char *__block, int __edflag)); 
+void    _EXFUN(encrypt, (char *, int __edflag));
```

sed -i 's;defined(_GLIBCXX_USE_FLOAT128)$;defined(_GLIBCXX_USE_FLOAT128) \&\& !defined\(__clang__\);' %MINGW64_DIR%/include/c++/*/type_traits


Download sources
----------------
```
  git clone https://github.com/tinysun212/swift-windows.git swift
  git clone https://github.com/tinysun212/swift-llvm-cygwin.git llvm
  git clone https://github.com/tinysun212/swift-clang-cygwin.git clang
  git clone https://github.com/tinysun212/swift-corelibs-foundation.git swift-corelibs-foundation
  git clone https://github.com/apple/swift-cmark.git cmark

# Choose the proper YYYYMMDD from the tag list of the repository. 
  cd swift; git checkout swift-mingw-YYYYMMDD ; cd ..
  cd llvm; git checkout swift-mingw-YYYYMMDD ; cd ..
  cd clang; git checkout swift-mingw-YYYYMMDD ; cd ..
  cd swift-corelibs-foundation; git checkout swift-mingw-YYYYMMDD ; cd ..
  cd cmark; git checkout master; cd ..
```

Patch sources
-------------
```
$WORK_DIR\clang\lib\Headers\stddef.h
Line 128: add //
#if !defined(_WINT_T)  // || __has_feature(modules)
```

Build cmark
-----------
```
mkdir -p $WORK_DIR/build/NinjaMinGW/cmark
cd $WORK_DIR/build/NinjaMinGW/cmark
cmake -G Ninja -D CMAKE_BUILD_TYPE=RELEASE -DCMAKE_C_COMPILER=clang  -DCMAKE_CXX_COMPILER=clang++ ../../../cmark
ninja

cd src
cp -p libcmark.dll.a libcmark.a
//cp -p libcmark.a  $WORK_DIR/swift/libcmark.a
```

Build clang
-----------
```
// To create the symbolic link, you should use the 'MKLINK.exe'
// Run "Command Prompt" in ADMINISTRATOR mode
(Command Prompt) set WORK_DIR=<Your working directory>
(Command Prompt) cd %WORK_DIR%/llvm/tools
(Command Prompt) mklink /d clang ..\..\clang

mkdir $WORK_DIR/build/NinjaMinGW/llvm
cd $WORK_DIR/build/NinjaMinGW/llvm
cmake -G Ninja -D CMAKE_BUILD_TYPE=RELEASE -DCMAKE_C_COMPILER=clang  -DCMAKE_CXX_COMPILER=clang++ -DLLVM_ENABLE_ASSERTIONS:BOOL=TRUE ../../../llvm

ninja
```

// Change Clang compiler
//cp -p clang clang++ /mingw64/bin
//cp -rp ../../llvm/lib/clang/4.0.0 /mingw64/lib/clang



Build Swift
-----------
```
// You already set environment variables WORK_DIR and PATH
// and you will use the commands - cmake, python, ninja, llvm tools here.
// If new command prompt is opened, set environment again.
// set PATH=%WORK_DIR%\build\NinjaMinGW\llvm\bin;%PATH%;C:\Program Files (x86)\CMake\bin;c:\mingw64\bin;c:\Tool
// set PKG_CONFIG_PATH=c:/pkg-config/conf

// Workaround for cmark
cp -p $WORK_DIR/build/NinjaMinGW/cmark/src/libcmark.a $WORK_DIR/swift
mkdir -p $WORK_DIR/build/NinjaMinGW/swift/bin		
cp $WORK_DIR/build/NinjaMinGW/cmark/src/libcmark.dll $WORK_DIR/build/NinjaMinGW/swift/bin		

cd $WORK_DIR/build/NinjaMinGW/swift

cmake -G "Ninja" ../../../swift -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$WORK_DIR/build/NinjaMinGW/llvm/bin/clang.exe  -DCMAKE_CXX_COMPILER=$WORK_DIR/build/NinjaMinGW/llvm/bin/clang++.exe -DCMAKE_ASM_COMPILER=$WORK_DIR/build/NinjaMinGW/llvm/bin/clang.exe -DPKG_CONFIG_EXECUTABLE=/mingw64/bin/pkg-config -DICU_UC_INCLUDE_DIR=/mingw64/include -DICU_UC_LIBRARY=/mingw64/lib/libicuuc.dll.a -DICU_I18N_INCLUDE_DIR=/mingw64/include -DICU_I18N_LIBRARY=/mingw64/lib/libicuin.dll.a -DSWIFT_INCLUDE_DOCS=FALSE -DSWIFT_PATH_TO_CMARK_BUILD=$WORK_DIR/build/NinjaMinGW/cmark -DSWIFT_PATH_TO_CMARK_SOURCE=$WORK_DIR/cmark -DSWIFT_PATH_TO_LLVM_SOURCE=$WORK_DIR/llvm -DSWIFT_PATH_TO_LLVM_BUILD=$WORK_DIR/build/NinjaMinGW/llvm -DSWIFT_STDLIB_ASSERTIONS:BOOL=TRUE ../../../swift

ninja
```

Build Foundation
----------------
```
  cd %WORK_DIR%/swift-corelibs-foundation
  SET SWIFTC=%WORK_DIR%/build/NinjaMinGW/swift/bin/swiftc 
  SET CLANG=%WORK_DIR%/build/NinjaMinGW/llvm/bin/clang 
  SET SWIFT=%WORK_DIR%/build/NinjaMinGW/swift/bin/swift 
  SET SDKROOT=%WORK_DIR%/build/NinjaMinGW/swift
  SET BUILD_DIR=Build
  SET DSTROOT=/
  SET PREFIX=/usr/
  SET MSYSTEM=MINGW64
  c:/msys64//usr/bin/python ./configure Release -DXCTEST_BUILD_DIR=%WORK_DIR%/build/NinjaMinGW/xctest-mingw-x86_64
  SET SDKROOT=
  
  // FIXEM: Workaround for ninja to work. We should these commands to only one command 'ninja'.
  c:/msys64/usr/bin/mkdir -p Build/Foundation/CoreFoundation Build/Foundation/Foundation
  MKLINK /d BFC Build\Foundation\CoreFoundation
  MKLINK /d BFF Build\Foundation\Foundation
  sed -i -e "s;Build/Foundation/CoreFoundation/;BFC/;g"  -e "s;Build/Foundation/Foundation/;BFF/;g"  build.ninja
  ninja

//  find Build -name *.d | xargs rm
```

Install Foundation
------------------
```
  SET INSTALL_DIR=%WORK_DIR%/build/NinjaMinGW/swift

  cd %WORK_DIR%/swift-corelibs-foundation/Build/Foundation
  cp -p libFoundation.dll %INSTALL_DIR%/bin
  cp -rp usr/lib/swift/CoreFoundation %INSTALL_DIR%/lib/swift
  cp -p libFoundation.dll %INSTALL_DIR%/lib/swift/mingw
  cp -p Foundation.swiftdoc Foundation.swiftmodule %INSTALL_DIR%/lib/swift/mingw/x86_64
```

Unintall Foundation
-------------------
```
  SET INSTALL_DIR=%WORK_DIR%/build/NinjaMinGW/swift

  rm %INSTALL_DIR%/bin/libFoundation.dll
  rm -rf %INSTALL_DIR%/lib/swift/CoreFoundation
  rm %INSTALL_DIR%/lib/swift/mingw/libFoundation.dll
  rm %INSTALL_DIR%/lib/swift/mingw/x86_64/Foundation.swift*
```


Not implemented Foundation features
-----------------------------------
- class NSData: .alwaysMapped is not implemented (mmap, munmap is not exist in Windows)
- class Process: launch() method is not implemented
- class Host: most part are not implemented
- class NSFileManager: most part are not implemented

Build hint
----------
  
/* OLD */
/*
clang --target=x86_64-windows-gnu  -L/c/Work/swift_msvc/build/NinjaMinGW/swift/lib/swift/mingw  -Lbootstrap/x86_64-windows-gnu/usr/lib  ${CF_OBJS} ${SWF_OBJS}  -shared  -lswiftMinGwCrt `icu-config --ldflags` -Wl,-defsym,__CFConstantStringClassReference=_T010Foundation19_NSCFConstantStringCN,--allow-multiple-definition -lpthread -ldl -lm -lswiftCore -lxml2 -o Build/Foundation/libFoundation.dll -lws2_32 -lcurl -lrpcrt4
*/

ar rcs -o libfound.a ${CF_OBJS} ${SWF_OBJS}

/c/tool/gen_def libfound.a libFoundation.def

clang --target=x86_64-windows-gnu  -L/c/Work/swift_msvc/build/NinjaMinGW/swift/lib/swift/mingw  -Lbootstrap/x86_64-windows-gnu/usr/lib  libFoundation.def libfound.a -shared  -lswiftMinGwCrt `icu-config --ldflags` -Wl,-defsym,__CFConstantStringClassReference=_T010Foundation19_NSCFConstantStringCN,--allow-multiple-definition -lpthread -ldl -lm -lswiftCore -lxml2 -o Build/Foundation/libFoundation.dll -lws2_32 -lcurl -lrpcrt4

