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
c:/msys64/mingw64/include/c++/6.3.0/type_traits
#if !defined(__STRICT_ANSI__) && defined(_GLIBCXX_USE_FLOAT128)
//  template<>
//    struct __is_floating_point_helper<__float128>
//    : public true_type { };
#endif

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
cp -p libcmark.a  $WORK_DIR/swift/libcmark.a
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
cp -p clang clang++ /mingw64/bin
cp -rp ../../llvm/lib/clang/4.0.0 /mingw64/lib/clang



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

cmake -G "Ninja" ../../../swift -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang  -DCMAKE_CXX_COMPILER=clang++ -DPKG_CONFIG_EXECUTABLE=/mingw64/bin/pkg-config -DICU_UC_INCLUDE_DIR=/mingw64/include -DICU_UC_LIBRARY=/mingw64/lib/libicuuc.dll.a -DICU_I18N_INCLUDE_DIR=/mingw64/include -DICU_I18N_LIBRARY=/mingw64/lib/libicuin.dll.a -DSWIFT_INCLUDE_DOCS=FALSE -DSWIFT_PATH_TO_CMARK_BUILD=$WORK_DIR/build/NinjaMinGW/cmark -DSWIFT_PATH_TO_CMARK_SOURCE=$WORK_DIR/cmark -DSWIFT_PATH_TO_LLVM_SOURCE=$WORK_DIR/llvm -DSWIFT_PATH_TO_LLVM_BUILD=$WORK_DIR/build/NinjaMinGW/llvm -DSWIFT_STDLIB_ASSERTIONS:BOOL=TRUE ../../../swift

ninja
```

Build Foundation
----------------
```
export PATH=$PATH:$WORK_DIR/build/NinjaMinGW/swift/bin
SWIFTC=/c/Work/swift_msvc/build/NinjaMinGW/swift/bin/swiftc 
CLANG=/c/Work/swift_msvc/build/NinjaMinGW/llvm/bin/clang 
SWIFT=/c/Work/swift_msvc/build/NinjaMinGW/swift/bin/swift 
SDKROOT=/c/Work/swift_msvc/build/NinjaMinGW/swift 
#BUILD_DIR=/c/Work/swift_msvc/build/NinjaMinGW/foundation
BUILD_DIR=Build
DSTROOT=/
PREFIX=/usr/ 
/usr/bin/python ./configure Release -DXCTEST_BUILD_DIR=/c/Work/swift_msvc/build/NinjaMinGW/xctest-mingw-x86_64
```

Install Foundation
------------------
```
  export INSTALL_DIR=$WORK_DIR/build/NinjaMinGW/swift

  cd $WORK_DIR/swift-corelibs-foundation/Build/Foundation
  cp -p libFoundation.dll $INSTALL_DIR/bin
  cp -rp usr/lib/swift/CoreFoundation $INSTALL_DIR/lib/swift
  cp -p libFoundation.dll $INSTALL_DIR/lib/swift/mingw
  cp -p Foundation.swiftdoc Foundation.swiftmodule $INSTALL_DIR/lib/swift/mingw/x86_64
```

Unintall Foundation
-------------------
```
  export INSTALL_DIR=$WORK_DIR/build/NinjaMinGW/swift

  rm $INSTALL_DIR/bin/libFoundation.dll
  rm -rf $INSTALL_DIR/lib/swift/CoreFoundation
  rm $INSTALL_DIR/lib/swift/mingw/libFoundation.dll
  rm $INSTALL_DIR/lib/swift/mingw/x86_64/Foundation.swift*
```


Not implemented Foundation features
-----------------------------------
- class NSData: .alwaysMapped is not implemented (mmap, munmap is not exist in Windows)
- class Process: launch() method is not implemented
- class Host: most part are not implemented
- class NSFileManager: most part are not implemented

Build hint
----------
sed -i 's/C\\:/C:/' $(find Build -name *.d)
rm `find Build -name *.d`; ninja -j1 -k1 -f mingw.ninja  Build/Foundation/Foundation/XXXXX.swift.o

ln -s Build/Foundation/Foundation bld_found
ln -s Build/Foundation/CoreFoundation bld_co
Replace Build/Foundation/Foundation/ --> bld_found/
Replace Build/Foundation/CoreFoundation/ --> bld_co/


CF_OBJS="Build/Foundation/closure/data.c.o Build/Foundation/closure/runtime.c.o Build/Foundation/uuid/uuid.c.o bld_co/Base.subproj/CFBase.c.o bld_co/Base.subproj/CFFileUtilities.c.o bld_co/Base.subproj/CFPlatform.c.o bld_co/Base.subproj/CFRuntime.c.o bld_co/Base.subproj/CFSortFunctions.c.o bld_co/Base.subproj/CFSystemDirectories.c.o bld_co/Base.subproj/CFUtilities.c.o bld_co/Base.subproj/CFUUID.c.o bld_co/Base.subproj/CFWindowsUtilities.c.o bld_co/Collections.subproj/CFArray.c.o bld_co/Collections.subproj/CFBag.c.o bld_co/Collections.subproj/CFBasicHash.c.o bld_co/Collections.subproj/CFBinaryHeap.c.o bld_co/Collections.subproj/CFBitVector.c.o bld_co/Collections.subproj/CFData.c.o bld_co/Collections.subproj/CFDictionary.c.o bld_co/Collections.subproj/CFSet.c.o bld_co/Collections.subproj/CFStorage.c.o bld_co/Collections.subproj/CFTree.c.o bld_co/Error.subproj/CFError.c.o bld_co/Locale.subproj/CFCalendar.c.o bld_co/Locale.subproj/CFDateFormatter.c.o bld_co/Locale.subproj/CFLocale.c.o bld_co/Locale.subproj/CFLocaleIdentifier.c.o bld_co/Locale.subproj/CFLocaleKeys.c.o bld_co/Locale.subproj/CFNumberFormatter.c.o bld_co/NumberDate.subproj/CFBigNumber.c.o bld_co/NumberDate.subproj/CFDate.c.o bld_co/NumberDate.subproj/CFNumber.c.o bld_co/NumberDate.subproj/CFTimeZone.c.o bld_co/Parsing.subproj/CFBinaryPList.c.o bld_co/Parsing.subproj/CFOldStylePList.c.o bld_co/Parsing.subproj/CFPropertyList.c.o bld_co/Parsing.subproj/CFXMLInputStream.c.o bld_co/Parsing.subproj/CFXMLNode.c.o bld_co/Parsing.subproj/CFXMLParser.c.o bld_co/Parsing.subproj/CFXMLTree.c.o bld_co/Parsing.subproj/CFXMLInterface.c.o bld_co/PlugIn.subproj/CFBundle.c.o bld_co/PlugIn.subproj/CFBundle_Binary.c.o bld_co/PlugIn.subproj/CFBundle_Grok.c.o bld_co/PlugIn.subproj/CFBundle_InfoPlist.c.o bld_co/PlugIn.subproj/CFBundle_Locale.c.o bld_co/PlugIn.subproj/CFBundle_Resources.c.o bld_co/PlugIn.subproj/CFBundle_Strings.c.o bld_co/PlugIn.subproj/CFPlugIn.c.o bld_co/PlugIn.subproj/CFPlugIn_Factory.c.o bld_co/PlugIn.subproj/CFPlugIn_Instance.c.o bld_co/PlugIn.subproj/CFPlugIn_PlugIn.c.o bld_co/Preferences.subproj/CFApplicationPreferences.c.o bld_co/Preferences.subproj/CFPreferences.c.o bld_co/RunLoop.subproj/CFRunLoop.c.o bld_co/RunLoop.subproj/CFSocket.c.o bld_co/Stream.subproj/CFConcreteStreams.c.o bld_co/Stream.subproj/CFSocketStream.c.o bld_co/Stream.subproj/CFStream.c.o bld_co/String.subproj/CFBurstTrie.c.o bld_co/String.subproj/CFCharacterSet.c.o bld_co/String.subproj/CFString.c.o bld_co/String.subproj/CFStringEncodings.c.o bld_co/String.subproj/CFStringScanner.c.o bld_co/String.subproj/CFStringUtilities.c.o bld_co/String.subproj/CFStringTransform.c.o bld_co/StringEncodings.subproj/CFBuiltinConverters.c.o bld_co/StringEncodings.subproj/CFICUConverters.c.o bld_co/StringEncodings.subproj/CFPlatformConverters.c.o bld_co/StringEncodings.subproj/CFStringEncodingConverter.c.o bld_co/StringEncodings.subproj/CFStringEncodingDatabase.c.o bld_co/StringEncodings.subproj/CFUniChar.c.o bld_co/StringEncodings.subproj/CFUnicodeDecomposition.c.o bld_co/StringEncodings.subproj/CFUnicodePrecomposition.c.o bld_co/URL.subproj/CFURL.c.o bld_co/URL.subproj/CFURLAccess.c.o bld_co/URL.subproj/CFURLComponents.c.o bld_co/URL.subproj/CFURLComponents_URIParser.c.o bld_co/String.subproj/CFCharacterSetData.S.o bld_co/String.subproj/CFUnicodeData.S.o bld_co/String.subproj/CFUniCharPropertyDatabase.S.o bld_co/String.subproj/CFRegularExpression.c.o bld_co/String.subproj/CFAttributedString.c.o bld_co/String.subproj/CFRunArray.c.o bld_co/URL.subproj/CFURLSessionInterface.c.o"

SWF_OBJS="bld_found/NSObject.swift.o bld_found/NSAffineTransform.swift.o bld_found/NSArray.swift.o bld_found/NSAttributedString.swift.o bld_found/NSBundle.swift.o bld_found/NSByteCountFormatter.swift.o bld_found/NSCache.swift.o bld_found/NSCalendar.swift.o bld_found/NSCFArray.swift.o bld_found/NSCFBoolean.swift.o bld_found/NSCFDictionary.swift.o bld_found/NSCFSet.swift.o bld_found/NSCFString.swift.o bld_found/NSCharacterSet.swift.o bld_found/NSCFCharacterSet.swift.o bld_found/NSCoder.swift.o bld_found/NSComparisonPredicate.swift.o bld_found/NSCompoundPredicate.swift.o bld_found/NSConcreteValue.swift.o bld_found/NSData.swift.o bld_found/NSDate.swift.o bld_found/NSDateComponentsFormatter.swift.o bld_found/NSDateFormatter.swift.o bld_found/NSDateIntervalFormatter.swift.o bld_found/NSDecimal.swift.o bld_found/NSDecimalNumber.swift.o bld_found/NSDictionary.swift.o bld_found/NSEnergyFormatter.swift.o bld_found/NSEnumerator.swift.o bld_found/NSError.swift.o bld_found/NSExpression.swift.o bld_found/NSFileHandle.swift.o bld_found/NSFileManager.swift.o bld_found/NSFormatter.swift.o bld_found/NSGeometry.swift.o bld_found/Host.swift.o bld_found/NSHTTPCookie.swift.o bld_found/NSHTTPCookieStorage.swift.o bld_found/NSIndexPath.swift.o bld_found/NSIndexSet.swift.o bld_found/NSJSONSerialization.swift.o bld_found/NSKeyedCoderOldStyleArray.swift.o bld_found/NSKeyedArchiver.swift.o bld_found/NSKeyedArchiverHelpers.swift.o bld_found/NSKeyedUnarchiver.swift.o bld_found/NSLengthFormatter.swift.o bld_found/NSLocale.swift.o bld_found/NSLock.swift.o bld_found/NSLog.swift.o bld_found/NSMassFormatter.swift.o bld_found/NSNotification.swift.o bld_found/NSNotificationQueue.swift.o bld_found/NSNull.swift.o bld_found/NSNumber.swift.o bld_found/NSNumberFormatter.swift.o bld_found/NSObjCRuntime.swift.o bld_found/NSOperation.swift.o bld_found/NSOrderedSet.swift.o bld_found/NSPathUtilities.swift.o bld_found/NSPersonNameComponents.swift.o bld_found/NSPersonNameComponentsFormatter.swift.o bld_found/NSPlatform.swift.o bld_found/NSPort.swift.o bld_found/NSPortMessage.swift.o bld_found/NSPredicate.swift.o bld_found/NSProcessInfo.swift.o bld_found/Progress.swift.o bld_found/ProgressFraction.swift.o bld_found/NSPropertyList.swift.o bld_found/NSRange.swift.o bld_found/NSRegularExpression.swift.o bld_found/NSRunLoop.swift.o bld_found/NSScanner.swift.o bld_found/NSSet.swift.o bld_found/NSSortDescriptor.swift.o bld_found/NSSpecialValue.swift.o bld_found/NSStream.swift.o bld_found/NSString.swift.o bld_found/NSStringAPI.swift.o bld_found/NSSwiftRuntime.swift.o bld_found/Process.swift.o bld_found/NSTextCheckingResult.swift.o bld_found/NSThread.swift.o bld_found/NSTimer.swift.o bld_found/NSTimeZone.swift.o bld_found/NSURL.swift.o bld_found/NSURLAuthenticationChallenge.swift.o bld_found/NSURLCache.swift.o bld_found/NSURLCredential.swift.o bld_found/NSURLCredentialStorage.swift.o bld_found/NSURLError.swift.o bld_found/NSURLProtectionSpace.swift.o bld_found/NSURLProtocol.swift.o bld_found/NSURLRequest.swift.o bld_found/NSURLResponse.swift.o bld_found/NSURLSession.swift.o bld_found/NSUserDefaults.swift.o bld_found/NSUUID.swift.o bld_found/NSValue.swift.o bld_found/NSXMLDocument.swift.o bld_found/NSXMLDTD.swift.o bld_found/NSXMLDTDNode.swift.o bld_found/NSXMLElement.swift.o bld_found/NSXMLNode.swift.o bld_found/NSXMLParser.swift.o bld_found/FoundationErrors.swift.o bld_found/URL.swift.o bld_found/UUID.swift.o bld_found/Boxing.swift.o bld_found/ReferenceConvertible.swift.o bld_found/Date.swift.o bld_found/Data.swift.o bld_found/CharacterSet.swift.o bld_found/URLRequest.swift.o bld_found/PersonNameComponents.swift.o bld_found/Notification.swift.o bld_found/URLComponents.swift.o bld_found/DateComponents.swift.o bld_found/DateInterval.swift.o bld_found/IndexPath.swift.o bld_found/IndexSet.swift.o bld_found/NSStringEncodings.swift.o bld_found/ExtraStringAPIs.swift.o bld_found/Measurement.swift.o bld_found/NSMeasurement.swift.o bld_found/NSMeasurementFormatter.swift.o bld_found/Unit.swift.o bld_found/TimeZone.swift.o bld_found/Calendar.swift.o bld_found/Locale.swift.o bld_found/String.swift.o bld_found/Set.swift.o bld_found/Dictionary.swift.o bld_found/Array.swift.o bld_found/Bridging.swift.o bld_found/CGFloat.swift.o"

/* OLD */
/*
clang --target=x86_64-windows-gnu  -L/c/Work/swift_msvc/build/NinjaMinGW/swift/lib/swift/mingw  -Lbootstrap/x86_64-windows-gnu/usr/lib  ${CF_OBJS} ${SWF_OBJS}  -shared  -lswiftMinGwCrt `icu-config --ldflags` -Wl,-defsym,__CFConstantStringClassReference=_T010Foundation19_NSCFConstantStringCN,--allow-multiple-definition -lpthread -ldl -lm -lswiftCore -lxml2 -o Build/Foundation/libFoundation.dll -lws2_32 -lcurl -lrpcrt4
*/

ar rcs -o libfound.a ${CF_OBJS} ${SWF_OBJS}

/c/tool/gen_def libfound.a libFoundation.def

clang --target=x86_64-windows-gnu  -L/c/Work/swift_msvc/build/NinjaMinGW/swift/lib/swift/mingw  -Lbootstrap/x86_64-windows-gnu/usr/lib  libFoundation.def libfound.a -shared  -lswiftMinGwCrt `icu-config --ldflags` -Wl,-defsym,__CFConstantStringClassReference=_T010Foundation19_NSCFConstantStringCN,--allow-multiple-definition -lpthread -ldl -lm -lswiftCore -lxml2 -o Build/Foundation/libFoundation.dll -lws2_32 -lcurl -lrpcrt4

