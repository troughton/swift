// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -enable-cxx-interop %s -emit-ir -I %S/Inputs/custom-modules/CXXModule -o - | %FileCheck %s

import CXXModule

// TODO: Support constructors instead of just zero-initializing.
var account = BankAccount()

// CHECK: call void @{{.*}}BankAccount{{.*}}deposit{{.*}}(%class.BankAccount* {{.*}}, i32 100)
account.deposit(100)

// CHECK: call zeroext i1 @{{.*}}BankAccount{{.*}}withdraw{{.*}}(%class.BankAccount* {{.*}}, i32 50)
_ = account.withdraw(50)

// CHECK: call i32 @{{.*}}BankAccount{{.*}}getBalance{{.*}}(%class.BankAccount* {{.*}})
_ = account.getBalance()

// CHECK: call i32 @{{.*}}BankAccount{{.*}}numberOfOpenAccounts{{.*}}()
_ = BankAccount.numberOfOpenAccounts()
