// RUN: %target-swift-frontend -module-name cxx_ir -I %S/Inputs/custom-modules -module-cache-path %t -enable-cxx-interop -emit-ir -o - -primary-file %s | %FileCheck %s

import CXXInterop

// CHECK-LABEL: define hidden swiftcc i32 @"$s6cxx_ir12basicMethods1as5Int32VSpySo0D0VG_tF"(i8*)
// CHECK: [[THIS_PTR1:%.*]] = bitcast i8* %0 to %TSo7MethodsV*
// CHECK: [[THIS_PTR2:%.*]] = bitcast %TSo7MethodsV* [[THIS_PTR1]] to %class.Methods*
// CHECK: [[RESULT:%.*]] = call i32 @_ZN7Methods12SimpleMethodEi(%class.Methods* [[THIS_PTR2]], i32 4)
// CHECK: ret i32 [[RESULT]]
func basicMethods(a: UnsafeMutablePointer<Methods>) -> Int32 {
  return a.pointee.SimpleMethod(4)
}

// CHECK-LABEL: define hidden swiftcc i32 @"$s6cxx_ir17basicMethodsConst1as5Int32VSpySo0D0VG_tF"(i8*)
// CHECK: [[THIS_PTR1:%.*]] = bitcast i8* %0 to %TSo7MethodsV*
// CHECK: [[THIS_PTR2:%.*]] = bitcast %TSo7MethodsV* [[THIS_PTR1]] to %class.Methods*
// CHECK: [[RESULT:%.*]] = call i32 @_ZNK7Methods17SimpleConstMethodEi(%class.Methods* [[THIS_PTR2]], i32 3)
// CHECK: ret i32 [[RESULT]]
func basicMethodsConst(a: UnsafeMutablePointer<Methods>) -> Int32 {
  return a.pointee.SimpleConstMethod(3)
}
