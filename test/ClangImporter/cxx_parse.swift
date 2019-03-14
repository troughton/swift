// RUN: %target-swift-frontend -enable-cxx-interop -typecheck %s -I %S/Inputs/custom-modules/CXXModule -verify

import CXXModule

// The type is imported as a struct, so we need to use `var` to call non-const
// methods.
// TODO: Support constructors instead of just zero-initializing.
var account = BankAccount()
account.deposit(100)
let _: Bool = account.withdraw(50)

// Const methods are fine too.
let _: Int32 = account.getBalance()

// But with `let`, we can only call const methods.
let constAccount = BankAccount()  // expected-note 2 {{change 'let' to 'var'}}
constAccount.deposit(100)  // expected-error{{cannot use mutating member on immutable value}}
let _: Bool = constAccount.withdraw(50)  // expected-error{{cannot use mutating member on immutable value}}

// Const method should still be fine.
let _: Int32 = constAccount.getBalance()

// Static methods get imported too.
let _: Int32 = BankAccount.numberOfOpenAccounts()
