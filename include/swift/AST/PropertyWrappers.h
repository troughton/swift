//===--- PropertyWrappers.h - Property Wrapper ASTs -----------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines helper types for property wrappers.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_PROPERTY_WRAPPERS_H
#define SWIFT_AST_PROPERTY_WRAPPERS_H

namespace llvm {
  class raw_ostream;
}

namespace swift {

class ConstructorDecl;
class CustomAttr;
class Expr;
class VarDecl;
class OpaqueValueExpr;

/// Describes a property wrapper type.
struct PropertyWrapperTypeInfo {
  /// The property through which access that uses this wrapper type is
  /// directed.
  VarDecl *valueVar = nullptr;

  /// The initializer init(wrappedValue:) that will be called when the
  /// initializing the property wrapper type from a value of the property type.
  ///
  /// This initializer is optional, but if present will be used for the `=`
  /// initialization syntax.
  ConstructorDecl *wrappedValueInit = nullptr;

  /// The initializer `init()` that will be called to default-initialize a
  /// value with an attached property wrapper.
  ConstructorDecl *defaultInit = nullptr;

  /// The property through which the projection value ($foo) will be accessed.
  ///
  /// This property is optional. If present, a computed property for `$foo`
  /// will be created that redirects to this property.
  VarDecl *projectedValueVar = nullptr;

  /// The static subscript through which the access of instance properties
  /// of classes can be directed (instead of wrappedValue), providing the
  /// ability to reason about the enclosing "self".
  SubscriptDecl *enclosingInstanceWrappedSubscript = nullptr;

  /// The static subscript through which the access of instance properties
  /// of classes can be directed (instead of projectedValue), providing the
  /// ability to reason about the enclosing "self".
  SubscriptDecl *enclosingInstanceProjectedSubscript = nullptr;

  ///
  /// Whether this is a valid property wrapper.
  bool isValid() const {
    return valueVar != nullptr;
  }

  explicit operator bool() const { return isValid(); }

  friend bool operator==(const PropertyWrapperTypeInfo &lhs,
                         const PropertyWrapperTypeInfo &rhs) {
    return lhs.valueVar == rhs.valueVar &&
        lhs.wrappedValueInit == rhs.wrappedValueInit;
  }
};

/// Describes the backing property of a property that has an attached wrapper.
struct PropertyWrapperBackingPropertyInfo {
  /// The backing property.
  VarDecl *backingVar = nullptr;

  /// The storage wrapper property, if any. When present, this takes the name
  /// '$foo' from `backingVar`.
  VarDecl *storageWrapperVar = nullptr;

  /// When the original default value is specified in terms of an '='
  /// initializer on the initial property, e.g.,
  ///
  /// \code
  /// @Lazy var i = 17
  /// \end
  ///
  /// This is the specified initial value (\c 17), which is suitable for
  /// embedding in the expression \c initializeFromOriginal.
  Expr *originalInitialValue = nullptr;

  /// An expression that initializes the backing property from a value of
  /// the original property's type (e.g., via `init(wrappedValue:)`), or
  /// \c NULL if the backing property can only be initialized directly.
  Expr *initializeFromOriginal = nullptr;

  /// When \c initializeFromOriginal is non-NULL, the opaque value that
  /// is used as a stand-in for a value of the original property's type.
  OpaqueValueExpr *underlyingValue = nullptr;

  PropertyWrapperBackingPropertyInfo() { }
  
  PropertyWrapperBackingPropertyInfo(VarDecl *backingVar,
                                      VarDecl *storageWrapperVar,
                                      Expr *originalInitialValue,
                                      Expr *initializeFromOriginal,
                                      OpaqueValueExpr *underlyingValue)
    : backingVar(backingVar), storageWrapperVar(storageWrapperVar),
      originalInitialValue(originalInitialValue),
      initializeFromOriginal(initializeFromOriginal),
      underlyingValue(underlyingValue) { }

  /// Whether this is a valid property wrapper.
  bool isValid() const {
    return backingVar != nullptr;
  }

  explicit operator bool() const { return isValid(); }

  friend bool operator==(const PropertyWrapperBackingPropertyInfo &lhs,
                         const PropertyWrapperBackingPropertyInfo &rhs) {
    // FIXME: Can't currently compare expressions.
    return lhs.backingVar == rhs.backingVar;
  }
};

void simple_display(
    llvm::raw_ostream &out,
    const PropertyWrapperTypeInfo &propertyWrapper);

void simple_display(
    llvm::raw_ostream &out,
    const PropertyWrapperBackingPropertyInfo &backingInfo);

/// Given the initializer for the given property with an attached property
/// wrapper, dig out the original initialization expression.
Expr *findOriginalPropertyWrapperInitialValue(VarDecl *var, Expr *init);

} // end namespace swift

#endif // SWIFT_AST_PROPERTY_WRAPPERS_H
