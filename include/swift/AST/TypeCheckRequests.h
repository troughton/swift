//===--- TypeCheckRequests.h - Type Checking Requests -----------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file defines type checking requests.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_TYPE_CHECK_REQUESTS_H
#define SWIFT_TYPE_CHECK_REQUESTS_H

#include "swift/AST/ASTTypeIDs.h"
#include "swift/AST/Type.h"
#include "swift/AST/Evaluator.h"
#include "swift/AST/SimpleRequest.h"
#include "swift/AST/TypeResolutionStage.h"
#include "swift/Basic/AnyValue.h"
#include "swift/Basic/Statistic.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TinyPtrVector.h"

namespace swift {

class GenericParamList;
struct PropertyWrapperBackingPropertyInfo;
class RequirementRepr;
class SpecializeAttr;
class TypeAliasDecl;
struct TypeLoc;
class ValueDecl;
class AbstractStorageDecl;
enum class OpaqueReadOwnership: uint8_t;

/// Display a nominal type or extension thereof.
void simple_display(
       llvm::raw_ostream &out,
       const llvm::PointerUnion<TypeDecl *, ExtensionDecl *> &value);

/// Request the type from the ith entry in the inheritance clause for the
/// given declaration.
class InheritedTypeRequest :
    public SimpleRequest<InheritedTypeRequest,
                         Type(llvm::PointerUnion<TypeDecl *, ExtensionDecl *>,
                              unsigned,
                              TypeResolutionStage),
                         CacheKind::SeparatelyCached>
{
  /// Retrieve the TypeLoc for this inherited type.
  TypeLoc &getTypeLoc(llvm::PointerUnion<TypeDecl *, ExtensionDecl *> decl,
                      unsigned index) const;

public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type>
  evaluate(Evaluator &evaluator,
           llvm::PointerUnion<TypeDecl *, ExtensionDecl *> decl,
           unsigned index,
           TypeResolutionStage stage) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Caching
  bool isCached() const;
  Optional<Type> getCachedResult() const;
  void cacheResult(Type value) const;
};

/// Request the superclass type for the given class.
class SuperclassTypeRequest :
    public SimpleRequest<SuperclassTypeRequest,
                         Type(NominalTypeDecl *, TypeResolutionStage),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type>
  evaluate(Evaluator &evaluator, NominalTypeDecl *classDecl,
           TypeResolutionStage stage) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const;
  Optional<Type> getCachedResult() const;
  void cacheResult(Type value) const;
};

/// Request the raw type of the given enum.
class EnumRawTypeRequest :
    public SimpleRequest<EnumRawTypeRequest,
                         Type(EnumDecl *, TypeResolutionStage),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type>
  evaluate(Evaluator &evaluator, EnumDecl *enumDecl,
           TypeResolutionStage stage) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const;
  Optional<Type> getCachedResult() const;
  void cacheResult(Type value) const;
};

/// Request to determine the set of declarations that were are overridden
/// by the given declaration.
class OverriddenDeclsRequest
  : public SimpleRequest<OverriddenDeclsRequest,
                         llvm::TinyPtrVector<ValueDecl *>(ValueDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<llvm::TinyPtrVector<ValueDecl *>>
  evaluate(Evaluator &evaluator, ValueDecl *decl) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<llvm::TinyPtrVector<ValueDecl *>> getCachedResult() const;
  void cacheResult(llvm::TinyPtrVector<ValueDecl *> value) const;
};

/// Determine whether the given declaration is exposed to Objective-C.
class IsObjCRequest :
    public SimpleRequest<IsObjCRequest,
                         bool(ValueDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<bool> evaluate(Evaluator &evaluator, ValueDecl *decl) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<bool> getCachedResult() const;
  void cacheResult(bool value) const;
};

/// Determine whether the given declaration is 'final'.
class IsFinalRequest :
    public SimpleRequest<IsFinalRequest,
                         bool(ValueDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<bool> evaluate(Evaluator &evaluator, ValueDecl *decl) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<bool> getCachedResult() const;
  void cacheResult(bool value) const;
};

/// Determine whether the given declaration is 'dynamic''.
class IsDynamicRequest :
    public SimpleRequest<IsDynamicRequest,
                         bool(ValueDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<bool> evaluate(Evaluator &evaluator, ValueDecl *decl) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<bool> getCachedResult() const;
  void cacheResult(bool value) const;
};

/// Compute the requirements that describe a protocol.
class RequirementSignatureRequest :
    public SimpleRequest<RequirementSignatureRequest,
                         ArrayRef<Requirement>(ProtocolDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<ArrayRef<Requirement>> evaluate(Evaluator &evaluator, ProtocolDecl *proto) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<ArrayRef<Requirement>> getCachedResult() const;
  void cacheResult(ArrayRef<Requirement> value) const;
};

/// Compute the default definition type of an associated type.
class DefaultDefinitionTypeRequest :
    public SimpleRequest<DefaultDefinitionTypeRequest,
                         Type(AssociatedTypeDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type> evaluate(Evaluator &evaluator, AssociatedTypeDecl *decl) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Caching.
  bool isCached() const { return true; }
};

/// Describes the owner of a where clause, from which we can extract
/// requirements.
struct WhereClauseOwner {
  /// The declaration context in which the where clause will be evaluated.
  DeclContext *dc;

  /// The source of the where clause, which can be a generic parameter list
  /// or a declaration that can have a where clause.
  llvm::PointerUnion3<GenericParamList *, Decl *, SpecializeAttr *> source;

  WhereClauseOwner(Decl *decl);

  WhereClauseOwner(DeclContext *dc, GenericParamList *genericParams)
    : dc(dc), source(genericParams) { }

  WhereClauseOwner(DeclContext *dc, SpecializeAttr *attr)
    : dc(dc), source(attr) { }

  SourceLoc getLoc() const;

  friend hash_code hash_value(const WhereClauseOwner &owner) {
    return hash_combine(hash_value(owner.dc),
                        hash_value(owner.source.getOpaqueValue()));
  }

  friend bool operator==(const WhereClauseOwner &lhs,
                         const WhereClauseOwner &rhs) {
    return lhs.dc == rhs.dc &&
           lhs.source.getOpaqueValue() == rhs.source.getOpaqueValue();
  }

  friend bool operator!=(const WhereClauseOwner &lhs,
                         const WhereClauseOwner &rhs) {
    return !(lhs == rhs);
  }
};

void simple_display(llvm::raw_ostream &out, const WhereClauseOwner &owner);

/// Retrieve a requirement from the where clause of the given declaration.
class RequirementRequest :
    public SimpleRequest<RequirementRequest,
                         Requirement(WhereClauseOwner, unsigned,
                                     TypeResolutionStage),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

  /// Retrieve the array of requirements from the given owner.
  static MutableArrayRef<RequirementRepr> getRequirements(WhereClauseOwner);

  /// Visit each of the requirements in the given owner,
  ///
  /// \returns true after short-circuiting if the callback returned \c true
  /// for any of the requirements.
  static bool visitRequirements(
      WhereClauseOwner, TypeResolutionStage stage,
      llvm::function_ref<bool(Requirement, RequirementRepr*)> callback);

private:
  friend SimpleRequest;

  /// Retrieve the requirement this request operates on.
  RequirementRepr &getRequirement() const;

  // Evaluation.
  llvm::Expected<Requirement> evaluate(Evaluator &evaluator,
                                       WhereClauseOwner,
                                       unsigned index,
                                       TypeResolutionStage stage) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const;
  Optional<Requirement> getCachedResult() const;
  void cacheResult(Requirement value) const;
};

/// Generate the USR for the given declaration.
class USRGenerationRequest :
    public SimpleRequest<USRGenerationRequest,
                         std::string(const ValueDecl*),
                         CacheKind::Cached>
{
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<std::string> evaluate(Evaluator &eval, const ValueDecl *d) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Caching
  bool isCached() const { return true; }
};

/// Generate the mangling for the given local type declaration.
class MangleLocalTypeDeclRequest :
    public SimpleRequest<MangleLocalTypeDeclRequest,
                         std::string(const TypeDecl*),
                         CacheKind::Cached>
{
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<std::string> evaluate(Evaluator &eval, const TypeDecl *d) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Caching
  bool isCached() const { return true; }
};

void simple_display(llvm::raw_ostream &out, const KnownProtocolKind);
class TypeChecker;

// Find the type in the cache or look it up
class DefaultTypeRequest
    : public SimpleRequest<DefaultTypeRequest,
                           Type(KnownProtocolKind, const DeclContext *),
                           CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type> evaluate(Evaluator &eval, KnownProtocolKind,
                                const DeclContext *) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Caching
  bool isCached() const { return true; }
  Optional<Type> getCachedResult() const;
  void cacheResult(Type value) const;

private:
  KnownProtocolKind getKnownProtocolKind() const {
    return std::get<0>(getStorage());
  }
  const DeclContext *getDeclContext() const {
    return std::get<1>(getStorage());
  }

  static const char *getTypeName(KnownProtocolKind);
  static bool getPerformLocalLookup(KnownProtocolKind);
  TypeChecker &getTypeChecker() const;
  SourceFile *getSourceFile() const;
  Type &getCache() const;
};

/// Retrieve information about a property wrapper type.
class PropertyWrapperTypeInfoRequest
  : public SimpleRequest<PropertyWrapperTypeInfoRequest,
                         PropertyWrapperTypeInfo(NominalTypeDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<PropertyWrapperTypeInfo>
      evaluate(Evaluator &eval, NominalTypeDecl *nominal) const;

public:
  // Caching
  bool isCached() const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the nominal type declaration to which the given custom attribute
/// refers.
class AttachedPropertyWrappersRequest :
    public SimpleRequest<AttachedPropertyWrappersRequest,
                         llvm::TinyPtrVector<CustomAttr *>(VarDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<llvm::TinyPtrVector<CustomAttr *>>
  evaluate(Evaluator &evaluator, VarDecl *) const;

public:
  // Caching
  bool isCached() const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the raw (possibly unbound generic) type of the property wrapper
/// that is attached to the given variable.
class AttachedPropertyWrapperTypeRequest :
    public SimpleRequest<AttachedPropertyWrapperTypeRequest,
                         Type(VarDecl *, unsigned),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type>
  evaluate(Evaluator &evaluator, VarDecl *var, unsigned i) const;

public:
  // Caching
  bool isCached() const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the nominal type declaration to which the given custom attribute
/// refers.
class PropertyWrapperBackingPropertyTypeRequest :
    public SimpleRequest<PropertyWrapperBackingPropertyTypeRequest,
                         Type(VarDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type>
  evaluate(Evaluator &evaluator, VarDecl *var) const;

public:
  // Caching
  bool isCached() const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request information about the backing property for properties that have
/// attached property wrappers.
class PropertyWrapperBackingPropertyInfoRequest :
    public SimpleRequest<PropertyWrapperBackingPropertyInfoRequest,
                         PropertyWrapperBackingPropertyInfo(VarDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<PropertyWrapperBackingPropertyInfo>
  evaluate(Evaluator &evaluator, VarDecl *var) const;

public:
  // Caching
  bool isCached() const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Retrieve the structural type of an alias type.
class StructuralTypeRequest :
    public SimpleRequest<StructuralTypeRequest,
                         Type(TypeAliasDecl*),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<Type> evaluate(Evaluator &eval, TypeAliasDecl *d) const;

public:
  // Cycle handling.
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Caching.
  bool isCached() const { return true; }
};

/// Request the custom attribute which attaches a function builder to the
/// given declaration.
class AttachedFunctionBuilderRequest :
    public SimpleRequest<AttachedFunctionBuilderRequest,
                         CustomAttr *(ValueDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<CustomAttr *>
  evaluate(Evaluator &evaluator, ValueDecl *decl) const;

public:
  // Caching
  bool isCached() const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the function builder type attached to the given declaration,
/// if any.
class FunctionBuilderTypeRequest :
    public SimpleRequest<FunctionBuilderTypeRequest,
                         Type(ValueDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  llvm::Expected<Type>
  evaluate(Evaluator &evaluator, ValueDecl *decl) const;

public:
  // Caching
  bool isCached() const { return true; }

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request a function's self access kind.
class SelfAccessKindRequest :
    public SimpleRequest<SelfAccessKindRequest,
                         SelfAccessKind(FuncDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<SelfAccessKind>
  evaluate(Evaluator &evaluator, FuncDecl *func) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<SelfAccessKind> getCachedResult() const;
  void cacheResult(SelfAccessKind value) const;
};

/// Request whether the storage has a mutating getter.
class IsGetterMutatingRequest :
    public SimpleRequest<IsGetterMutatingRequest,
                         bool(AbstractStorageDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<bool>
  evaluate(Evaluator &evaluator, AbstractStorageDecl *func) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<bool> getCachedResult() const;
  void cacheResult(bool value) const;
};

/// Request whether the storage has a mutating getter.
class IsSetterMutatingRequest :
    public SimpleRequest<IsSetterMutatingRequest,
                         bool(AbstractStorageDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<bool>
  evaluate(Evaluator &evaluator, AbstractStorageDecl *func) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<bool> getCachedResult() const;
  void cacheResult(bool value) const;
};

/// Request whether reading the storage yields a borrowed value.
class OpaqueReadOwnershipRequest :
    public SimpleRequest<OpaqueReadOwnershipRequest,
                         OpaqueReadOwnership(AbstractStorageDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<OpaqueReadOwnership>
  evaluate(Evaluator &evaluator, AbstractStorageDecl *storage) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  // Separate caching.
  bool isCached() const { return true; }
  Optional<OpaqueReadOwnership> getCachedResult() const;
  void cacheResult(OpaqueReadOwnership value) const;
};

/// Request to build the underlying storage for a lazy property.
class LazyStoragePropertyRequest :
    public SimpleRequest<LazyStoragePropertyRequest,
                         VarDecl *(VarDecl *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<VarDecl *>
  evaluate(Evaluator &evaluator, VarDecl *lazyVar) const;

public:
  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;

  bool isCached() const { return true; }
};

// Allow AnyValue to compare two Type values, even though Type doesn't
// support ==.
template<>
inline bool AnyValue::Holder<Type>::equals(const HolderBase &other) const {
  assert(typeID == other.typeID && "Caller should match type IDs");
  return value.getPointer() ==
      static_cast<const Holder<Type> &>(other).value.getPointer();
}

void simple_display(llvm::raw_ostream &out, Type value);

/// The zone number for the type checker.
#define SWIFT_TYPE_CHECKER_REQUESTS_TYPEID_ZONE 10

#define SWIFT_TYPEID_ZONE SWIFT_TYPE_CHECKER_REQUESTS_TYPEID_ZONE
#define SWIFT_TYPEID_HEADER "swift/AST/TypeCheckerTypeIDZone.def"
#include "swift/Basic/DefineTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER

// Set up reporting of evaluated requests.
#define SWIFT_TYPEID(RequestType)                                \
template<>                                                       \
inline void reportEvaluatedRequest(UnifiedStatsReporter &stats,  \
                            const RequestType &request) {        \
  ++stats.getFrontendCounters().RequestType;                     \
}
#include "swift/AST/TypeCheckerTypeIDZone.def"
#undef SWIFT_TYPEID

} // end namespace swift

#endif // SWIFT_TYPE_CHECK_REQUESTS_H
