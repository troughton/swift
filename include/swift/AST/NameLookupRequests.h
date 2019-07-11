//===--- NameLookupRequests.h - Name Lookup Requests ------------*- C++ -*-===//
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
//  This file defines name-lookup requests.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_NAME_LOOKUP_REQUESTS_H
#define SWIFT_NAME_LOOKUP_REQUESTS_H

#include "swift/AST/SimpleRequest.h"
#include "swift/AST/ASTTypeIDs.h"
#include "swift/Basic/Statistic.h"
#include "llvm/ADT/TinyPtrVector.h"

namespace swift {

class ClassDecl;
class TypeAliasDecl;
class TypeDecl;

/// Display a nominal type or extension thereof.
void simple_display(
       llvm::raw_ostream &out,
       const llvm::PointerUnion<TypeDecl *, ExtensionDecl *> &value);

/// Describes a set of type declarations that are "direct" referenced by
/// a particular type in the AST.
using DirectlyReferencedTypeDecls = llvm::TinyPtrVector<TypeDecl *>;

/// Request the set of declarations directly referenced by the an "inherited"
/// type of a type or extension declaration.
///
/// This request retrieves the set of declarations that are directly referenced
/// by a particular type in the "inherited" clause of a type or extension
/// declaration. For example:
///
/// \code
///   protocol P { }
///   protocol Q { }
///   typealias Alias = P & Q
///   class C { }
///
///   class D: C, Alias { }
/// \endcode
///
/// The inherited declaration of \c D at index 0 is the class declaration C.
/// The inherited declaration of \c D at index 1 is the typealias Alias.
class InheritedDeclsReferencedRequest :
  public SimpleRequest<InheritedDeclsReferencedRequest,
                       DirectlyReferencedTypeDecls(
                         llvm::PointerUnion<TypeDecl *, ExtensionDecl *>,
                         unsigned),
                       CacheKind::Uncached> // FIXME: Cache these
{
  /// Retrieve the TypeLoc for this inherited type.
  TypeLoc &getTypeLoc(llvm::PointerUnion<TypeDecl *, ExtensionDecl *> decl,
                      unsigned index) const;

public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  DirectlyReferencedTypeDecls evaluate(
      Evaluator &evaluator,
      llvm::PointerUnion<TypeDecl *, ExtensionDecl *> decl,
      unsigned index) const;

public:
  // Caching
  bool isCached() const { return true; }

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the set of declarations directly referenced by the underlying
/// type of a typealias.
///
/// This request retrieves the set of type declarations that directly referenced
/// by the underlying type of a typealias. For example:
///
/// \code
///   protocol P { }
///   protocol Q { }
///   class C { }
///   typealias PQ = P & Q
///   typealias Alias = C & PQ
/// \endcode
///
/// The set of declarations referenced by the underlying type of \c PQ
/// contains both \c P and \c Q.
/// The set of declarations referenced by the underlying type of \c Alias
/// contains \c C and \c PQ. Clients can choose to look further into \c PQ
/// using another instance of this request.
class UnderlyingTypeDeclsReferencedRequest :
  public SimpleRequest<UnderlyingTypeDeclsReferencedRequest,
                       DirectlyReferencedTypeDecls(TypeAliasDecl *),
                       CacheKind::Uncached> // FIXME: Cache these
{
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  DirectlyReferencedTypeDecls evaluate(
      Evaluator &evaluator,
      TypeAliasDecl *typealias) const;

public:
  // Caching
  bool isCached() const { return true; }

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the superclass declaration for the given class.
class SuperclassDeclRequest :
    public SimpleRequest<SuperclassDeclRequest,
                         ClassDecl *(NominalTypeDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<ClassDecl *>
  evaluate(Evaluator &evaluator, NominalTypeDecl *subject) const;

public:
  // Caching
  bool isCached() const { return true; }
  Optional<ClassDecl *> getCachedResult() const;
  void cacheResult(ClassDecl *value) const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the nominal declaration extended by a given extension declaration.
class ExtendedNominalRequest :
    public SimpleRequest<ExtendedNominalRequest,
                         NominalTypeDecl *(ExtensionDecl *),
                         CacheKind::SeparatelyCached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<NominalTypeDecl *>
  evaluate(Evaluator &evaluator, ExtensionDecl *ext) const;

public:
  // Separate caching.
  bool isCached() const { return true; }
  Optional<NominalTypeDecl *> getCachedResult() const;
  void cacheResult(NominalTypeDecl *value) const;

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

struct SelfBounds {
  llvm::TinyPtrVector<NominalTypeDecl *> decls;
  bool anyObject = false;
};

/// Request the nominal types that occur as the right-hand side of "Self: Foo"
/// constraints in the "where" clause of a protocol extension.
class SelfBoundsFromWhereClauseRequest :
    public SimpleRequest<SelfBoundsFromWhereClauseRequest,
                         SelfBounds(llvm::PointerUnion<TypeDecl *,
                                    ExtensionDecl *>),
                         CacheKind::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  SelfBounds evaluate(Evaluator &evaluator,
                      llvm::PointerUnion<TypeDecl *, ExtensionDecl *>) const;

public:
  // Cycle handling
  SelfBounds breakCycle() const { return { }; }
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};


/// Request all type aliases and nominal types that appear in the "where"
/// clause of an extension.
class TypeDeclsFromWhereClauseRequest :
    public SimpleRequest<TypeDeclsFromWhereClauseRequest,
                         DirectlyReferencedTypeDecls(ExtensionDecl *),
                         CacheKind::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  DirectlyReferencedTypeDecls evaluate(Evaluator &evaluator,
                                       ExtensionDecl *ext) const;

public:
  // Cycle handling
  DirectlyReferencedTypeDecls breakCycle() const { return { }; }
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// Request the nominal type declaration to which the given custom attribute
/// refers.
class CustomAttrNominalRequest :
    public SimpleRequest<CustomAttrNominalRequest,
                         NominalTypeDecl *(CustomAttr *, DeclContext *),
                         CacheKind::Cached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  llvm::Expected<NominalTypeDecl *>
  evaluate(Evaluator &evaluator, CustomAttr *attr, DeclContext *dc) const;

public:
  // Caching
  bool isCached() const { return true; }

  // Cycle handling
  void diagnoseCycle(DiagnosticEngine &diags) const;
  void noteCycleStep(DiagnosticEngine &diags) const;
};

/// The zone number for name-lookup requests.
#define SWIFT_NAME_LOOKUP_REQUESTS_TYPEID_ZONE 9

#define SWIFT_TYPEID_ZONE SWIFT_NAME_LOOKUP_REQUESTS_TYPEID_ZONE
#define SWIFT_TYPEID_HEADER "swift/AST/NameLookupTypeIDZone.def"
#include "swift/Basic/DefineTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER

// Set up reporting of evaluated requests.
template<typename Request>
void reportEvaluatedRequest(UnifiedStatsReporter &stats,
                            const Request &request);

#define SWIFT_TYPEID(RequestType)                                \
template<>                                                       \
inline void reportEvaluatedRequest(UnifiedStatsReporter &stats,  \
                            const RequestType &request) {        \
  ++stats.getFrontendCounters().RequestType;                     \
}
#include "swift/AST/NameLookupTypeIDZone.def"
#undef SWIFT_TYPEID

} // end namespace swift

#endif // SWIFT_NAME_LOOKUP_REQUESTS
