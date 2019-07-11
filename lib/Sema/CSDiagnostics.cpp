//===--- CSDiagnostics.cpp - Constraint Diagnostics -----------------------===//
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
// This file implements diagnostics for constraint system.
//
//===----------------------------------------------------------------------===//

#include "CSDiagnostics.h"
#include "ConstraintSystem.h"
#include "MiscDiagnostics.h"
#include "TypoCorrection.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/ProtocolConformanceRef.h"
#include "swift/AST/Stmt.h"
#include "swift/AST/Types.h"
#include "swift/Basic/SourceLoc.h"
#include "swift/Parse/Lexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"

using namespace swift;
using namespace constraints;

FailureDiagnostic::~FailureDiagnostic() {}

bool FailureDiagnostic::diagnose(bool asNote) {
  return asNote ? diagnoseAsNote() : diagnoseAsError();
}

bool FailureDiagnostic::diagnoseAsNote() {
  return false;
}

std::pair<Expr *, bool> FailureDiagnostic::computeAnchor() const {
  auto &cs = getConstraintSystem();

  auto *locator = getLocator();
  // Resolve the locator to a specific expression.
  SourceRange range;
  bool isSubscriptMember =
      (!locator->getPath().empty() && locator->getPath().back().getKind() ==
                                          ConstraintLocator::SubscriptMember);

  ConstraintLocator *resolved = simplifyLocator(cs, locator, range);
  if (!resolved || !resolved->getAnchor())
    return {locator->getAnchor(), true};

  Expr *anchor = resolved->getAnchor();
  // FIXME: Work around an odd locator representation that doesn't separate the
  // base of a subscript member from the member access.
  if (isSubscriptMember) {
    if (auto subscript = dyn_cast<SubscriptExpr>(anchor))
      anchor = subscript->getBase();
  }

  return {anchor, !resolved->getPath().empty()};
}

Type FailureDiagnostic::getType(Expr *expr) const {
  return resolveType(CS.getType(expr));
}

Type FailureDiagnostic::getType(const TypeLoc &loc) const {
  return resolveType(CS.getType(loc));
}

template <typename... ArgTypes>
InFlightDiagnostic
FailureDiagnostic::emitDiagnostic(ArgTypes &&... Args) const {
  auto &cs = getConstraintSystem();
  return cs.TC.diagnose(std::forward<ArgTypes>(Args)...);
}

Expr *FailureDiagnostic::findParentExpr(Expr *subExpr) const {
  return E ? E->getParentMap()[subExpr] : nullptr;
}

Expr *FailureDiagnostic::getArgumentExprFor(Expr *anchor) const {
  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(anchor)) {
    if (auto *call = dyn_cast_or_null<CallExpr>(findParentExpr(UDE)))
      return call->getArg();
  } else if (auto *UME = dyn_cast<UnresolvedMemberExpr>(anchor)) {
    return UME->getArgument();
  } else if (auto *call = dyn_cast<CallExpr>(anchor)) {
    return call->getArg();
  } else if (auto *SE = dyn_cast<SubscriptExpr>(anchor)) {
    return SE->getIndex();
  }
  return nullptr;
}

Expr *FailureDiagnostic::getBaseExprFor(Expr *anchor) const {
  if (!anchor)
    return nullptr;

  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(anchor))
    return UDE->getBase();
  else if (auto *SE = dyn_cast<SubscriptExpr>(anchor))
    return SE->getBase();
  else if (auto *MRE = dyn_cast<MemberRefExpr>(anchor))
    return MRE->getBase();

  return nullptr;
}

Optional<SelectedOverload> FailureDiagnostic::getChoiceFor(Expr *expr) const {
  auto &cs = getConstraintSystem();
  return getOverloadChoiceIfAvailable(cs.getCalleeLocator(expr));
}

Type FailureDiagnostic::resolveInterfaceType(Type type,
                                             bool reconstituteSugar) const {
  auto &cs = getConstraintSystem();
  auto resolvedType = type.transform([&](Type type) -> Type {
    if (auto *tvt = type->getAs<TypeVariableType>()) {
      // If this type variable is for a generic parameter, return that.
      if (auto *gp = tvt->getImpl().getGenericParameter())
        return gp;

      // Otherwise resolve its fixed type, mapped out of context.
      if (auto fixed = cs.getFixedType(tvt))
        return resolveInterfaceType(fixed->mapTypeOutOfContext());

      return cs.getRepresentative(tvt);
    }
    if (auto *dmt = type->getAs<DependentMemberType>()) {
      // For a dependent member, first resolve the base.
      auto newBase = resolveInterfaceType(dmt->getBase());

      // Then reconstruct using its associated type.
      assert(dmt->getAssocType());
      return DependentMemberType::get(newBase, dmt->getAssocType());
    }
    return type;
  });

  assert(!resolvedType->hasArchetype());
  return reconstituteSugar ? resolvedType->reconstituteSugar(/*recursive*/ true)
                           : resolvedType;
}

/// Given an apply expr, returns true if it is expected to have a direct callee
/// overload, resolvable using `getChoiceFor`. Otherwise, returns false.
static bool shouldHaveDirectCalleeOverload(const ApplyExpr *apply) {
  auto *fnExpr = apply->getFn()->getValueProvidingExpr();

  // An apply of an apply/subscript doesn't have a direct callee.
  if (isa<ApplyExpr>(fnExpr) || isa<SubscriptExpr>(fnExpr))
    return false;

  // Applies of closures don't have callee overloads.
  if (isa<ClosureExpr>(fnExpr))
    return false;

  // If the optionality changes, there's no direct callee.
  if (isa<BindOptionalExpr>(fnExpr) || isa<ForceValueExpr>(fnExpr) ||
      isa<OptionalTryExpr>(fnExpr)) {
    return false;
  }

  // If we have an intermediate cast, there's no direct callee.
  if (isa<ExplicitCastExpr>(fnExpr))
    return false;

  // No direct callee for an if expr.
  if (isa<IfExpr>(fnExpr))
    return false;

  // Assume that anything else would have a direct callee.
  return true;
}

Optional<FunctionArgApplyInfo>
FailureDiagnostic::getFunctionArgApplyInfo(ConstraintLocator *locator) const {
  auto &cs = getConstraintSystem();
  auto *anchor = locator->getAnchor();
  auto path = locator->getPath();

  // Look for the apply-arg-to-param element in the locator's path. We may
  // have to look through other elements that are generated from an argument
  // conversion such as GenericArgument for an optional-to-optional conversion,
  // and OptionalPayload for a value-to-optional conversion.
  auto applyArgElt =
      std::find_if(path.rbegin(), path.rend(), [](LocatorPathElt elt) {
        return elt.getKind() == ConstraintLocator::ApplyArgToParam;
      });

  if (applyArgElt == path.rend())
    return None;

  assert(std::find_if(applyArgElt + 1, path.rend(), [](LocatorPathElt elt) {
    return elt.getKind() == ConstraintLocator::ApplyArgToParam;
  }) == path.rend() && "Multiple ApplyArgToParam components?");

  // Form a new locator that ends at the apply-arg-to-param element, and
  // simplify it to get the full argument expression.
  auto argPath = path.drop_back(applyArgElt - path.rbegin());
  auto *argLocator = cs.getConstraintLocator(
      anchor, argPath, ConstraintLocator::getSummaryFlagsForPath(argPath));

  auto *argExpr = simplifyLocatorToAnchor(cs, argLocator);

  // If we were unable to simplify down to the argument expression, we don't
  // know what this is.
  if (!argExpr)
    return None;

  ValueDecl *callee = nullptr;
  Type rawFnType;
  if (auto overload = getChoiceFor(anchor)) {
    // If we have resolved an overload for the callee, then use that to get the
    // function type and callee.
    if (auto *decl = overload->choice.getDeclOrNull())
      callee = decl;

    rawFnType = overload->openedType;
  } else {
    // If we didn't resolve an overload for the callee, we must be dealing with
    // a call of an arbitrary function expr.
    auto *call = cast<CallExpr>(anchor);
    assert(!shouldHaveDirectCalleeOverload(call) &&
           "Should we have resolved a callee for this?");
    rawFnType = cs.getType(call->getFn())->getRValueType();
  }

  auto *fnType = resolveType(rawFnType)->getAs<FunctionType>();
  if (!fnType)
    return None;

  // Resolve the interface type for the function. Note that this may not be a
  // function type, for example it could be a generic parameter.
  Type fnInterfaceType;
  if (callee && callee->hasInterfaceType()) {
    // If we have a callee with an interface type, we can use it. This is
    // preferable to resolveInterfaceType, as this will allow us to get a
    // GenericFunctionType for generic decls.
    //
    // Note that it's possible to find a callee without an interface type. This
    // can happen for example with closure parameters, where the interface type
    // isn't set until the solution is applied. In that case, use
    // resolveInterfaceType.
    fnInterfaceType = callee->getInterfaceType();

    // Strip off the curried self parameter if necessary.
    if (callee->hasCurriedSelf())
      fnInterfaceType = fnInterfaceType->castTo<AnyFunctionType>()->getResult();

    if (auto *fn = fnInterfaceType->getAs<AnyFunctionType>()) {
      assert(fn->getNumParams() == fnType->getNumParams() &&
             "Parameter mismatch?");
      (void)fn;
    }
  } else {
    fnInterfaceType = resolveInterfaceType(rawFnType);
  }

  auto argIdx = applyArgElt->getValue();
  auto paramIdx = applyArgElt->getValue2();

  return FunctionArgApplyInfo(argExpr, argIdx, getType(argExpr), paramIdx,
                              fnInterfaceType, fnType, callee);
}

Type RequirementFailure::getOwnerType() const {
  auto *anchor = getRawAnchor();

  // If diagnostic is anchored at assignment expression
  // it means that requirement failure happend while trying
  // to convert source to destination, which means that
  // owner type is actually not an assignment expression
  // itself but its source.
  if (auto *assignment = dyn_cast<AssignExpr>(anchor))
    anchor = assignment->getSrc();

  return getType(anchor)->getInOutObjectType()->getMetatypeInstanceType();
}

const GenericContext *RequirementFailure::getGenericContext() const {
  if (auto *genericCtx = AffectedDecl->getAsGenericContext())
    return genericCtx;
  return AffectedDecl->getDeclContext()->getAsDecl()->getAsGenericContext();
}

const Requirement &RequirementFailure::getRequirement() const {
  // If this is a conditional requirement failure we need to
  // fetch conformance from constraint system associated with
  // type requirement this conditional conformance belongs to.
  auto requirements = isConditional()
                          ? Conformance->getConditionalRequirements()
                          : Signature->getRequirements();
  return requirements[getRequirementIndex()];
}

ProtocolConformance *RequirementFailure::getConformanceForConditionalReq(
    ConstraintLocator *locator) {
  auto &cs = getConstraintSystem();
  auto path = locator->getPath();
  assert(!path.empty());

  if (!path.back().isConditionalRequirement()) {
    assert(path.back().isTypeParameterRequirement());
    return nullptr;
  }

  auto *typeReqLoc = getConstraintLocator(getRawAnchor(), path.drop_back());

  auto result = llvm::find_if(
      cs.CheckedConformances,
      [&](const std::pair<ConstraintLocator *, ProtocolConformanceRef>
              &conformance) { return conformance.first == typeReqLoc; });
  assert(result != cs.CheckedConformances.end());

  auto conformance = result->second;
  assert(conformance.isConcrete());
  return conformance.getConcrete();
}

ValueDecl *RequirementFailure::getDeclRef() const {
  auto &cs = getConstraintSystem();

  // Get a declaration associated with given type (if any).
  // This is used to retrieve affected declaration when
  // failure is in any way contextual, and declaration can't
  // be fetched directly from constraint system.
  auto getAffectedDeclFromType = [](Type type) -> ValueDecl * {
    assert(type);
    // If problem is related to a typealias, let's point this
    // diagnostic directly to its declaration without desugaring.
    if (auto *alias = dyn_cast<TypeAliasType>(type.getPointer()))
      return alias->getDecl();

    if (auto *opaque = type->getAs<OpaqueTypeArchetypeType>())
      return opaque->getDecl();

    return type->getAnyGeneric();
  };

  if (isFromContextualType())
    return getAffectedDeclFromType(cs.getContextualType());

  if (auto overload = getChoiceFor(getRawAnchor()))
    return overload->choice.getDecl();

  return getAffectedDeclFromType(getOwnerType());
}

GenericSignature *RequirementFailure::getSignature(ConstraintLocator *locator) {
  if (isConditional())
    return Conformance->getGenericSignature();

  auto path = locator->getPath();
  for (auto iter = path.rbegin(); iter != path.rend(); ++iter) {
    const auto &elt = *iter;
    if (elt.getKind() == ConstraintLocator::OpenedGeneric)
      return elt.getGenericSignature();
  }

  llvm_unreachable("Type requirement failure should always have signature");
}

bool RequirementFailure::isFromContextualType() const {
  auto path = getLocator()->getPath();
  assert(!path.empty());
  return path.front().getKind() == ConstraintLocator::ContextualType;
}

const DeclContext *RequirementFailure::getRequirementDC() const {
  // In case of conditional requirement failure, we don't
  // have to guess where the it comes from.
  if (isConditional())
    return Conformance->getDeclContext();

  const auto &req = getRequirement();
  auto *DC = AffectedDecl->getDeclContext();

  do {
    if (auto *sig = DC->getGenericSignatureOfContext()) {
      if (sig->isRequirementSatisfied(req))
        return DC;
    }
  } while ((DC = DC->getParent()));

  return AffectedDecl->getAsGenericContext();
}

bool RequirementFailure::isStaticOrInstanceMember(const ValueDecl *decl) {
  if (decl->isInstanceMember())
    return true;

  if (auto *AFD = dyn_cast<AbstractFunctionDecl>(decl))
    return AFD->isStatic() && !AFD->isOperator();

  return decl->isStatic();
}

bool RequirementFailure::diagnoseAsError() {
  if (!canDiagnoseFailure())
    return false;

  auto *anchor = getRawAnchor();
  const auto *reqDC = getRequirementDC();
  auto *genericCtx = getGenericContext();

  auto lhs = resolveType(getLHS());
  auto rhs = resolveType(getRHS());

  if (auto *OTD = dyn_cast<OpaqueTypeDecl>(AffectedDecl)) {
    auto *namingDecl = OTD->getNamingDecl();
    emitDiagnostic(
        anchor->getLoc(), diag::type_does_not_conform_in_opaque_return,
        namingDecl->getDescriptiveKind(), namingDecl->getFullName(), lhs, rhs);

    if (auto *repr = namingDecl->getOpaqueResultTypeRepr()) {
      emitDiagnostic(repr->getLoc(), diag::opaque_return_type_declared_here)
          .highlight(repr->getSourceRange());
    }
    return true;
  }

  if (genericCtx != reqDC && (genericCtx->isChildContextOf(reqDC) ||
                              isStaticOrInstanceMember(AffectedDecl))) {
    auto *NTD = reqDC->getSelfNominalTypeDecl();
    emitDiagnostic(anchor->getLoc(), getDiagnosticInRereference(),
                   AffectedDecl->getDescriptiveKind(),
                   AffectedDecl->getFullName(), NTD->getDeclaredType(), lhs,
                   rhs);
  } else {
    emitDiagnostic(anchor->getLoc(), getDiagnosticOnDecl(),
                   AffectedDecl->getDescriptiveKind(),
                   AffectedDecl->getFullName(), lhs, rhs);
  }

  emitRequirementNote(reqDC->getAsDecl(), lhs, rhs);
  return true;
}

bool RequirementFailure::diagnoseAsNote() {
  const auto &req = getRequirement();
  const auto *reqDC = getRequirementDC();

  emitDiagnostic(reqDC->getAsDecl(), getDiagnosticAsNote(), getLHS(), getRHS(),
                 req.getFirstType(), req.getSecondType(), "");
  return true;
}

void RequirementFailure::emitRequirementNote(const Decl *anchor, Type lhs,
                                             Type rhs) const {
  auto &req = getRequirement();

  if (isConditional()) {
    emitDiagnostic(anchor, diag::requirement_implied_by_conditional_conformance,
                   resolveType(Conformance->getType()),
                   Conformance->getProtocol()->getDeclaredInterfaceType());
    return;
  }

  if (rhs->isEqual(req.getSecondType())) {
    emitDiagnostic(anchor, diag::where_requirement_failure_one_subst,
                   req.getFirstType(), lhs);
    return;
  }

  if (lhs->isEqual(req.getFirstType())) {
    emitDiagnostic(anchor, diag::where_requirement_failure_one_subst,
                   req.getSecondType(), rhs);
    return;
  }

  emitDiagnostic(anchor, diag::where_requirement_failure_both_subst,
                 req.getFirstType(), lhs, req.getSecondType(), rhs);
}

bool MissingConformanceFailure::diagnoseAsError() {
  if (!canDiagnoseFailure())
    return false;

  auto *anchor = getAnchor();
  auto ownerType = getOwnerType();
  auto nonConformingType = getLHS();
  auto protocolType = getRHS();

  auto getArgumentAt = [](const ApplyExpr *AE, unsigned index) -> Expr * {
    assert(AE);

    auto *arg = AE->getArg();
    if (auto *TE = dyn_cast<TupleExpr>(arg))
      return TE->getElement(index);

    assert(index == 0);
    if (auto *PE = dyn_cast<ParenExpr>(arg))
      return PE->getSubExpr();

    return arg;
  };

  Optional<unsigned> atParameterPos;
  // Sometimes fix is recorded by type-checking sub-expression
  // during normal diagnostics, in such case call expression
  // is unavailable.
  if (Apply) {
    if (auto *fnType = ownerType->getAs<AnyFunctionType>()) {
      auto parameters = fnType->getParams();
      for (auto index : indices(parameters)) {
        if (parameters[index].getOldType()->isEqual(nonConformingType)) {
          atParameterPos = index;
          break;
        }
      }
    }
  }

  if (nonConformingType->isExistentialType()) {
    auto diagnostic = diag::protocol_does_not_conform_objc;
    if (nonConformingType->isObjCExistentialType())
      diagnostic = diag::protocol_does_not_conform_static;

    emitDiagnostic(anchor->getLoc(), diagnostic, nonConformingType,
                   protocolType);
    return true;
  }

  if (atParameterPos) {
    // Requirement comes from one of the parameter types,
    // let's try to point diagnostic to the argument expression.
    auto *argExpr = getArgumentAt(Apply, *atParameterPos);
    emitDiagnostic(argExpr->getLoc(),
                   diag::cannot_convert_argument_value_protocol,
                   nonConformingType, protocolType);
    return true;
  }

  // If none of the special cases could be diagnosed,
  // let's fallback to the most general diagnostic.
  return RequirementFailure::diagnoseAsError();
}

Optional<Diag<Type, Type>> GenericArgumentsMismatchFailure::getDiagnosticFor(
    ContextualTypePurpose context) {
  switch (context) {
  case CTP_Initialization:
  case CTP_AssignSource:
    return diag::cannot_convert_assign;
  case CTP_ReturnStmt:
  case CTP_ReturnSingleExpr:
    return diag::cannot_convert_to_return_type;
  case CTP_DefaultParameter:
    return diag::cannot_convert_default_arg_value;
  case CTP_YieldByValue:
    return diag::cannot_convert_yield_value;
  case CTP_CallArgument:
    return diag::cannot_convert_argument_value;
  case CTP_ClosureResult:
    return diag::cannot_convert_closure_result;
  case CTP_ArrayElement:
    return diag::cannot_convert_array_element;
  // TODO(diagnostics): Make dictionary related diagnostics take prescedence
  // over CSDiag. Currently these won't ever be produced.
  case CTP_DictionaryKey:
    return diag::cannot_convert_dict_key;
  case CTP_DictionaryValue:
    return diag::cannot_convert_dict_value;
  case CTP_CoerceOperand:
    return diag::cannot_convert_coerce;
  case CTP_SubscriptAssignSource:
    return diag::cannot_convert_subscript_assign;

  case CTP_ThrowStmt:
  case CTP_Unused:
  case CTP_CannotFail:
  case CTP_YieldByReference:
  case CTP_CalleeResult:
  case CTP_EnumCaseRawValue:
    break;
  }
  return None;
}

void GenericArgumentsMismatchFailure::emitNoteForMismatch(int position) {
  auto genericTypeDecl = getActual()->getCanonicalType()->getAnyGeneric();
  auto param = genericTypeDecl->getGenericParams()->getParams()[position];

  auto lhs = resolveType(getActual()->getGenericArgs()[position])
                 ->reconstituteSugar(/*recursive=*/false);
  auto rhs = resolveType(getRequired()->getGenericArgs()[position])
                 ->reconstituteSugar(/*recursive=*/false);

  auto noteLocation = param->getLoc();

  if (!noteLocation.isValid()) {
    noteLocation = getAnchor()->getLoc();
  }

  emitDiagnostic(noteLocation, diag::generic_argument_mismatch,
                 param->getName(), lhs, rhs);
}

bool GenericArgumentsMismatchFailure::diagnoseAsError() {
  auto *anchor = getAnchor();
  auto path = getLocator()->getPath();

  Optional<Diag<Type, Type>> diagnostic;
  if (path.empty()) {
    assert(isa<AssignExpr>(anchor));
    diagnostic = getDiagnosticFor(CTP_AssignSource);
  } else {
    const auto &last = path.back();
    switch (last.getKind()) {
    case ConstraintLocator::ContextualType: {
      auto purpose = getConstraintSystem().getContextualTypePurpose();
      assert(purpose != CTP_Unused);
      diagnostic = getDiagnosticFor(purpose);
      break;
    }

    case ConstraintLocator::AutoclosureResult:
    case ConstraintLocator::ApplyArgToParam:
    case ConstraintLocator::ApplyArgument: {
      diagnostic = diag::cannot_convert_argument_value;
      break;
    }

    case ConstraintLocator::ParentType: {
      diagnostic = diag::cannot_convert_parent_type;
      break;
    }

    case ConstraintLocator::ClosureResult: {
      diagnostic = diag::cannot_convert_closure_result;
      break;
    }

    default:
      break;
    }
  }

  if (!diagnostic)
    return false;

  emitDiagnostic(
      getAnchor()->getLoc(), *diagnostic,
      resolveType(getActual())->reconstituteSugar(/*recursive=*/false),
      resolveType(getRequired())->reconstituteSugar(/*recursive=*/false));
  emitNotesForMismatches();
  return true;
}

bool LabelingFailure::diagnoseAsError() {
  auto &cs = getConstraintSystem();
  auto *anchor = getRawAnchor();

  auto *argExpr = getArgumentExprFor(anchor);
  if (!argExpr)
    return false;

  return diagnoseArgumentLabelError(cs.getASTContext(), argExpr, CorrectLabels,
                                    isa<SubscriptExpr>(anchor));
}

bool NoEscapeFuncToTypeConversionFailure::diagnoseAsError() {
  auto *anchor = getAnchor();

  if (diagnoseParameterUse())
    return true;

  if (ConvertTo) {
    emitDiagnostic(anchor->getLoc(), diag::converting_noescape_to_type,
                   ConvertTo);
    return true;
  }

  GenericTypeParamType *paramTy = nullptr;

  auto path = getLocator()->getPath();
  if (!path.empty()) {
    auto &last = path.back();
    if (last.getKind() == ConstraintLocator::GenericParameter)
      paramTy = last.getGenericParameter();
  }

  if (paramTy) {
    emitDiagnostic(anchor->getLoc(), diag::converting_noescape_to_type,
                  paramTy);
  } else {
    emitDiagnostic(anchor->getLoc(), diag::unknown_escaping_use_of_noescape);
  }
  return true;
}

bool NoEscapeFuncToTypeConversionFailure::diagnoseParameterUse() const {
  // If the other side is not a function, we have common case diagnostics
  // which handle function-to-type conversion diagnostics.
  if (!ConvertTo || !ConvertTo->is<FunctionType>())
    return false;

  auto *anchor = getAnchor();
  auto diagnostic = diag::general_noescape_to_escaping;

  ParamDecl *PD = nullptr;
  if (auto *DRE = dyn_cast<DeclRefExpr>(anchor)) {
    PD = dyn_cast<ParamDecl>(DRE->getDecl());

    // If anchor is not a parameter declaration there
    // is no need to dig up more information.
    if (!PD)
      return false;

    // Let's check whether this is a function parameter passed
    // as an argument to another function which accepts @escaping
    // function at that position.
    if (auto argApplyInfo = getFunctionArgApplyInfo(getLocator())) {
      auto paramInterfaceTy = argApplyInfo->getParamInterfaceType();
      if (paramInterfaceTy->isTypeParameter()) {
        auto diagnoseGenericParamFailure = [&](GenericTypeParamDecl *decl) {
          emitDiagnostic(anchor->getLoc(),
                         diag::converting_noespace_param_to_generic_type,
                         PD->getName(), paramInterfaceTy);

          emitDiagnostic(decl, diag::generic_parameters_always_escaping);
        };

        // If this is a situation when non-escaping parameter is passed
        // to the argument which represents generic parameter, there is
        // a tailored diagnostic for that.

        if (auto *DMT = paramInterfaceTy->getAs<DependentMemberType>()) {
          diagnoseGenericParamFailure(DMT->getRootGenericParam()->getDecl());
          return true;
        }

        if (auto *GP = paramInterfaceTy->getAs<GenericTypeParamType>()) {
          diagnoseGenericParamFailure(GP->getDecl());
          return true;
        }
      }

      // If there are no generic parameters involved, this could
      // only mean that parameter is expecting @escaping function type.
      diagnostic = diag::passing_noescape_to_escaping;
    }
  } else if (auto *AE = dyn_cast<AssignExpr>(getRawAnchor())) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(AE->getSrc())) {
      PD = dyn_cast<ParamDecl>(DRE->getDecl());
      diagnostic = diag::assigning_noescape_to_escaping;
    }
  }

  if (!PD)
    return false;

  emitDiagnostic(anchor->getLoc(), diagnostic, PD->getName());

  // Give a note and fix-it
  auto note =
      emitDiagnostic(PD->getLoc(), diag::noescape_parameter, PD->getName());

  if (!PD->isAutoClosure()) {
    note.fixItInsert(PD->getTypeLoc().getSourceRange().Start, "@escaping ");
  } // TODO: add in a fixit for autoclosure

  return true;
}

bool MissingForcedDowncastFailure::diagnoseAsError() {
  if (hasComplexLocator())
    return false;

  auto &TC = getTypeChecker();

  auto *expr = getAnchor();
  if (auto *assignExpr = dyn_cast<AssignExpr>(expr))
    expr = assignExpr->getSrc();
  auto *coerceExpr = dyn_cast<CoerceExpr>(expr);
  if (!coerceExpr)
    return false;

  auto *subExpr = coerceExpr->getSubExpr();
  auto fromType = getType(subExpr)->getRValueType();
  auto toType = resolveType(coerceExpr->getCastTypeLoc().getType());

  auto castKind =
      TC.typeCheckCheckedCast(fromType, toType, CheckedCastContextKind::None,
                              getDC(), coerceExpr->getLoc(), subExpr,
                              coerceExpr->getCastTypeLoc().getSourceRange());

  switch (castKind) {
  // Invalid cast.
  case CheckedCastKind::Unresolved:
    // Fix didn't work, let diagnoseFailureForExpr handle this.
    return false;
  case CheckedCastKind::Coercion:
  case CheckedCastKind::BridgingCoercion:
    llvm_unreachable("Coercions handled in other disjunction branch");

  // Valid casts.
  case CheckedCastKind::ArrayDowncast:
  case CheckedCastKind::DictionaryDowncast:
  case CheckedCastKind::SetDowncast:
  case CheckedCastKind::ValueCast:
    emitDiagnostic(coerceExpr->getLoc(), diag::missing_forced_downcast,
                   fromType, toType)
        .highlight(coerceExpr->getSourceRange())
        .fixItReplace(coerceExpr->getLoc(), "as!");
    return true;
  }
  llvm_unreachable("unhandled cast kind");
}

bool MissingAddressOfFailure::diagnoseAsError() {
  if (hasComplexLocator())
    return false;

  auto *anchor = getAnchor();
  auto type = getType(anchor)->getRValueType();
  emitDiagnostic(anchor->getLoc(), diag::missing_address_of, type)
      .fixItInsert(anchor->getStartLoc(), "&");
  return true;
}

bool MissingExplicitConversionFailure::diagnoseAsError() {
  if (hasComplexLocator())
    return false;

  auto *DC = getDC();
  auto &TC = getTypeChecker();

  auto *anchor = getAnchor();
  if (auto *assign = dyn_cast<AssignExpr>(anchor))
    anchor = assign->getSrc();
  if (auto *paren = dyn_cast<ParenExpr>(anchor))
    anchor = paren->getSubExpr();

  auto fromType = getType(anchor)->getRValueType();
  Type toType = resolveType(ConvertingTo);
  if (!toType->hasTypeRepr())
    return false;

  bool useAs = TC.isExplicitlyConvertibleTo(fromType, toType, DC);
  bool useAsBang = !useAs && TC.checkedCastMaySucceed(fromType, toType, DC);
  if (!useAs && !useAsBang)
    return false;

  auto *expr = getParentExpr();
  // If we're performing pattern matching,
  // "as" means something completely different...
  if (auto binOpExpr = dyn_cast<BinaryExpr>(expr)) {
    auto overloadedFn = dyn_cast<OverloadedDeclRefExpr>(binOpExpr->getFn());
    if (overloadedFn && !overloadedFn->getDecls().empty()) {
      ValueDecl *decl0 = overloadedFn->getDecls()[0];
      if (decl0->getBaseName() == decl0->getASTContext().Id_MatchOperator)
        return false;
    }
  }

  bool needsParensInside = exprNeedsParensBeforeAddingAs(anchor);
  bool needsParensOutside = exprNeedsParensAfterAddingAs(anchor, expr);

  llvm::SmallString<2> insertBefore;
  llvm::SmallString<32> insertAfter;
  if (needsParensOutside) {
    insertBefore += "(";
  }
  if (needsParensInside) {
    insertBefore += "(";
    insertAfter += ")";
  }
  insertAfter += useAs ? " as " : " as! ";
  insertAfter += toType->getWithoutParens()->getString();
  if (needsParensOutside)
    insertAfter += ")";

  auto diagID =
      useAs ? diag::missing_explicit_conversion : diag::missing_forced_downcast;
  auto diag = emitDiagnostic(anchor->getLoc(), diagID, fromType, toType);
  if (!insertBefore.empty()) {
    diag.fixItInsert(anchor->getStartLoc(), insertBefore);
  }
  diag.fixItInsertAfter(anchor->getEndLoc(), insertAfter);
  return true;
}

bool MemberAccessOnOptionalBaseFailure::diagnoseAsError() {
  if (hasComplexLocator())
    return false;

  auto *anchor = getAnchor();
  auto baseType = getType(anchor)->getRValueType();
  bool resultIsOptional = ResultTypeIsOptional;

  // If we've resolved the member overload to one that returns an optional
  // type, then the result of the expression is optional (and we want to offer
  // only a '?' fixit) even though the constraint system didn't need to add any
  // additional optionality.
  auto overload = getResolvedOverload(getLocator());
  if (overload && overload->ImpliedType->getOptionalObjectType())
    resultIsOptional = true;

  auto unwrappedBaseType = baseType->getOptionalObjectType();
  if (!unwrappedBaseType)
    return false;

  emitDiagnostic(anchor->getLoc(), diag::optional_base_not_unwrapped,
                 baseType, Member, unwrappedBaseType);

  // FIXME: It would be nice to immediately offer "base?.member ?? defaultValue"
  // for non-optional results where that would be appropriate. For the moment
  // always offering "?" means that if the user chooses chaining, we'll end up
  // in MissingOptionalUnwrapFailure:diagnose() to offer a default value during
  // the next compile.
  emitDiagnostic(anchor->getLoc(), diag::optional_base_chain, Member)
      .fixItInsertAfter(anchor->getEndLoc(), "?");

  if (!resultIsOptional) {
    emitDiagnostic(anchor->getLoc(), diag::unwrap_with_force_value)
      .fixItInsertAfter(anchor->getEndLoc(), "!");
  }

  return true;
}

void MissingOptionalUnwrapFailure::offerDefaultValueUnwrapFixIt(
    DeclContext *DC, Expr *expr) const {
  assert(expr);

  auto *anchor = getAnchor();
  // If anchor is n explicit address-of, or expression which produces
  // an l-value (e.g. first argument of `+=` operator), let's not
  // suggest default value here because that would produce r-value type.
  if (isa<InOutExpr>(anchor))
    return;

  if (auto argApplyInfo = getFunctionArgApplyInfo(getLocator()))
    if (argApplyInfo->getParameterFlags().isInOut())
      return;

  auto diag = emitDiagnostic(expr->getLoc(), diag::unwrap_with_default_value);

  auto &TC = getTypeChecker();
  // Figure out what we need to parenthesize.
  bool needsParensInside =
      exprNeedsParensBeforeAddingNilCoalescing(TC, DC, expr);
  bool needsParensOutside =
      exprNeedsParensAfterAddingNilCoalescing(TC, DC, expr, getParentExpr());

  llvm::SmallString<2> insertBefore;
  llvm::SmallString<32> insertAfter;
  if (needsParensOutside) {
    insertBefore += "(";
  }
  if (needsParensInside) {
    insertBefore += "(";
    insertAfter += ")";
  }
  insertAfter += " ?? <" "#default value#" ">";
  if (needsParensOutside)
    insertAfter += ")";

  if (!insertBefore.empty()) {
    diag.fixItInsert(expr->getStartLoc(), insertBefore);
  }
  diag.fixItInsertAfter(expr->getEndLoc(), insertAfter);
}

// Suggest a force-unwrap.
void MissingOptionalUnwrapFailure::offerForceUnwrapFixIt(Expr *expr) const {
  auto diag = emitDiagnostic(expr->getLoc(), diag::unwrap_with_force_value);

  // If expr is optional as the result of an optional chain and this last
  // dot isn't a member returning optional, then offer to force the last
  // link in the chain, rather than an ugly parenthesized postfix force.
  if (auto optionalChain = dyn_cast<OptionalEvaluationExpr>(expr)) {
    if (auto dotExpr =
        dyn_cast<UnresolvedDotExpr>(optionalChain->getSubExpr())) {
      auto bind = dyn_cast<BindOptionalExpr>(dotExpr->getBase());
      if (bind && !getType(dotExpr)->getOptionalObjectType()) {
        diag.fixItReplace(SourceRange(bind->getLoc()), "!");
        return;
      }
    }
  }

  if (expr->canAppendPostfixExpression(true)) {
    diag.fixItInsertAfter(expr->getEndLoc(), "!");
  } else {
    diag.fixItInsert(expr->getStartLoc(), "(")
        .fixItInsertAfter(expr->getEndLoc(), ")!");
  }
}

class VarDeclMultipleReferencesChecker : public ASTWalker {
  VarDecl *varDecl;
  int count;

  std::pair<bool, Expr *> walkToExprPre(Expr *E) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (DRE->getDecl() == varDecl)
        count++;
    }
    return { true, E };
  }

public:
  VarDeclMultipleReferencesChecker(VarDecl *varDecl) : varDecl(varDecl),count(0) {}
  int referencesCount() { return count; }
};

bool MissingOptionalUnwrapFailure::diagnoseAsError() {
  if (hasComplexLocator())
    return false;

  auto *anchor = getAnchor();

  if (auto assignExpr = dyn_cast<AssignExpr>(anchor))
    anchor = assignExpr->getSrc();

  auto *unwrappedExpr = anchor->getValueProvidingExpr();

  if (auto *tryExpr = dyn_cast<OptionalTryExpr>(unwrappedExpr)) {
    bool isSwift5OrGreater = getASTContext().isSwiftVersionAtLeast(5);
    auto subExprType = getType(tryExpr->getSubExpr());
    bool subExpressionIsOptional = (bool)subExprType->getOptionalObjectType();

    if (isSwift5OrGreater && subExpressionIsOptional) {
      // Using 'try!' won't change the type for a 'try?' with an optional
      // sub-expr under Swift 5+, so just report that a missing unwrap can't be
      // handled here.
      return false;
    }

    emitDiagnostic(tryExpr->getTryLoc(), diag::missing_unwrap_optional_try,
                   getType(anchor)->getRValueType())
        .fixItReplace({tryExpr->getTryLoc(), tryExpr->getQuestionLoc()},
                      "try!");
    return true;
  }

  auto baseType = getBaseType();
  auto unwrappedType = getUnwrappedType();

  assert(!baseType->hasTypeVariable() &&
         "Base type must not be a type variable");
  assert(!unwrappedType->hasTypeVariable() &&
         "Unwrapped type must not be a type variable");

  if (!baseType->getOptionalObjectType())
    return false;

  emitDiagnostic(unwrappedExpr->getLoc(), diag::optional_not_unwrapped,
                 baseType, unwrappedType);

  // If the expression we're unwrapping is the only reference to a
  // local variable whose type isn't explicit in the source, then
  // offer unwrapping fixits on the initializer as well.
  if (auto declRef = dyn_cast<DeclRefExpr>(unwrappedExpr)) {
    if (auto varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
      bool singleUse = false;
      AbstractFunctionDecl *AFD = nullptr;
      if (auto contextDecl = varDecl->getDeclContext()->getAsDecl()) {
        if ((AFD = dyn_cast<AbstractFunctionDecl>(contextDecl))) {
          auto checker = VarDeclMultipleReferencesChecker(varDecl);
          AFD->getBody()->walk(checker);
          singleUse = checker.referencesCount() == 1;
        }
      }

      PatternBindingDecl *binding = varDecl->getParentPatternBinding();
      if (singleUse && binding && binding->getNumPatternEntries() == 1 &&
          varDecl->getTypeSourceRangeForDiagnostics().isInvalid()) {

        auto *initializer = varDecl->getParentInitializer();
        if (!initializer)
          return true;

        if (auto declRefExpr = dyn_cast<DeclRefExpr>(initializer)) {
          if (declRefExpr->getDecl()
                  ->getAttrs()
                  .hasAttribute<ImplicitlyUnwrappedOptionalAttr>()) {
            emitDiagnostic(declRefExpr->getLoc(), diag::unwrap_iuo_initializer,
                           baseType);
          }
        }

        auto fnTy = AFD->getInterfaceType()->castTo<AnyFunctionType>();
        bool voidReturn =
            fnTy->getResult()->isEqual(TupleType::getEmpty(getASTContext()));

        auto diag = emitDiagnostic(varDecl->getLoc(), diag::unwrap_with_guard);
        diag.fixItInsert(binding->getStartLoc(), "guard ");
        if (voidReturn) {
          diag.fixItInsertAfter(binding->getEndLoc(), " else { return }");
        } else {
          diag.fixItInsertAfter(binding->getEndLoc(), " else { return <"
                                                      "#default value#"
                                                      "> }");
        }
        diag.flush();

        offerDefaultValueUnwrapFixIt(varDecl->getDeclContext(), initializer);
        offerForceUnwrapFixIt(initializer);
      }
    }
  }

  offerDefaultValueUnwrapFixIt(getDC(), unwrappedExpr);
  offerForceUnwrapFixIt(unwrappedExpr);
  return true;
}

bool RValueTreatedAsLValueFailure::diagnoseAsError() {
  Diag<StringRef> subElementDiagID;
  Diag<Type> rvalueDiagID = diag::assignment_lhs_not_lvalue;
  Expr *diagExpr = getRawAnchor();
  SourceLoc loc = diagExpr->getLoc();

  if (auto assignExpr = dyn_cast<AssignExpr>(diagExpr)) {
    diagExpr = assignExpr->getDest();
  }

  if (auto callExpr = dyn_cast<ApplyExpr>(diagExpr)) {
    Expr *argExpr = callExpr->getArg();
    loc = callExpr->getFn()->getLoc();

    if (isa<PrefixUnaryExpr>(callExpr) || isa<PostfixUnaryExpr>(callExpr)) {
      subElementDiagID = diag::cannot_apply_lvalue_unop_to_subelement;
      rvalueDiagID = diag::cannot_apply_lvalue_unop_to_rvalue;
      diagExpr = argExpr;
    } else if (isa<BinaryExpr>(callExpr)) {
      subElementDiagID = diag::cannot_apply_lvalue_binop_to_subelement;
      rvalueDiagID = diag::cannot_apply_lvalue_binop_to_rvalue;
      auto argTuple = dyn_cast<TupleExpr>(argExpr);
      diagExpr = argTuple->getElement(0);
    } else if (getLocator()->getPath().size() > 0) {
      auto lastPathElement = getLocator()->getPath().back();
      assert(lastPathElement.getKind() ==
             ConstraintLocator::PathElementKind::ApplyArgToParam);

      subElementDiagID = diag::cannot_pass_rvalue_inout_subelement;
      rvalueDiagID = diag::cannot_pass_rvalue_inout;
      if (auto argTuple = dyn_cast<TupleExpr>(argExpr))
        diagExpr = argTuple->getElement(lastPathElement.getValue());
      else if (auto parens = dyn_cast<ParenExpr>(argExpr))
        diagExpr = parens->getSubExpr();
    } else {
      subElementDiagID = diag::assignment_lhs_is_apply_expression;
    }
  } else if (auto inoutExpr = dyn_cast<InOutExpr>(diagExpr)) {
    if (auto restriction = getRestrictionForType(getType(inoutExpr))) {
      PointerTypeKind pointerKind;
      if (restriction->second == ConversionRestrictionKind::ArrayToPointer &&
          restriction->first->getAnyPointerElementType(pointerKind) &&
          (pointerKind == PTK_UnsafePointer ||
           pointerKind == PTK_UnsafeRawPointer)) {
        // If we're converting to an UnsafePointer, then the programmer
        // specified an & unnecessarily. Produce a fixit hint to remove it.
        emitDiagnostic(inoutExpr->getLoc(),
                       diag::extra_address_of_unsafepointer, restriction->first)
            .highlight(inoutExpr->getSourceRange())
            .fixItRemove(inoutExpr->getStartLoc());
        return true;
      }
    }

    subElementDiagID = diag::cannot_pass_rvalue_inout_subelement;
    rvalueDiagID = diag::cannot_pass_rvalue_inout;
    diagExpr = inoutExpr->getSubExpr();
  } else if (isa<DeclRefExpr>(diagExpr)) {
    subElementDiagID = diag::assignment_lhs_is_immutable_variable;
  } else if (isa<ForceValueExpr>(diagExpr)) {
    subElementDiagID = diag::assignment_bang_has_immutable_subcomponent;
  } else if (isa<MemberRefExpr>(diagExpr)) {
    subElementDiagID = diag::assignment_lhs_is_immutable_property;
  } else if (auto member = dyn_cast<UnresolvedDotExpr>(diagExpr)) {
    subElementDiagID = diag::assignment_lhs_is_immutable_property;

    if (auto *ctor = dyn_cast<ConstructorDecl>(getDC())) {
      if (auto *baseRef = dyn_cast<DeclRefExpr>(member->getBase())) {
        if (baseRef->getDecl() == ctor->getImplicitSelfDecl() &&
            ctor->getDelegatingOrChainedInitKind(nullptr) ==
            ConstructorDecl::BodyInitKind::Delegating) {
          emitDiagnostic(loc, diag::assignment_let_property_delegating_init,
                      member->getName());
          if (auto *ref = getResolvedMemberRef(member)) {
            emitDiagnostic(ref, diag::decl_declared_here, member->getName());
          }
          return true;
        }
      }
    }

    if (auto resolvedOverload = getResolvedOverload(getLocator())) {
      if (resolvedOverload->Choice.getKind() ==
          OverloadChoiceKind::DynamicMemberLookup)
        subElementDiagID = diag::assignment_dynamic_property_has_immutable_base;

      if (resolvedOverload->Choice.getKind() ==
          OverloadChoiceKind::KeyPathDynamicMemberLookup) {
        if (!getType(member->getBase())->hasLValueType())
          subElementDiagID =
              diag::assignment_dynamic_property_has_immutable_base;
      }
    }
  } else if (auto sub = dyn_cast<SubscriptExpr>(diagExpr)) {
      subElementDiagID = diag::assignment_subscript_has_immutable_base;
  } else {
    subElementDiagID = diag::assignment_lhs_is_immutable_variable;
  }

  AssignmentFailure failure(diagExpr, getConstraintSystem(), loc,
                            subElementDiagID, rvalueDiagID);
  return failure.diagnose();
}

bool TrailingClosureAmbiguityFailure::diagnoseAsNote() {
  const auto *expr = getParentExpr();
  auto *callExpr = dyn_cast<CallExpr>(expr);
  if (!callExpr)
    return false;
  if (!callExpr->hasTrailingClosure())
    return false;
  if (callExpr->getFn() != getAnchor())
    return false;

  llvm::SmallMapVector<Identifier, const ValueDecl *, 8> choicesByLabel;
  for (const auto &choice : Choices) {
    auto *callee = dyn_cast<AbstractFunctionDecl>(choice.getDecl());
    if (!callee)
      return false;

    const ParameterList *paramList = callee->getParameters();
    const ParamDecl *param = paramList->getArray().back();

    // Sanity-check that the trailing closure corresponds to this parameter.
    if (!param->hasValidSignature() ||
        !param->getInterfaceType()->is<AnyFunctionType>())
      return false;

    Identifier trailingClosureLabel = param->getArgumentName();
    auto &choiceForLabel = choicesByLabel[trailingClosureLabel];

    // FIXME: Cargo-culted from diagnoseAmbiguity: apparently the same decl can
    // appear more than once?
    if (choiceForLabel == callee)
      continue;

    // If just providing the trailing closure label won't solve the ambiguity,
    // don't bother offering the fix-it.
    if (choiceForLabel != nullptr)
      return false;

    choiceForLabel = callee;
  }

  // If we got here, then all of the choices have unique labels. Offer them in
  // order.
  for (const auto &choicePair : choicesByLabel) {
    auto diag = emitDiagnostic(
        expr->getLoc(), diag::ambiguous_because_of_trailing_closure,
        choicePair.first.empty(), choicePair.second->getFullName());
    swift::fixItEncloseTrailingClosure(getTypeChecker(), diag, callExpr,
                                       choicePair.first);
  }

  return true;
}

AssignmentFailure::AssignmentFailure(Expr *destExpr, ConstraintSystem &cs,
                                     SourceLoc diagnosticLoc)
    : FailureDiagnostic(destExpr, cs, cs.getConstraintLocator(destExpr)),
      Loc(diagnosticLoc),
      DeclDiagnostic(findDeclDiagonstic(cs.getASTContext(), destExpr)),
      TypeDiagnostic(diag::assignment_lhs_not_lvalue) {}

bool AssignmentFailure::diagnoseAsError() {
  auto &cs = getConstraintSystem();
  auto *DC = getDC();
  auto *destExpr = getParentExpr();

  // Walk through the destination expression, resolving what the problem is.  If
  // we find a node in the lvalue path that is problematic, this returns it.
  auto immInfo = resolveImmutableBase(destExpr);

  Optional<OverloadChoice> choice = immInfo.second;

  // Attempt diagnostics based on the overload choice.
  if (choice.hasValue()) {

    auto getKeyPathArgument = [](SubscriptExpr *expr) {
      auto *TE = dyn_cast<TupleExpr>(expr->getIndex());
      assert(TE->getNumElements() == 1);
      assert(TE->getElementName(0).str() == "keyPath");
      return TE->getElement(0);
    };

    if (!choice->isDecl()) {
      if (choice->getKind() == OverloadChoiceKind::KeyPathApplication &&
          !isa<ApplyExpr>(immInfo.first)) {
        std::string message = "key path is read-only";
        if (auto *SE = dyn_cast<SubscriptExpr>(immInfo.first)) {
          if (auto *DRE = dyn_cast<DeclRefExpr>(getKeyPathArgument(SE))) {
            auto identifier = DRE->getDecl()->getBaseName().getIdentifier();
            message =
                "'" + identifier.str().str() + "' is a read-only key path";
          }
        }
        emitDiagnostic(Loc, DeclDiagnostic, message)
            .highlight(immInfo.first->getSourceRange());
        return true;
      }
      return false;
    }

    // Otherwise, we cannot resolve this because the available setter candidates
    // are all mutating and the base must be mutating.  If we dug out a
    // problematic decl, we can produce a nice tailored diagnostic.
    if (auto *VD = dyn_cast<VarDecl>(choice->getDecl())) {
      std::string message = "'";
      message += VD->getName().str().str();
      message += "'";

      auto type = getType(immInfo.first);

      if (isKnownKeyPathType(type))
        message += " is read-only";
      else if (VD->isCaptureList())
        message += " is an immutable capture";
      else if (VD->isImplicit())
        message += " is immutable";
      else if (VD->isLet())
        message += " is a 'let' constant";
      else if (!VD->isSettable(DC))
        message += " is a get-only property";
      else if (!VD->isSetterAccessibleFrom(DC))
        message += " setter is inaccessible";
      else {
        message += " is immutable";
      }

      emitDiagnostic(Loc, DeclDiagnostic, message)
          .highlight(immInfo.first->getSourceRange());

      // If there is a masked instance variable of the same type, emit a
      // note to fixit prepend a 'self.'.
      if (auto typeContext = DC->getInnermostTypeContext()) {
        UnqualifiedLookup lookup(VD->getFullName(), typeContext,
                                 getASTContext().getLazyResolver());
        for (auto &result : lookup.Results) {
          const VarDecl *typeVar = dyn_cast<VarDecl>(result.getValueDecl());
          if (typeVar && typeVar != VD && typeVar->isSettable(DC) &&
              typeVar->isSetterAccessibleFrom(DC) &&
              typeVar->getType()->isEqual(VD->getType())) {
            // But not in its own accessor.
            auto AD =
                dyn_cast_or_null<AccessorDecl>(DC->getInnermostMethodContext());
            if (!AD || AD->getStorage() != typeVar) {
              emitDiagnostic(Loc, diag::masked_instance_variable,
                             typeContext->getSelfTypeInContext())
                  .fixItInsert(Loc, "self.");
            }
          }
        }
      }

      // If this is a simple variable marked with a 'let', emit a note to fixit
      // hint it to 'var'.
      VD->emitLetToVarNoteIfSimple(DC);
      return true;
    }

    // If the underlying expression was a read-only subscript, diagnose that.
    if (auto *SD = dyn_cast_or_null<SubscriptDecl>(choice->getDecl())) {
      StringRef message;
      if (!SD->isSettable())
        message = "subscript is get-only";
      else if (!SD->isSetterAccessibleFrom(DC))
        message = "subscript setter is inaccessible";
      else
        message = "subscript is immutable";

      emitDiagnostic(Loc, DeclDiagnostic, message)
          .highlight(immInfo.first->getSourceRange());
      return true;
    }

    // If we're trying to set an unapplied method, say that.
    if (auto *VD = choice->getDecl()) {
      std::string message = "'";
      message += VD->getBaseName().getIdentifier().str();
      message += "'";

      auto diagID = DeclDiagnostic;
      if (auto *AFD = dyn_cast<AbstractFunctionDecl>(VD)) {
        if (AFD->hasImplicitSelfDecl()) {
          message += " is a method";
          diagID = diag::assignment_lhs_is_immutable_variable;
        } else {
          message += " is a function";
        }
      } else
        message += " is not settable";

      emitDiagnostic(Loc, diagID, message)
          .highlight(immInfo.first->getSourceRange());
      return true;
    }
  }

  // Fall back to producing diagnostics based on the expression since we
  // couldn't determine anything from the OverloadChoice.

  // If a keypath was the problem but wasn't resolved into a vardecl
  // it is ambiguous or unable to be used for setting.
  if (auto *KPE = dyn_cast_or_null<KeyPathExpr>(immInfo.first)) {
    emitDiagnostic(Loc, DeclDiagnostic, "immutable key path")
        .highlight(KPE->getSourceRange());
    return true;
  }

  if (auto LE = dyn_cast<LiteralExpr>(immInfo.first)) {
    emitDiagnostic(Loc, DeclDiagnostic, "literals are not mutable")
        .highlight(LE->getSourceRange());
    return true;
  }

  // If the expression is the result of a call, it is an rvalue, not a mutable
  // lvalue.
  if (auto *AE = dyn_cast<ApplyExpr>(immInfo.first)) {
    // Handle literals, which are a call to the conversion function.
    auto argsTuple =
        dyn_cast<TupleExpr>(AE->getArg()->getSemanticsProvidingExpr());
    if (isa<CallExpr>(AE) && AE->isImplicit() && argsTuple &&
        argsTuple->getNumElements() == 1) {
      if (auto LE = dyn_cast<LiteralExpr>(
              argsTuple->getElement(0)->getSemanticsProvidingExpr())) {
        emitDiagnostic(Loc, DeclDiagnostic, "literals are not mutable")
            .highlight(LE->getSourceRange());
        return true;
      }
    }

    std::string name = "call";
    if (isa<PrefixUnaryExpr>(AE) || isa<PostfixUnaryExpr>(AE))
      name = "unary operator";
    else if (isa<BinaryExpr>(AE))
      name = "binary operator";
    else if (isa<CallExpr>(AE))
      name = "function call";
    else if (isa<DotSyntaxCallExpr>(AE) || isa<DotSyntaxBaseIgnoredExpr>(AE))
      name = "method call";

    if (auto *DRE = dyn_cast<DeclRefExpr>(AE->getFn()->getValueProvidingExpr()))
      name = std::string("'") +
             DRE->getDecl()->getBaseName().getIdentifier().str().str() + "'";

    emitDiagnostic(Loc, DeclDiagnostic, name + " returns immutable value")
        .highlight(AE->getSourceRange());
    return true;
  }

  if (auto contextualType = cs.getContextualType(immInfo.first)) {
    Type neededType = contextualType->getInOutObjectType();
    Type actualType = getType(immInfo.first)->getInOutObjectType();
    if (!neededType->isEqual(actualType)) {
      if (DeclDiagnostic.ID == diag::cannot_pass_rvalue_inout_subelement.ID) {
        // We have a special diagnostic with tailored wording for this
        // common case.
        emitDiagnostic(Loc, diag::cannot_pass_rvalue_inout_converted,
                       actualType, neededType)
            .highlight(immInfo.first->getSourceRange());

        if (auto inoutExpr = dyn_cast<InOutExpr>(immInfo.first))
          fixItChangeInoutArgType(inoutExpr->getSubExpr(), actualType,
                                  neededType);
      } else {
        emitDiagnostic(Loc, DeclDiagnostic,
                       "implicit conversion from '" + actualType->getString() +
                           "' to '" + neededType->getString() +
                           "' requires a temporary")
            .highlight(immInfo.first->getSourceRange());
      }
      return true;
    }
  }

  if (auto IE = dyn_cast<IfExpr>(immInfo.first)) {
    emitDiagnostic(Loc, DeclDiagnostic,
                   "result of conditional operator '? :' is never mutable")
        .highlight(IE->getQuestionLoc())
        .highlight(IE->getColonLoc());
    return true;
  }

  emitDiagnostic(Loc, TypeDiagnostic, getType(destExpr))
      .highlight(immInfo.first->getSourceRange());
  return true;
}

void AssignmentFailure::fixItChangeInoutArgType(const Expr *arg,
                                                Type actualType,
                                                Type neededType) const {
  auto *DC = getDC();
  auto *DRE = dyn_cast<DeclRefExpr>(arg);
  if (!DRE)
    return;

  auto *VD = dyn_cast_or_null<VarDecl>(DRE->getDecl());
  if (!VD)
    return;

  // Don't emit for non-local variables.
  // (But in script-mode files, we consider module-scoped
  // variables in the same file to be local variables.)
  auto VDC = VD->getDeclContext();
  bool isLocalVar = VDC->isLocalContext();
  if (!isLocalVar && VDC->isModuleScopeContext()) {
    auto argFile = DC->getParentSourceFile();
    auto varFile = VDC->getParentSourceFile();
    isLocalVar = (argFile == varFile && argFile->isScriptMode());
  }
  if (!isLocalVar)
    return;

  SmallString<32> scratch;
  SourceLoc endLoc;   // Filled in if we decide to diagnose this
  SourceLoc startLoc; // Left invalid if we're inserting

  auto isSimpleTypelessPattern = [](Pattern *P) -> bool {
    if (auto VP = dyn_cast_or_null<VarPattern>(P))
      P = VP->getSubPattern();
    return P && isa<NamedPattern>(P);
  };

  auto typeRange = VD->getTypeSourceRangeForDiagnostics();
  if (typeRange.isValid()) {
    startLoc = typeRange.Start;
    endLoc = typeRange.End;
  } else if (isSimpleTypelessPattern(VD->getParentPattern())) {
    endLoc = VD->getNameLoc();
    scratch += ": ";
  }

  if (endLoc.isInvalid())
    return;

  scratch += neededType.getString();

  // Adjust into the location where we actually want to insert
  endLoc = Lexer::getLocForEndOfToken(getASTContext().SourceMgr, endLoc);

  // Since we already adjusted endLoc, this will turn an insertion
  // into a zero-character replacement.
  if (!startLoc.isValid())
    startLoc = endLoc;

  emitDiagnostic(VD->getLoc(), diag::inout_change_var_type_if_possible,
                 actualType, neededType)
      .fixItReplaceChars(startLoc, endLoc, scratch);
}

std::pair<Expr *, Optional<OverloadChoice>>
AssignmentFailure::resolveImmutableBase(Expr *expr) const {
  auto &cs = getConstraintSystem();
  auto *DC = getDC();
  expr = expr->getValueProvidingExpr();

  auto isImmutable = [&DC](ValueDecl *decl) {
    if (auto *storage = dyn_cast<AbstractStorageDecl>(decl))
      return !storage->isSettable(nullptr) ||
             !storage->isSetterAccessibleFrom(DC);

    return false;
  };

  // Provide specific diagnostics for assignment to subscripts whose base expr
  // is known to be an rvalue.
  if (auto *SE = dyn_cast<SubscriptExpr>(expr)) {
    // If we found a decl for the subscript, check to see if it is a set-only
    // subscript decl.
    if (SE->hasDecl()) {
      const auto &declRef = SE->getDecl();
      if (auto *subscript =
              dyn_cast_or_null<SubscriptDecl>(declRef.getDecl())) {
        if (isImmutable(subscript))
          return {expr, OverloadChoice(getType(SE->getBase()), subscript,
                                       FunctionRefKind::DoubleApply)};
      }
    }

    Optional<OverloadChoice> member = getMemberRef(
        cs.getConstraintLocator(SE, ConstraintLocator::SubscriptMember));

    // If it isn't settable, return it.
    if (member) {
      if (member->isDecl() && isImmutable(member->getDecl()))
        return {expr, member};

      // We still have a choice, the choice is not a decl
      if (!member->isDecl()) {
        // This must be a keypath application
        assert(member->getKind() == OverloadChoiceKind::KeyPathApplication);

        auto *argType = getType(SE->getIndex())->castTo<TupleType>();
        assert(argType->getNumElements() == 1);

        auto indexType = resolveType(argType->getElementType(0));

        if (auto bgt = indexType->getAs<BoundGenericType>()) {
          // In Swift versions lower than 5, this check will fail as read only
          // key paths can masquerade as writable for compatibilty reasons.
          // This is fine as in this case we just fall back on old diagnostics.
          if (bgt->getDecl() == getASTContext().getKeyPathDecl()) {
            return {expr, member};
          }
        }
      }
    }

    // If it is settable, then the base must be the problem, recurse.
    return resolveImmutableBase(SE->getBase());
  }

  // Look through property references.
  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(expr)) {
    // If we found a decl for the UDE, check it.
    auto loc = cs.getConstraintLocator(UDE, ConstraintLocator::Member);

    auto member = getMemberRef(loc);

    // If we can resolve a member, we can determine whether it is settable in
    // this context.
    if (member && member->isDecl() && isImmutable(member->getDecl()))
      return {expr, member};

    // If we weren't able to resolve a member or if it is mutable, then the
    // problem must be with the base, recurse.
    return resolveImmutableBase(UDE->getBase());
  }

  if (auto *MRE = dyn_cast<MemberRefExpr>(expr)) {
    // If the member isn't settable, then it is the problem: return it.
    if (auto member = dyn_cast<AbstractStorageDecl>(MRE->getMember().getDecl()))
      if (isImmutable(member))
        return {expr, OverloadChoice(getType(MRE->getBase()), member,
                                     FunctionRefKind::SingleApply)};

    // If we weren't able to resolve a member or if it is mutable, then the
    // problem must be with the base, recurse.
    return resolveImmutableBase(MRE->getBase());
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(expr))
    return {expr,
            OverloadChoice(Type(), DRE->getDecl(), FunctionRefKind::Unapplied)};

  // Look through x!
  if (auto *FVE = dyn_cast<ForceValueExpr>(expr))
    return resolveImmutableBase(FVE->getSubExpr());

  // Look through x?
  if (auto *BOE = dyn_cast<BindOptionalExpr>(expr))
    return resolveImmutableBase(BOE->getSubExpr());

  // Look through implicit conversions
  if (auto *ICE = dyn_cast<ImplicitConversionExpr>(expr))
    if (!isa<LoadExpr>(ICE->getSubExpr()))
      return resolveImmutableBase(ICE->getSubExpr());

  if (auto *SAE = dyn_cast<SelfApplyExpr>(expr))
    return resolveImmutableBase(SAE->getFn());

  return {expr, None};
}

Optional<OverloadChoice>
AssignmentFailure::getMemberRef(ConstraintLocator *locator) const {
  auto member = getOverloadChoiceIfAvailable(locator);
  if (!member)
    return None;

  if (!member->choice.isDecl())
    return member->choice;

  auto *DC = getDC();
  auto &TC = getTypeChecker();

  auto *decl = member->choice.getDecl();
  if (isa<SubscriptDecl>(decl) &&
      isValidDynamicMemberLookupSubscript(cast<SubscriptDecl>(decl), DC, TC)) {
    auto *subscript = cast<SubscriptDecl>(decl);
    // If this is a keypath dynamic member lookup, we have to
    // adjust the locator to find member referred by it.
    if (isValidKeyPathDynamicMemberLookup(subscript, TC)) {
      auto &cs = getConstraintSystem();
      // Type has a following format:
      // `(Self) -> (dynamicMember: {Writable}KeyPath<T, U>) -> U`
      auto *fullType = member->openedFullType->castTo<FunctionType>();
      auto *fnType = fullType->getResult()->castTo<FunctionType>();

      auto paramTy = fnType->getParams()[0].getPlainType();
      auto keyPath = paramTy->getAnyNominal();
      auto memberLoc = cs.getConstraintLocator(
          locator, LocatorPathElt::getKeyPathDynamicMember(keyPath));

      auto memberRef = getOverloadChoiceIfAvailable(memberLoc);
      return memberRef ? Optional<OverloadChoice>(memberRef->choice) : None;
    }

    // If this is a string based dynamic lookup, there is no member declaration.
    return None;
  }

  return member->choice;
}

Diag<StringRef> AssignmentFailure::findDeclDiagonstic(ASTContext &ctx,
                                                      Expr *destExpr) {
  if (isa<ApplyExpr>(destExpr) || isa<SelfApplyExpr>(destExpr))
    return diag::assignment_lhs_is_apply_expression;

  if (isa<UnresolvedDotExpr>(destExpr) || isa<MemberRefExpr>(destExpr))
    return diag::assignment_lhs_is_immutable_property;

  if (auto *subscript = dyn_cast<SubscriptExpr>(destExpr)) {
    auto diagID = diag::assignment_subscript_has_immutable_base;
    // If the destination is a subscript with a 'dynamicLookup:' label and if
    // the tuple is implicit, then this was actually a @dynamicMemberLookup
    // access. Emit a more specific diagnostic.
    if (subscript->getIndex()->isImplicit() &&
        subscript->getArgumentLabels().size() == 1 &&
        subscript->getArgumentLabels().front() == ctx.Id_dynamicMember)
      diagID = diag::assignment_dynamic_property_has_immutable_base;

    return diagID;
  }

  return diag::assignment_lhs_is_immutable_variable;
}

bool ContextualFailure::diagnoseAsError() {
  auto *anchor = getAnchor();
  auto path = getLocator()->getPath();

  assert(!path.empty());

  if (diagnoseMissingFunctionCall())
    return true;

  Diag<Type, Type> diagnostic;
  switch (path.back().getKind()) {
  case ConstraintLocator::ClosureResult: {
    diagnostic = diag::cannot_convert_closure_result;
    break;
  }

  case ConstraintLocator::ContextualType: {
    if (isKnownKeyPathType(FromType) && isKnownKeyPathType(ToType)) {
      diagnostic = diag::cannot_convert_initializer_value;
      break;
    }

    LLVM_FALLTHROUGH;
  }
  default:
    return false;
  }

  auto diag = emitDiagnostic(anchor->getLoc(), diagnostic, FromType, ToType);
  diag.highlight(anchor->getSourceRange());

  (void)trySequenceSubsequenceFixIts(diag, getConstraintSystem(), FromType,
                                     ToType, anchor);
  return true;
}

bool ContextualFailure::diagnoseMissingFunctionCall() const {
  auto &TC = getTypeChecker();

  auto *srcFT = FromType->getAs<FunctionType>();
  if (!srcFT || !srcFT->getParams().empty())
    return false;

  if (ToType->is<AnyFunctionType>() ||
      !TC.isConvertibleTo(srcFT->getResult(), ToType, getDC()))
    return false;

  auto *anchor = getAnchor();
  emitDiagnostic(anchor->getLoc(), diag::missing_nullary_call,
                 srcFT->getResult())
      .highlight(anchor->getSourceRange())
      .fixItInsertAfter(anchor->getEndLoc(), "()");

  tryComputedPropertyFixIts(anchor);

  return true;
}

bool ContextualFailure::trySequenceSubsequenceFixIts(InFlightDiagnostic &diag,
                                                     ConstraintSystem &CS,
                                                     Type fromType, Type toType,
                                                     Expr *expr) {
  if (!CS.TC.Context.getStdlibModule())
    return false;

  auto String = CS.TC.getStringType(CS.DC);
  auto Substring = CS.TC.getSubstringType(CS.DC);

  if (!String || !Substring)
    return false;

  // Substring -> String conversion
  // Wrap in String.init
  if (fromType->isEqual(Substring)) {
    if (toType->isEqual(String)) {
      auto range = expr->getSourceRange();
      diag.fixItInsert(range.Start, "String(");
      diag.fixItInsertAfter(range.End, ")");
      return true;
    }
  }

  return false;
}

void ContextualFailure::tryComputedPropertyFixIts(Expr *expr) const {
  if (!isa<ClosureExpr>(expr))
    return;

  // It is possible that we're looking at a stored property being
  // initialized with a closure. Something like:
  //
  // var foo: Int = { return 0 }
  //
  // Let's offer another fix-it to remove the '=' to turn the stored
  // property into a computed property. If the variable is immutable, then
  // replace the 'let' with a 'var'.

  PatternBindingDecl *PBD = nullptr;

  if (auto TLCD = dyn_cast<TopLevelCodeDecl>(getDC())) {
    if (TLCD->getBody()->isImplicit()) {
      if (auto decl = TLCD->getBody()->getElement(0).dyn_cast<Decl *>()) {
        if (auto binding = dyn_cast<PatternBindingDecl>(decl)) {
          PBD = binding;
        }
      }
    }
  } else if (auto PBI = dyn_cast<PatternBindingInitializer>(getDC())) {
    PBD = PBI->getBinding();
  }

  if (PBD) {
    if (auto VD = PBD->getSingleVar()) {
      auto entry = PBD->getPatternEntryForVarDecl(VD);

      if (!VD->isStatic() &&
          !VD->getAttrs().getAttribute<DynamicReplacementAttr>() &&
          entry.getInit() && isa<ClosureExpr>(entry.getInit())) {
        auto diag = emitDiagnostic(expr->getLoc(),
                                   diag::extension_stored_property_fixit,
                                   VD->getName());
        diag.fixItRemove(entry.getEqualLoc());

        if (VD->isLet()) {
          diag.fixItReplace(PBD->getStartLoc(), getTokenText(tok::kw_var));
        }

        if (auto lazyAttr = VD->getAttrs().getAttribute<LazyAttr>()) {
          diag.fixItRemove(lazyAttr->getRange());
        }
      }
    }
  }
}

bool AutoClosureForwardingFailure::diagnoseAsError() {
  auto path = getLocator()->getPath();
  assert(!path.empty());

  auto &last = path.back();
  assert(last.getKind() == ConstraintLocator::ApplyArgToParam);

  // We need a raw anchor here because `getAnchor()` is simplified
  // to the argument expression.
  auto *argExpr = getArgumentExpr(getRawAnchor(), last.getValue());
  emitDiagnostic(argExpr->getLoc(), diag::invalid_autoclosure_forwarding)
      .highlight(argExpr->getSourceRange())
      .fixItInsertAfter(argExpr->getEndLoc(), "()");
  return true;
}

bool NonOptionalUnwrapFailure::diagnoseAsError() {
  auto *anchor = getAnchor();

  auto diagnostic = diag::invalid_optional_chain;
  if (isa<ForceValueExpr>(anchor))
    diagnostic = diag::invalid_force_unwrap;

  emitDiagnostic(anchor->getLoc(), diagnostic, BaseType)
      .highlight(anchor->getSourceRange())
      .fixItRemove(anchor->getEndLoc());

  return true;
}

bool MissingCallFailure::diagnoseAsError() {
  auto *baseExpr = getAnchor();
  SourceLoc insertLoc = baseExpr->getEndLoc();

  if (auto *FVE = dyn_cast<ForceValueExpr>(baseExpr))
    baseExpr = FVE->getSubExpr();

  // Calls are not yet supported by key path, but it
  // is useful to record this fix to diagnose chaining
  // where one of the key path components is a method
  // reference.
  if (isa<KeyPathExpr>(baseExpr))
    return false;

  auto path = getLocator()->getPath();
  if (!path.empty()) {
    const auto &last = path.back();

    switch (last.getKind()) {
    case ConstraintLocator::ContextualType:
    case ConstraintLocator::ApplyArgToParam: {
      auto fnType = getType(baseExpr)->castTo<FunctionType>();
      assert(fnType->getNumParams() == 0);
      emitDiagnostic(baseExpr->getLoc(), diag::missing_nullary_call,
                     fnType->getResult())
          .fixItInsertAfter(baseExpr->getEndLoc(), "()");
      return true;
    }

    case ConstraintLocator::FunctionResult: {
      path = path.drop_back();
      if (path.back().getKind() != ConstraintLocator::AutoclosureResult)
        break;

      LLVM_FALLTHROUGH;
    }

    case ConstraintLocator::AutoclosureResult: {
      auto &cs = getConstraintSystem();
      auto loc = getConstraintLocator(getRawAnchor(), path.drop_back());
      AutoClosureForwardingFailure failure(cs, loc);
      return failure.diagnoseAsError();
    }
    default:
      break;
    }
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(baseExpr)) {
    emitDiagnostic(baseExpr->getLoc(), diag::did_not_call_function,
                   DRE->getDecl()->getBaseName().getIdentifier())
        .fixItInsertAfter(insertLoc, "()");
    return true;
  }

  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(baseExpr)) {
    emitDiagnostic(baseExpr->getLoc(), diag::did_not_call_method,
                   UDE->getName().getBaseIdentifier())
        .fixItInsertAfter(insertLoc, "()");
    return true;
  }

  if (auto *DSCE = dyn_cast<DotSyntaxCallExpr>(baseExpr)) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(DSCE->getFn())) {
      emitDiagnostic(baseExpr->getLoc(), diag::did_not_call_method,
                     DRE->getDecl()->getBaseName().getIdentifier())
          .fixItInsertAfter(insertLoc, "()");
      return true;
    }
  }

  if (auto *AE = dyn_cast<AssignExpr>(baseExpr)) {
    auto *srcExpr = AE->getSrc();
    if (auto *fnType = getType(srcExpr)->getAs<FunctionType>()) {
      emitDiagnostic(srcExpr->getLoc(), diag::missing_nullary_call,
                     fnType->getResult())
          .highlight(srcExpr->getSourceRange())
          .fixItInsertAfter(srcExpr->getEndLoc(), "()");
      return true;
    }
  }

  emitDiagnostic(baseExpr->getLoc(), diag::did_not_call_function_value)
      .fixItInsertAfter(insertLoc, "()");
  return true;
}

bool ExtraneousPropertyWrapperUnwrapFailure::diagnoseAsError() {
  auto loc = getAnchor()->getLoc();
  auto newPrefix = usingStorageWrapper() ? "$" : "_";

  if (auto *member = getReferencedMember()) {
    emitDiagnostic(loc, diag::incorrect_property_wrapper_reference_member,
                   member->getDescriptiveKind(), member->getFullName(), false,
                   getToType())
        .fixItInsert(loc, newPrefix);
    return true;
  }

  emitDiagnostic(loc, diag::incorrect_property_wrapper_reference,
                 getPropertyName(), getFromType(), getToType(), false)
      .fixItInsert(loc, newPrefix);
  return true;
}

bool MissingPropertyWrapperUnwrapFailure::diagnoseAsError() {
  auto loc = getAnchor()->getLoc();
  auto endLoc = getAnchor()->getLoc().getAdvancedLoc(1);

  if (auto *member = getReferencedMember()) {
    emitDiagnostic(loc, diag::incorrect_property_wrapper_reference_member,
                   member->getDescriptiveKind(), member->getFullName(), true,
                   getToType())
        .fixItRemoveChars(loc, endLoc);
    return true;
  }

  emitDiagnostic(loc, diag::incorrect_property_wrapper_reference,
                 getPropertyName(), getFromType(), getToType(), true)
      .fixItRemoveChars(loc, endLoc);
  return true;
}

bool SubscriptMisuseFailure::diagnoseAsError() {
  auto &sourceMgr = getASTContext().SourceMgr;

  auto *memberExpr = cast<UnresolvedDotExpr>(getRawAnchor());
  auto *baseExpr = getAnchor();

  auto memberRange = baseExpr->getSourceRange();
  (void)simplifyLocator(getConstraintSystem(), getLocator(), memberRange);

  auto nameLoc = DeclNameLoc(memberRange.Start);

  auto diag = emitDiagnostic(baseExpr->getLoc(),
                             diag::could_not_find_subscript_member_did_you_mean,
                             getType(baseExpr));

  diag.highlight(memberRange).highlight(nameLoc.getSourceRange());

  if (auto *parentExpr = dyn_cast_or_null<ApplyExpr>(findParentExpr(memberExpr))) {
    auto *argExpr = parentExpr->getArg();

    auto toCharSourceRange = Lexer::getCharSourceRangeFromSourceRange;
    auto lastArgSymbol = toCharSourceRange(sourceMgr, argExpr->getEndLoc());

    diag.fixItReplace(SourceRange(argExpr->getStartLoc()),
                      getTokenText(tok::l_square));
    diag.fixItRemove(nameLoc.getSourceRange());
    diag.fixItRemove(SourceRange(memberExpr->getDotLoc()));

    if (sourceMgr.extractText(lastArgSymbol) == getTokenText(tok::r_paren))
      diag.fixItReplace(SourceRange(argExpr->getEndLoc()),
                        getTokenText(tok::r_square));
    else
      diag.fixItInsertAfter(argExpr->getEndLoc(), getTokenText(tok::r_square));
  } else {
    diag.fixItReplace(SourceRange(memberExpr->getDotLoc(), memberExpr->getLoc()), "[<#index#>]");
  }
  diag.flush();

  if (auto overload = getOverloadChoiceIfAvailable(getLocator())) {
    emitDiagnostic(overload->choice.getDecl(), diag::kind_declared_here,
                   DescriptiveDeclKind::Subscript);
  }

  return true;
}

bool SubscriptMisuseFailure::diagnoseAsNote() {
  if (auto overload = getOverloadChoiceIfAvailable(getLocator())) {
    emitDiagnostic(overload->choice.getDecl(), diag::found_candidate);
    return true;
  }
  return false;
}

/// When a user refers a enum case with a wrong member name, we try to find a
/// enum element whose name differs from the wrong name only in convention;
/// meaning their lower case counterparts are identical.
///   - DeclName is valid when such a correct case is found; invalid otherwise.
DeclName MissingMemberFailure::findCorrectEnumCaseName(
    Type Ty, TypoCorrectionResults &corrections, DeclName memberName) {
  if (memberName.isSpecial() || !memberName.isSimpleName())
    return DeclName();
  if (!Ty->getEnumOrBoundGenericEnum())
    return DeclName();
  auto candidate =
      corrections.getUniqueCandidateMatching([&](ValueDecl *candidate) {
        return (isa<EnumElementDecl>(candidate) &&
                candidate->getFullName().getBaseIdentifier().str().equals_lower(
                    memberName.getBaseIdentifier().str()));
      });
  return (candidate ? candidate->getFullName() : DeclName());
}

bool MissingMemberFailure::diagnoseAsError() {
  auto &TC = getTypeChecker();
  auto *anchor = getRawAnchor();
  auto *baseExpr = getAnchor();

  if (!anchor || !baseExpr)
    return false;

  if (auto *typeVar = getBaseType()->getAs<TypeVariableType>()) {
    auto &CS = getConstraintSystem();
    auto *memberLoc = typeVar->getImpl().getLocator();
    // Don't try to diagnose anything besides first missing
    // member in the chain. e.g. `x.foo().bar()` let's make
    // sure to diagnose only `foo()` as missing because we
    // don't really know much about what `bar()` is supposed
    // to be.
    if (CS.MissingMembers.count(memberLoc))
      return false;
  }

  auto baseType = resolveType(getBaseType())->getWithoutSpecifierType();

  DeclNameLoc nameLoc(anchor->getStartLoc());
  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(anchor)) {
    nameLoc = UDE->getNameLoc();
  } else if (auto *UME = dyn_cast<UnresolvedMemberExpr>(anchor)) {
    nameLoc = UME->getNameLoc();
  }

  auto emitBasicError = [&](Type baseType) {
    auto diagnostic = diag::could_not_find_value_member;

    if (auto *metatype = baseType->getAs<MetatypeType>()) {
      baseType = metatype->getInstanceType();
      diagnostic = diag::could_not_find_type_member;
    }

    if (baseType->is<TupleType>())
      diagnostic = diag::could_not_find_tuple_member;

    emitDiagnostic(anchor->getLoc(), diagnostic, baseType, getName())
        .highlight(baseExpr->getSourceRange())
        .highlight(nameLoc.getSourceRange());
  };

  TypoCorrectionResults corrections(TC, getName(), nameLoc);
  auto tryTypoCorrection = [&] {
    TC.performTypoCorrection(getDC(), DeclRefKind::Ordinary, baseType,
                             defaultMemberLookupOptions, corrections);
  };

  if (getName().getBaseName().getKind() == DeclBaseName::Kind::Subscript) {
    auto loc = anchor->getLoc();
    if (auto *metatype = baseType->getAs<MetatypeType>()) {
      emitDiagnostic(loc, diag::could_not_find_type_member,
                     metatype->getInstanceType(), getName())
        .highlight(baseExpr->getSourceRange());
    } else {
      emitDiagnostic(loc, diag::could_not_find_value_subscript, baseType)
        .highlight(baseExpr->getSourceRange());
    }
  } else if (getName().getBaseName() == "deinit") {
    // Specialised diagnostic if trying to access deinitialisers
    emitDiagnostic(anchor->getLoc(), diag::destructor_not_accessible)
        .highlight(baseExpr->getSourceRange());
  } else if (auto metatypeTy = baseType->getAs<MetatypeType>()) {
    auto instanceTy = metatypeTy->getInstanceType();
    tryTypoCorrection();

    if (DeclName rightName =
            findCorrectEnumCaseName(instanceTy, corrections, getName())) {
      emitDiagnostic(anchor->getLoc(), diag::could_not_find_enum_case,
                     instanceTy, getName(), rightName)
          .fixItReplace(nameLoc.getBaseNameLoc(),
                        rightName.getBaseIdentifier().str());
      return true;
    }

    if (auto correction = corrections.claimUniqueCorrection()) {
      auto diagnostic = emitDiagnostic(
          anchor->getLoc(), diag::could_not_find_type_member_corrected,
          instanceTy, getName(), correction->CorrectedName);
      diagnostic.highlight(baseExpr->getSourceRange())
          .highlight(nameLoc.getSourceRange());
      correction->addFixits(diagnostic);
    } else {
      emitBasicError(baseType);
    }
  } else if (auto moduleTy = baseType->getAs<ModuleType>()) {
    emitDiagnostic(baseExpr->getLoc(), diag::no_member_of_module,
                   moduleTy->getModule()->getName(), getName())
        .highlight(baseExpr->getSourceRange())
        .highlight(nameLoc.getSourceRange());
    return true;
  } else {
    // Check for a few common cases that can cause missing members.
    auto *ED = baseType->getEnumOrBoundGenericEnum();
    if (ED && getName().isSimpleName("rawValue")) {
      auto loc = ED->getNameLoc();
      if (loc.isValid()) {
        emitBasicError(baseType);
        emitDiagnostic(loc, diag::did_you_mean_raw_type);
        return true;
      }
    } else if (baseType->isAny()) {
      emitBasicError(baseType);
      emitDiagnostic(anchor->getLoc(), diag::any_as_anyobject_fixit)
          .fixItInsert(baseExpr->getStartLoc(), "(")
          .fixItInsertAfter(baseExpr->getEndLoc(), " as AnyObject)");
      return true;
    }

    tryTypoCorrection();

    if (auto correction = corrections.claimUniqueCorrection()) {
      auto diagnostic = emitDiagnostic(
          anchor->getLoc(), diag::could_not_find_value_member_corrected,
          baseType, getName(), correction->CorrectedName);
      diagnostic.highlight(baseExpr->getSourceRange())
          .highlight(nameLoc.getSourceRange());
      correction->addFixits(diagnostic);
    } else {
      emitBasicError(baseType);
    }
  }

  // Note all the correction candidates.
  corrections.noteAllCandidates();
  return true;
}

bool InvalidMemberRefOnExistential::diagnoseAsError() {
  auto *anchor = getRawAnchor();

  Expr *baseExpr = getAnchor();
  DeclNameLoc nameLoc;
  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(anchor)) {
    baseExpr = UDE->getBase();
    nameLoc = UDE->getNameLoc();
  } else if (auto *UME = dyn_cast<UnresolvedMemberExpr>(anchor)) {
    nameLoc = UME->getNameLoc();
  } else if (auto *SE = dyn_cast<SubscriptExpr>(anchor)) {
    baseExpr = SE->getBase();
  } else if (auto *call = dyn_cast<CallExpr>(anchor)) {
    baseExpr = call->getFn();
  }

  emitDiagnostic(getAnchor()->getLoc(),
                 diag::could_not_use_member_on_existential, getBaseType(),
                 getName())
      .highlight(nameLoc.getSourceRange())
      .highlight(baseExpr->getSourceRange());
  return true;
}

bool AllowTypeOrInstanceMemberFailure::diagnoseAsError() {
  auto loc = getAnchor()->getLoc();
  auto &cs = getConstraintSystem();
  auto *DC = getDC();
  auto locator = getLocator();

  if (loc.isInvalid()) {
    return true;
  }

  Expr *expr = getParentExpr();
  SourceRange baseRange = expr ? expr->getSourceRange() : SourceRange();

  // If the base is an implicit self type reference, and we're in a
  // an initializer, then the user wrote something like:
  //
  //   class Foo { let x = 1, y = x }
  //
  // which runs in type context, not instance context, or
  //
  //   class Bar {
  //     let otherwise = 1              // instance member
  //     var x: Int
  //     func init(x: Int =otherwise) { // default parameter
  //       self.x = x
  //     }
  //   }
  //
  // in which an instance member is used as a default value for a
  // parameter.
  //
  // Produce a tailored diagnostic for these cases since this
  // comes up and is otherwise non-obvious what is going on.

  if (Name.isSimpleName(DeclBaseName::createConstructor()) &&
      !BaseType->is<AnyMetatypeType>()) {
    if (auto ctorRef = dyn_cast<UnresolvedDotExpr>(getRawAnchor())) {
      if (isa<SuperRefExpr>(ctorRef->getBase())) {
        emitDiagnostic(loc, diag::super_initializer_not_in_initializer);
        return true;
      }

      auto isCallArgument = [this](Expr *expr) {
        auto &cs = getConstraintSystem();
        auto argExpr = cs.getParentExpr(expr);
        if (!argExpr)
          return false;
        auto possibleApplyExpr = cs.getParentExpr(expr);
        return possibleApplyExpr && isa<ApplyExpr>(possibleApplyExpr);
      };

      auto *initCall = cs.getParentExpr(cs.getParentExpr(ctorRef));

      auto isMutable = [&DC](ValueDecl *decl) {
        if (auto *storage = dyn_cast<AbstractStorageDecl>(decl))
          return storage->isSettable(DC) && storage->isSetterAccessibleFrom(DC);

        return true;
      };

      auto selection = getChoiceFor(ctorRef->getBase());
      if (selection) {
        OverloadChoice choice = selection->choice;
        if (choice.isDecl() && isMutable(choice.getDecl()) &&
            !isCallArgument(initCall) &&
            cs.getContextualTypePurpose() == CTP_Unused) {
          auto fixItLoc = ctorRef->getBase()->getSourceRange().End;
          emitDiagnostic(loc, diag::init_not_instance_member_use_assignment)
              .fixItInsertAfter(fixItLoc, " = ");
          return true;
        }

        SourceRange fixItRng = ctorRef->getBase()->getSourceRange();
        emitDiagnostic(loc, diag::init_not_instance_member)
            .fixItInsert(fixItRng.Start, "type(of: ")
            .fixItInsertAfter(fixItRng.End, ")");
        return true;
      }
    }
  }

  if (BaseType->is<AnyMetatypeType>() && !Member->isStatic()) {
    auto instanceTy = BaseType;

    if (auto *AMT = instanceTy->getAs<AnyMetatypeType>()) {
      instanceTy = AMT->getInstanceType();
    }

    if (getRawAnchor() &&
        cs.DC->getContextKind() == DeclContextKind::Initializer) {
      auto *TypeDC = cs.DC->getParent();
      bool propertyInitializer = true;
      // If the parent context is not a type context, we expect it
      // to be a defaulted parameter in a function declaration.
      if (!TypeDC->isTypeContext()) {
        assert(TypeDC->getContextKind() ==
               DeclContextKind::AbstractFunctionDecl &&
               "Expected function decl context for initializer!");
        TypeDC = TypeDC->getParent();
        propertyInitializer = false;
      }
      
      assert(TypeDC->isTypeContext() && "Expected type decl context!");
      
      if (TypeDC->getSelfNominalTypeDecl() == instanceTy->getAnyNominal()) {
        if (propertyInitializer) {
          emitDiagnostic(loc, diag::instance_member_in_initializer, Name);
          return true;
        } else {
          emitDiagnostic(loc, diag::instance_member_in_default_parameter, Name);
          return true;
        }
      }
    }

    auto maybeCallExpr = getRawAnchor();

    if (auto UDE = dyn_cast<UnresolvedDotExpr>(maybeCallExpr)) {
      maybeCallExpr = UDE->getBase();
    }

    if (auto callExpr = dyn_cast<ApplyExpr>(maybeCallExpr)) {
      auto fnExpr = callExpr->getFn();
      auto fnType = cs.getType(fnExpr)->getRValueType();
      auto arg = callExpr->getArg();

      if (fnType->is<ExistentialMetatypeType>()) {
        emitDiagnostic(arg->getStartLoc(),
                       diag::missing_init_on_metatype_initialization)
            .highlight(fnExpr->getSourceRange());
        return true;
      }
    }

    // Check whether the instance member is declared on parent context and if so
    // provide more specialized message.
    auto memberTypeContext =
        Member->getDeclContext()->getInnermostTypeContext();
    auto currentTypeContext = cs.DC->getInnermostTypeContext();
    
    if (memberTypeContext && currentTypeContext &&
        memberTypeContext->getSemanticDepth() <
        currentTypeContext->getSemanticDepth()) {
      emitDiagnostic(loc, diag::could_not_use_instance_member_on_type,
                     currentTypeContext->getDeclaredInterfaceType(), Name,
                     memberTypeContext->getDeclaredInterfaceType(), true)
          .highlight(baseRange)
          .highlight(Member->getSourceRange());
      return true;
    }

    if (auto *UDE = dyn_cast<UnresolvedDotExpr>(getRawAnchor())) {
      auto *baseExpr = UDE->getBase();
      if (isa<TypeExpr>(baseExpr)) {
        emitDiagnostic(loc, diag::instance_member_use_on_type, instanceTy, Name)
            .highlight(baseExpr->getSourceRange());
        return true;
      }
    }

    // Just emit a generic "instance member cannot be used" error
    emitDiagnostic(loc, diag::could_not_use_instance_member_on_type, instanceTy,
                   Name, instanceTy, false)
        .highlight(getAnchor()->getSourceRange());
    return true;
  } else {
    // If the base of the lookup is a protocol metatype, suggest
    // to replace the metatype with 'Self'
    // error saying the lookup cannot be on a protocol metatype
    Optional<InFlightDiagnostic> Diag;
    auto baseTy = BaseType;

    if (auto metatypeTy = baseTy->getAs<AnyMetatypeType>()) {
      auto instanceTy = metatypeTy->getInstanceType();

      // This will only happen if we have an unresolved dot expression
      // (.foo) where foo is a protocol member and the contextual type is
      // an optional protocol metatype.
      if (auto objectTy = instanceTy->getOptionalObjectType()) {
        instanceTy = objectTy;
        baseTy = MetatypeType::get(objectTy);
      }

      if (instanceTy->isExistentialType()) {
        // Give a customized message if we're accessing a member type
        // of a protocol -- otherwise a diagnostic talking about
        // static members doesn't make a whole lot of sense
        if (auto TAD = dyn_cast<TypeAliasDecl>(Member)) {
          Diag.emplace(emitDiagnostic(loc, diag::typealias_outside_of_protocol,
                                      TAD->getName()));
        } else if (auto ATD = dyn_cast<AssociatedTypeDecl>(Member)) {
          Diag.emplace(emitDiagnostic(loc, diag::assoc_type_outside_of_protocol,
                                      ATD->getName()));
        } else if (isa<ConstructorDecl>(Member)) {
          Diag.emplace(emitDiagnostic(loc, diag::construct_protocol_by_name,
                                      instanceTy));
        } else {
          Diag.emplace(emitDiagnostic(
              loc, diag::could_not_use_type_member_on_protocol_metatype, baseTy,
              Name));
        }

        Diag->highlight(baseRange).highlight(getAnchor()->getSourceRange());

        // See through function decl context
        if (auto parent = cs.DC->getInnermostTypeContext()) {
          // If we are in a protocol extension of 'Proto' and we see
          // 'Proto.static', suggest 'Self.static'
          if (auto extensionContext = parent->getExtendedProtocolDecl()) {
            if (extensionContext->getDeclaredType()->isEqual(instanceTy)) {
              Diag->fixItReplace(getAnchor()->getSourceRange(), "Self");
            }
          }
        }

        return true;
      }
    }

    // If this is a reference to a static member by one of the key path
    // components, let's provide a tailored diagnostic and return because
    // that is unsupported so there is no fix-it.
    if (locator->isForKeyPathComponent()) {
      InvalidStaticMemberRefInKeyPath failure(expr, cs, Member, locator);
      return failure.diagnoseAsError();
    }

    if (isa<EnumElementDecl>(Member)) {
      Diag.emplace(emitDiagnostic(
          loc, diag::could_not_use_enum_element_on_instance, Name));
    } else {
      Diag.emplace(emitDiagnostic(
          loc, diag::could_not_use_type_member_on_instance, baseTy, Name));
    }

    Diag->highlight(getAnchor()->getSourceRange());

    if (Name.isSimpleName(DeclBaseName::createConstructor()) &&
        !baseTy->is<AnyMetatypeType>()) {
      if (auto ctorRef = dyn_cast<UnresolvedDotExpr>(getRawAnchor())) {
        SourceRange fixItRng = ctorRef->getNameLoc().getSourceRange();
        Diag->fixItInsert(fixItRng.Start, "type(of: ");
        Diag->fixItInsertAfter(fixItRng.End, ")");
        return true;
      }
    }

    // Determine the contextual type of the expression
    Type contextualType;
    for (auto iterateCS = &cs; contextualType.isNull() && iterateCS;
         iterateCS = iterateCS->baseCS) {
      contextualType = iterateCS->getContextualType();
    }
    
    // Try to provide a fix-it that only contains a '.'
    if (contextualType && baseTy->isEqual(contextualType)) {
      Diag->fixItInsert(loc, ".");
      return true;
    }

    // Check if the expression is the matching operator ~=, most often used in
    // case statements. If so, try to provide a single dot fix-it
    const Expr *contextualTypeNode = nullptr;
    ConstraintSystem *lastCS = nullptr;
    for (auto iterateCS = &cs; iterateCS; iterateCS = iterateCS->baseCS) {
      lastCS = iterateCS;
      contextualTypeNode = iterateCS->getContextualTypeNode();
    }
    
    // The '~=' operator is an overloaded decl ref inside a binaryExpr
    if (auto binaryExpr = dyn_cast<BinaryExpr>(contextualTypeNode)) {
      if (auto overloadedFn
          = dyn_cast<OverloadedDeclRefExpr>(binaryExpr->getFn())) {
        if (!overloadedFn->getDecls().empty()) {
          // Fetch any declaration to check if the name is '~='
          ValueDecl *decl0 = overloadedFn->getDecls()[0];
          
          if (decl0->getBaseName() == decl0->getASTContext().Id_MatchOperator) {
            assert(binaryExpr->getArg()->getElements().size() == 2);
            
            // If the rhs of '~=' is the enum type, a single dot suffixes
            // since the type can be inferred
            Type secondArgType =
            lastCS->getType(binaryExpr->getArg()->getElement(1));
            if (secondArgType->isEqual(baseTy)) {
              Diag->fixItInsert(loc, ".");
              return true;
            }
          }
        }
      }
    }

    // Fall back to a fix-it with a full type qualifier
    if (auto *NTD = Member->getDeclContext()->getSelfNominalTypeDecl()) {
      auto typeName = NTD->getSelfInterfaceType()->getString();
      if (auto *SE = dyn_cast<SubscriptExpr>(getRawAnchor())) {
        auto *baseExpr = SE->getBase();
        Diag->fixItReplace(baseExpr->getSourceRange(), typeName);
      } else {
        Diag->fixItInsert(loc, typeName + ".");
      }
    }

    return true;
  }

  return false;
}

bool PartialApplicationFailure::diagnoseAsError() {
  auto &cs = getConstraintSystem();
  auto *anchor = cast<UnresolvedDotExpr>(getRawAnchor());

  RefKind kind = RefKind::MutatingMethod;

  // If this is initializer delegation chain, we have a tailored message.
  if (getOverloadChoiceIfAvailable(cs.getConstraintLocator(
          anchor, ConstraintLocator::ConstructorMember))) {
    kind = anchor->getBase()->isSuperExpr() ? RefKind::SuperInit
                                            : RefKind::SelfInit;
  }

  auto diagnostic = CompatibilityWarning
                        ? diag::partial_application_of_function_invalid_swift4
                        : diag::partial_application_of_function_invalid;

  emitDiagnostic(anchor->getNameLoc(), diagnostic, kind);
  return true;
}

bool InvalidDynamicInitOnMetatypeFailure::diagnoseAsError() {
  auto *anchor = getRawAnchor();
  emitDiagnostic(anchor->getLoc(), diag::dynamic_construct_class,
                 BaseType->getMetatypeInstanceType())
      .highlight(BaseRange);
  emitDiagnostic(Init, diag::note_nonrequired_initializer, Init->isImplicit(),
                 Init->getFullName());
  return true;
}

bool InitOnProtocolMetatypeFailure::diagnoseAsError() {
  auto *anchor = getRawAnchor();
  if (IsStaticallyDerived) {
    emitDiagnostic(anchor->getLoc(), diag::construct_protocol_by_name,
                   BaseType->getMetatypeInstanceType())
        .highlight(BaseRange);
  } else {
    emitDiagnostic(anchor->getLoc(), diag::construct_protocol_value, BaseType)
        .highlight(BaseRange);
  }

  return true;
}

bool ImplicitInitOnNonConstMetatypeFailure::diagnoseAsError() {
  auto *apply = cast<ApplyExpr>(getRawAnchor());
  auto loc = apply->getArg()->getStartLoc();
  emitDiagnostic(loc, diag::missing_init_on_metatype_initialization)
      .fixItInsert(loc, ".init");
  return true;
}

bool MissingArgumentsFailure::diagnoseAsError() {
  auto *locator = getLocator();
  auto path = locator->getPath();

  // TODO: Currently this is only intended to diagnose contextual failures.
  if (path.empty() ||
      !(path.back().getKind() == ConstraintLocator::ApplyArgToParam ||
        path.back().getKind() == ConstraintLocator::ContextualType))
    return false;

  if (auto *closure = dyn_cast<ClosureExpr>(getAnchor()))
    return diagnoseTrailingClosure(closure);

  return false;
}

bool MissingArgumentsFailure::diagnoseTrailingClosure(ClosureExpr *closure) {
  auto diff = Fn->getNumParams() - NumSynthesized;

  // If the closure didn't specify any arguments and it is in a context that
  // needs some, produce a fixit to turn "{...}" into "{ _,_ in ...}".
  if (diff == 0) {
    auto diag =
        emitDiagnostic(closure->getStartLoc(),
                       diag::closure_argument_list_missing, NumSynthesized);

    std::string fixText; // Let's provide fixits for up to 10 args.
    if (Fn->getNumParams() <= 10) {
      fixText += " ";
      interleave(
          Fn->getParams(),
          [&fixText](const AnyFunctionType::Param &param) { fixText += '_'; },
          [&fixText] { fixText += ','; });
      fixText += " in ";
    }

    if (!fixText.empty()) {
      // Determine if there is already a space after the { in the closure to
      // make sure we introduce the right whitespace.
      auto afterBrace = closure->getStartLoc().getAdvancedLoc(1);
      auto text = getASTContext().SourceMgr.extractText({afterBrace, 1});
      if (text.size() == 1 && text == " ")
        fixText = fixText.erase(fixText.size() - 1);
      else
        fixText = fixText.erase(0, 1);
      diag.fixItInsertAfter(closure->getStartLoc(), fixText);
    }

    return true;
  }

  auto params = closure->getParameters();
  bool onlyAnonymousParams =
      std::all_of(params->begin(), params->end(),
                  [](ParamDecl *param) { return !param->hasName(); });

  auto diag =
      emitDiagnostic(params->getStartLoc(), diag::closure_argument_list_tuple,
                     resolveType(Fn), Fn->getNumParams(), diff, diff == 1);

  // If the number of parameters is less than number of inferred
  // let's try to suggest a fix-it with the rest of the missing parameters.
  if (!closure->hasExplicitResultType() &&
      closure->getInLoc().isValid()) {
    SmallString<32> fixIt;
    llvm::raw_svector_ostream OS(fixIt);

    OS << ",";
    for (unsigned i = 0; i != NumSynthesized; ++i) {
      OS << ((onlyAnonymousParams) ? "_" : "<#arg#>");
      OS << ((i == NumSynthesized - 1) ? " " : ",");
    }

    diag.fixItInsertAfter(params->getEndLoc(), OS.str());
  }

  return true;
}

bool ClosureParamDestructuringFailure::diagnoseAsError() {
  auto *closure = cast<ClosureExpr>(getAnchor());
  auto params = closure->getParameters();

  // In case of implicit parameters e.g. $0, $1 we
  // can't really provide good fix-it because
  // structure of parameter type itself is unclear.
  for (auto *param : params->getArray()) {
    if (param->isImplicit()) {
      emitDiagnostic(params->getStartLoc(),
                     diag::closure_tuple_parameter_destructuring_implicit,
                     getParameterType());
      return true;
    }
  }

  auto diag = emitDiagnostic(params->getStartLoc(),
                             diag::closure_tuple_parameter_destructuring,
                             getParameterType());

  auto *closureBody = closure->getBody();
  if (!closureBody)
    return true;

  auto &sourceMgr = getASTContext().SourceMgr;
  auto bodyStmts = closureBody->getElements();

  SourceLoc bodyLoc;
  SourceLoc inLoc = closure->getInLoc();
  // If location for `in` is unknown we can't proceed
  // since we'll not be able to figure out source line
  // to place the fix-it on.
  if (inLoc.isInvalid())
    return true;

  // If the body is empty let's put the cursor
  // right after "in", otherwise make it start
  // location of the first statement in the body.
  if (bodyStmts.empty())
    bodyLoc = Lexer::getLocForEndOfToken(sourceMgr, inLoc);
  else
    bodyLoc = bodyStmts.front().getStartLoc();

  if (bodyLoc.isInvalid())
    return true;

  SmallString<64> fixIt;
  llvm::raw_svector_ostream OS(fixIt);

  // If this is multi-line closure we'd have to insert new lines
  // in the suggested 'let' to keep the structure of the code intact,
  // otherwise just use ';' to keep everything on the same line.
  auto inLine = sourceMgr.getLineNumber(inLoc);
  auto bodyLine = sourceMgr.getLineNumber(bodyLoc);
  auto isMultiLineClosure = bodyLine > inLine;
  auto indent =
      bodyStmts.empty() ? "" : Lexer::getIndentationForLine(sourceMgr, bodyLoc);

  SmallString<16> parameter;
  llvm::raw_svector_ostream parameterOS(parameter);

  parameterOS << "(";
  interleave(
      params->getArray(),
      [&](const ParamDecl *param) { parameterOS << param->getNameStr(); },
      [&] { parameterOS << ", "; });
  parameterOS << ")";

  // Check if there are any explicit types associated
  // with parameters, if there are, we'll have to add
  // type information to the replacement argument.
  bool explicitTypes =
      llvm::any_of(params->getArray(), [](const ParamDecl *param) {
        return param->getTypeLoc().getTypeRepr();
      });

  if (isMultiLineClosure)
    OS << '\n' << indent;

  // Let's form 'let <name> : [<type>]? = arg' expression.
  OS << "let " << parameterOS.str() << " = arg"
     << (isMultiLineClosure ? "\n" + indent : "; ");

  SmallString<64> argName;
  llvm::raw_svector_ostream nameOS(argName);
  if (explicitTypes) {
    nameOS << "(arg: " << getParameterType()->getString() << ")";
  } else {
    nameOS << "(arg)";
  }

  if (closure->hasSingleExpressionBody()) {
    // Let's see if we need to add result type to the argument/fix-it:
    //  - if the there is a result type associated with the closure;
    //  - and it's not a void type;
    //  - and it hasn't been explicitly written.
    auto resultType = resolveType(ContextualType->getResult());
    auto hasResult = [](Type resultType) -> bool {
      return resultType && !resultType->isVoid();
    };

    auto isValidType = [](Type resultType) -> bool {
      return resultType && !resultType->hasUnresolvedType() &&
             !resultType->hasTypeVariable();
    };

    // If there an expected result type but it hasn't been explicitly
    // provided, let's add it to the argument.
    if (hasResult(resultType) && !closure->hasExplicitResultType()) {
      nameOS << " -> ";
      if (isValidType(resultType))
        nameOS << resultType->getString();
      else
        nameOS << "<#Result#>";
    }

    if (auto stmt = bodyStmts.front().get<Stmt *>()) {
      // If the body is a single expression with implicit return.
      if (isa<ReturnStmt>(stmt) && stmt->isImplicit()) {
        // And there is non-void expected result type,
        // because we add 'let' expression to the body
        // we need to make such 'return' explicit.
        if (hasResult(resultType))
          OS << "return ";
      }
    }
  }

  diag.fixItReplace(params->getSourceRange(), nameOS.str())
      .fixItInsert(bodyLoc, OS.str());
  return true;
}

bool OutOfOrderArgumentFailure::diagnoseAsError() {
  auto *anchor = getRawAnchor();
  auto *argExpr = isa<TupleExpr>(anchor) ? anchor : getArgumentExprFor(anchor);
  if (!argExpr)
    return false;

  auto *tuple = cast<TupleExpr>(argExpr);

  Identifier first = tuple->getElementName(ArgIdx);
  Identifier second = tuple->getElementName(PrevArgIdx);

  // Build a mapping from arguments to parameters.
  SmallVector<unsigned, 4> argBindings(tuple->getNumElements());
  for (unsigned paramIdx = 0; paramIdx != Bindings.size(); ++paramIdx) {
    for (auto argIdx : Bindings[paramIdx])
      argBindings[argIdx] = paramIdx;
  }

  auto argRange = [&](unsigned argIdx, Identifier label) -> SourceRange {
    auto range = tuple->getElement(argIdx)->getSourceRange();
    if (!label.empty())
      range.Start = tuple->getElementNameLoc(argIdx);

    unsigned paramIdx = argBindings[argIdx];
    if (Bindings[paramIdx].size() > 1)
      range.End = tuple->getElement(Bindings[paramIdx].back())->getEndLoc();

    return range;
  };

  auto firstRange = argRange(ArgIdx, first);
  auto secondRange = argRange(PrevArgIdx, second);

  SourceLoc diagLoc = firstRange.Start;

  auto addFixIts = [&](InFlightDiagnostic diag) {
    diag.highlight(firstRange).highlight(secondRange);

    // Move the misplaced argument by removing it from one location and
    // inserting it in another location. To maintain argument comma
    // separation, since the argument is always moving to an earlier index
    // the preceding comma and whitespace is removed and a new trailing
    // comma and space is inserted with the moved argument.
    auto &SM = getASTContext().SourceMgr;
    auto text = SM.extractText(
        Lexer::getCharSourceRangeFromSourceRange(SM, firstRange));

    auto removalRange =
        SourceRange(Lexer::getLocForEndOfToken(
                        SM, tuple->getElement(ArgIdx - 1)->getEndLoc()),
                    firstRange.End);
    diag.fixItRemove(removalRange);
    diag.fixItInsert(secondRange.Start, text.str() + ", ");
  };

  // There are 4 diagnostic messages variations depending on
  // labeled/unlabeled arguments.
  if (first.empty() && second.empty()) {
    addFixIts(emitDiagnostic(diagLoc,
                             diag::argument_out_of_order_unnamed_unnamed,
                             ArgIdx + 1, PrevArgIdx + 1));
  } else if (first.empty() && !second.empty()) {
    addFixIts(emitDiagnostic(diagLoc, diag::argument_out_of_order_unnamed_named,
                             ArgIdx + 1, second));
  } else if (!first.empty() && second.empty()) {
    addFixIts(emitDiagnostic(diagLoc, diag::argument_out_of_order_named_unnamed,
                             first, PrevArgIdx + 1));
  } else {
    addFixIts(emitDiagnostic(diagLoc, diag::argument_out_of_order_named_named,
                             first, second));
  }

  return true;
}

bool InaccessibleMemberFailure::diagnoseAsError() {
  auto *anchor = getRawAnchor();
  // Let's try to avoid over-diagnosing chains of inaccessible
  // members e.g.:
  //
  // struct A {
  //   struct B {
  //     struct C {}
  //   }
  // }
  //
  // _ = A.B.C()
  //
  // We'll have a fix for each `B', `C` and `C.init` but it makes
  // sense to diagnose only `B` and consider the rest hidden.
  Expr *baseExpr = nullptr;
  DeclNameLoc nameLoc;
  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(anchor)) {
    baseExpr = UDE->getBase();
    nameLoc = UDE->getNameLoc();
  } else if (auto *UME = dyn_cast<UnresolvedMemberExpr>(anchor)) {
    nameLoc = UME->getNameLoc();
  } else if (auto *SE = dyn_cast<SubscriptExpr>(anchor)) {
    baseExpr = SE->getBase();
  } else if (auto *call = dyn_cast<CallExpr>(anchor)) {
    baseExpr = call->getFn();
  }

  if (baseExpr) {
    auto &cs = getConstraintSystem();
    auto *locator =
        cs.getConstraintLocator(baseExpr, ConstraintLocator::Member);
    if (llvm::any_of(cs.getFixes(), [&](const ConstraintFix *fix) {
          return fix->getLocator() == locator;
        }))
      return false;
  }

  auto loc = nameLoc.isValid() ? nameLoc.getStartLoc() : anchor->getLoc();
  auto accessLevel = Member->getFormalAccessScope().accessLevelForDiagnostics();
  if (auto *CD = dyn_cast<ConstructorDecl>(Member)) {
    emitDiagnostic(loc, diag::init_candidate_inaccessible,
                   CD->getResultInterfaceType(), accessLevel)
        .highlight(nameLoc.getSourceRange());
  } else {
    emitDiagnostic(loc, diag::candidate_inaccessible, Member->getBaseName(),
                   accessLevel)
        .highlight(nameLoc.getSourceRange());
  }

  emitDiagnostic(Member, diag::decl_declared_here, Member->getFullName());
  return true;
}

bool AnyObjectKeyPathRootFailure::diagnoseAsError() {
  // Diagnose use of AnyObject as root for a keypath

  auto anchor = getAnchor();
  auto loc = anchor->getLoc();
  auto range = anchor->getSourceRange();

  if (auto KPE = dyn_cast<KeyPathExpr>(anchor)) {
    if (auto rootTyRepr = KPE->getRootType()) {
      loc = rootTyRepr->getLoc();
      range = rootTyRepr->getSourceRange();
    }
  }

  emitDiagnostic(loc, diag::expr_swift_keypath_anyobject_root).highlight(range);
  return true;
}

bool KeyPathSubscriptIndexHashableFailure::diagnoseAsError() {
  auto *anchor = getRawAnchor();
  auto *locator = getLocator();

  auto loc = anchor->getLoc();
  if (locator->isKeyPathSubscriptComponent()) {
    auto *KPE = cast<KeyPathExpr>(anchor);
    for (auto &elt : locator->getPath()) {
      if (elt.isKeyPathComponent()) {
        loc = KPE->getComponents()[elt.getValue()].getLoc();
        break;
      }
    }
  }

  emitDiagnostic(loc, diag::expr_keypath_subscript_index_not_hashable,
                 resolveType(NonConformingType));
  return true;
}

SourceLoc InvalidMemberRefInKeyPath::getLoc() const {
  auto *anchor = getRawAnchor();

  if (auto *KPE = dyn_cast<KeyPathExpr>(anchor)) {
    auto *locator = getLocator();
    auto component =
        llvm::find_if(locator->getPath(), [](const LocatorPathElt &elt) {
          return elt.isKeyPathComponent();
        });

    assert(component != locator->getPath().end());
    return KPE->getComponents()[component->getValue()].getLoc();
  }

  return anchor->getLoc();
}

bool InvalidStaticMemberRefInKeyPath::diagnoseAsError() {
  emitDiagnostic(getLoc(), diag::expr_keypath_static_member, getName(),
                 isForKeyPathDynamicMemberLookup());
  return true;
}

bool InvalidMemberWithMutatingGetterInKeyPath::diagnoseAsError() {
  emitDiagnostic(getLoc(), diag::expr_keypath_mutating_getter, getName(),
                 isForKeyPathDynamicMemberLookup());
  return true;
}

bool InvalidMethodRefInKeyPath::diagnoseAsError() {
  emitDiagnostic(getLoc(), diag::expr_keypath_not_property, getKind(),
                 getName(), isForKeyPathDynamicMemberLookup());
  return true;
}

SourceLoc InvalidUseOfAddressOf::getLoc() const {
  auto *anchor = getAnchor();

  if (auto *assign = dyn_cast<AssignExpr>(anchor))
    anchor = assign->getSrc();

  return anchor->getLoc();
}

bool InvalidUseOfAddressOf::diagnoseAsError() {
  emitDiagnostic(getLoc(), diag::extraneous_address_of);
  return true;
}

bool ExtraneousReturnFailure::diagnoseAsError() {
  auto *anchor = getAnchor();
  emitDiagnostic(anchor->getLoc(), diag::cannot_return_value_from_void_func);
  return true;
}

bool CollectionElementContextualFailure::diagnoseAsError() {
  auto *anchor = getAnchor();
  auto *locator = getLocator();

  auto eltType = getFromType();
  auto contextualType = getToType();

  Optional<InFlightDiagnostic> diagnostic;
  if (isa<ArrayExpr>(getRawAnchor())) {
    diagnostic.emplace(emitDiagnostic(anchor->getLoc(),
                                      diag::cannot_convert_array_element,
                                      eltType, contextualType));
  }

  if (isa<DictionaryExpr>(getRawAnchor())) {
    const auto &eltLoc = locator->getPath().back();

    switch (eltLoc.getValue()) {
    case 0: // key
      diagnostic.emplace(emitDiagnostic(anchor->getLoc(),
                                        diag::cannot_convert_dict_key, eltType,
                                        contextualType));
      break;

    case 1: // value
      diagnostic.emplace(emitDiagnostic(anchor->getLoc(),
                                        diag::cannot_convert_dict_value,
                                        eltType, contextualType));
      break;

    default:
      break;
    }
  }

  if (locator->isForSequenceElementType()) {
    diagnostic.emplace(
        emitDiagnostic(anchor->getLoc(),
                       contextualType->isExistentialType()
                           ? diag::cannot_convert_sequence_element_protocol
                           : diag::cannot_convert_sequence_element_value,
                       eltType, contextualType));
  }

  if (!diagnostic)
    return false;

  (void)trySequenceSubsequenceFixIts(*diagnostic, getConstraintSystem(),
                                     eltType, contextualType, anchor);
  return true;
}

bool MissingContextualConformanceFailure::diagnoseAsError() {
  auto *anchor = getAnchor();
  auto path = getLocator()->getPath();

  Optional<Diag<Type, Type>> diagnostic;
  if (path.empty()) {
    assert(isa<AssignExpr>(anchor));
    if (isa<SubscriptExpr>(cast<AssignExpr>(anchor)->getDest())) {
      diagnostic = getDiagnosticFor(CTP_SubscriptAssignSource);
    } else {
      diagnostic = getDiagnosticFor(CTP_AssignSource);
    }
  } else {
    const auto &last = path.back();
    switch (last.getKind()) {
    case ConstraintLocator::ContextualType:
      assert(Context != CTP_Unused);
      diagnostic = getDiagnosticFor(Context);
      break;

    case ConstraintLocator::SequenceElementType: {
      diagnostic = diag::cannot_convert_sequence_element_protocol;
      break;
    }

    default:
      break;
    }
  }

  if (!diagnostic)
    return false;

  auto srcType = getFromType();
  auto dstType = getToType();

  emitDiagnostic(anchor->getLoc(), *diagnostic, srcType, dstType);

  if (isa<InOutExpr>(anchor))
    return true;

  if (srcType->isAny() && dstType->isAnyObject()) {
    emitDiagnostic(anchor->getLoc(), diag::any_as_anyobject_fixit)
        .fixItInsertAfter(anchor->getEndLoc(), " as AnyObject");
  }

  return true;
}

Optional<Diag<Type, Type>>
MissingContextualConformanceFailure::getDiagnosticFor(
    ContextualTypePurpose context) {
  switch (context) {
  case CTP_Initialization:
    return diag::cannot_convert_initializer_value_protocol;
  case CTP_ReturnStmt:
  case CTP_ReturnSingleExpr:
    return diag::cannot_convert_to_return_type_protocol;
  case CTP_EnumCaseRawValue:
    return diag::cannot_convert_raw_initializer_value;
  case CTP_DefaultParameter:
    return diag::cannot_convert_default_arg_value_protocol;
  case CTP_YieldByValue:
    return diag::cannot_convert_yield_value_protocol;
  case CTP_CallArgument:
    return diag::cannot_convert_argument_value_protocol;
  case CTP_ClosureResult:
    return diag::cannot_convert_closure_result_protocol;
  case CTP_ArrayElement:
    return diag::cannot_convert_array_element_protocol;
  case CTP_DictionaryKey:
    return diag::cannot_convert_dict_key_protocol;
  case CTP_DictionaryValue:
    return diag::cannot_convert_dict_value_protocol;
  case CTP_CoerceOperand:
    return diag::cannot_convert_coerce_protocol;
  case CTP_AssignSource:
    return diag::cannot_convert_assign_protocol;
  case CTP_SubscriptAssignSource:
    return diag::cannot_convert_subscript_assign_protocol;

  case CTP_ThrowStmt:
  case CTP_Unused:
  case CTP_CannotFail:
  case CTP_YieldByReference:
  case CTP_CalleeResult:
    break;
  }
  return None;
}

bool MissingGenericArgumentsFailure::hasLoc(GenericTypeParamType *GP) const {
  return GP->getDecl()->getStartLoc().isValid();
}

bool MissingGenericArgumentsFailure::diagnoseAsError() {
  llvm::SmallDenseMap<TypeRepr *, SmallVector<GenericTypeParamType *, 4>>
      scopedParameters;

  auto isScoped =
      findArgumentLocations([&](TypeRepr *base, GenericTypeParamType *GP) {
        scopedParameters[base].push_back(GP);
      });

  if (!isScoped)
    return diagnoseForAnchor(getAnchor(), Parameters);

  bool diagnosed = false;
  for (const auto &scope : scopedParameters)
    diagnosed |= diagnoseForAnchor(scope.first, scope.second);
  return diagnosed;
}

bool MissingGenericArgumentsFailure::diagnoseForAnchor(
    Anchor anchor, ArrayRef<GenericTypeParamType *> params) const {
  bool diagnosed = false;
  for (auto *GP : params)
    diagnosed |= diagnoseParameter(anchor, GP);

  if (!diagnosed)
    return false;

  auto *DC = getDeclContext();
  if (auto *SD = dyn_cast<SubscriptDecl>(DC)) {
    emitDiagnostic(SD, diag::note_call_to_subscript, SD->getFullName());
    return true;
  }

  if (auto *AFD = dyn_cast<AbstractFunctionDecl>(DC)) {
    if (isa<ConstructorDecl>(AFD)) {
      emitDiagnostic(AFD, diag::note_call_to_initializer);
    } else {
      emitDiagnostic(AFD,
                     AFD->isOperator() ? diag::note_call_to_operator
                                       : diag::note_call_to_func,
                     AFD->getFullName());
    }
    return true;
  }

  emitGenericSignatureNote(anchor);
  return true;
}

bool MissingGenericArgumentsFailure::diagnoseParameter(
    Anchor anchor, GenericTypeParamType *GP) const {
  auto &cs = getConstraintSystem();

  auto loc = anchor.is<Expr *>() ? anchor.get<Expr *>()->getLoc()
                                 : anchor.get<TypeRepr *>()->getLoc();

  auto *locator = getLocator();
  // Type variables associated with missing generic parameters are
  // going to be completely cut off from the rest of constraint system,
  // that's why we'd get two fixes in this case which is not ideal.
  if (locator->isForContextualType() &&
      llvm::count_if(cs.DefaultedConstraints,
                     [&GP](const ConstraintLocator *locator) {
                       return locator->getGenericParameter() == GP;
                     }) > 1) {
    return false;
  }

  if (auto *CE = dyn_cast<ExplicitCastExpr>(getRawAnchor())) {
    auto castTo = getType(CE->getCastTypeLoc());
    auto *NTD = castTo->getAnyNominal();
    emitDiagnostic(loc, diag::unbound_generic_parameter_cast, GP,
                   NTD ? NTD->getDeclaredType() : castTo);
  } else {
    emitDiagnostic(loc, diag::unbound_generic_parameter, GP);
  }

  if (!hasLoc(GP))
    return true;

  Type baseTyForNote;
  auto *DC = getDeclContext();

  if (auto *NTD =
          dyn_cast_or_null<NominalTypeDecl>(DC->getSelfNominalTypeDecl())) {
    baseTyForNote = NTD->getDeclaredType();
  } else if (auto *TAD = dyn_cast<TypeAliasDecl>(DC)) {
    baseTyForNote = TAD->getUnboundGenericType();
  } else {
    return true;
  }

  emitDiagnostic(GP->getDecl(), diag::archetype_declared_in_type, GP,
                 baseTyForNote);
  return true;
}

void MissingGenericArgumentsFailure::emitGenericSignatureNote(
    Anchor anchor) const {
  auto &cs = getConstraintSystem();
  auto &TC = getTypeChecker();
  auto *paramDC = getDeclContext();

  auto *GTD = dyn_cast<GenericTypeDecl>(paramDC);
  if (!GTD || anchor.is<Expr *>())
    return;

  auto getParamDecl =
      [](const ConstraintLocator *locator) -> GenericTypeParamDecl * {
    return locator->isForGenericParameter()
               ? locator->getGenericParameter()->getDecl()
               : nullptr;
  };

  llvm::SmallDenseMap<GenericTypeParamDecl *, Type> params;
  for (auto *typeVar : cs.getTypeVariables()) {
    auto *GP = typeVar->getImpl().getGenericParameter();
    if (!GP)
      continue;

    auto type = resolveType(typeVar);
    // This could happen if the diagnostic is used by CSDiag.
    if (type->is<TypeVariableType>())
      continue;

    // If this is one of the defaulted parameter types, attempt
    // to emit placeholder for it instead of `Any`.
    if (llvm::any_of(cs.DefaultedConstraints,
                     [&](const ConstraintLocator *locator) {
                       return GP->getDecl() == getParamDecl(locator);
                     }))
      continue;

    params[GP->getDecl()] = type;
  }

  auto getPreferredType = [&](const GenericTypeParamDecl *GP) -> Type {
    auto type = params.find(GP);
    return (type == params.end()) ? Type() : type->second;
  };

  SmallString<64> paramsAsString;
  auto baseType = anchor.get<TypeRepr *>();
  if (TC.getDefaultGenericArgumentsString(paramsAsString, GTD,
                                          getPreferredType)) {
    auto diagnostic = emitDiagnostic(
        baseType->getLoc(), diag::unbound_generic_parameter_explicit_fix);

    if (auto *genericTy = dyn_cast<GenericIdentTypeRepr>(baseType)) {
      // If some of the eneric arguments have been specified, we need to
      // replace existing signature with a new one.
      diagnostic.fixItReplace(genericTy->getAngleBrackets(), paramsAsString);
    } else {
      // Otherwise we can simply insert new generic signature.
      diagnostic.fixItInsertAfter(baseType->getEndLoc(), paramsAsString);
    }
  }
}

bool MissingGenericArgumentsFailure::findArgumentLocations(
    llvm::function_ref<void(TypeRepr *, GenericTypeParamType *)> callback) {
  using Callback = llvm::function_ref<void(TypeRepr *, GenericTypeParamType *)>;

  auto *anchor = getRawAnchor();

  TypeLoc typeLoc;
  if (auto *TE = dyn_cast<TypeExpr>(anchor))
    typeLoc = TE->getTypeLoc();
  else if (auto *ECE = dyn_cast<ExplicitCastExpr>(anchor))
    typeLoc = ECE->getCastTypeLoc();

  if (!typeLoc.hasLocation())
    return false;

  struct AssociateMissingParams : public ASTWalker {
    llvm::SmallVector<GenericTypeParamType *, 4> Params;
    Callback Fn;

    AssociateMissingParams(ArrayRef<GenericTypeParamType *> params,
                           Callback callback)
        : Params(params.begin(), params.end()), Fn(callback) {}

    bool walkToTypeReprPre(TypeRepr *T) override {
      if (Params.empty())
        return false;

      auto *ident = dyn_cast<ComponentIdentTypeRepr>(T);
      if (!ident)
        return true;

      auto *decl = dyn_cast_or_null<GenericTypeDecl>(ident->getBoundDecl());
      if (!decl)
        return true;

      auto *paramList = decl->getGenericParams();
      if (!paramList)
        return true;

      // There could a situation like `S<S>()`, so we need to be
      // careful not to point at first `S` because it has all of
      // its generic parameters specified.
      if (auto *generic = dyn_cast<GenericIdentTypeRepr>(ident)) {
        if (paramList->size() == generic->getNumGenericArgs())
          return true;
      }

      for (auto *candidate : paramList->getParams()) {
        auto result =
            llvm::find_if(Params, [&](const GenericTypeParamType *param) {
              return candidate == param->getDecl();
            });

        if (result != Params.end()) {
          Fn(ident, *result);
          Params.erase(result);
        }
      }

      // Keep walking.
      return true;
    }

    bool allParamsAssigned() const { return Params.empty(); }

  } associator(Parameters, callback);

  typeLoc.getTypeRepr()->walk(associator);
  return associator.allParamsAssigned();
}

void SkipUnhandledConstructInFunctionBuilderFailure::diagnosePrimary(
    bool asNote) {
  if (auto stmt = unhandled.dyn_cast<Stmt *>()) {
    emitDiagnostic(stmt->getStartLoc(),
                   asNote? diag::note_function_builder_control_flow
                         : diag::function_builder_control_flow,
                   builder->getFullName());
  } else {
    auto decl = unhandled.get<Decl *>();
    emitDiagnostic(decl,
                   asNote ? diag::note_function_builder_decl
                          : diag::function_builder_decl,
                   builder->getFullName());
  }
}

bool SkipUnhandledConstructInFunctionBuilderFailure::diagnoseAsError() {
  diagnosePrimary(/*asNote=*/false);
  emitDiagnostic(builder,
                 diag::kind_declname_declared_here,
                 builder->getDescriptiveKind(),
                 builder->getFullName());
  return true;
}

bool SkipUnhandledConstructInFunctionBuilderFailure::diagnoseAsNote() {
  diagnosePrimary(/*asNote=*/true);
  return true;
}

bool MutatingMemberRefOnImmutableBase::diagnoseAsError() {
  auto *anchor = getRawAnchor();
  auto baseExpr = getBaseExprFor(anchor);
  if (!baseExpr)
    return false;

  auto diagIDsubelt = diag::cannot_pass_rvalue_mutating_subelement;
  auto diagIDmember = diag::cannot_pass_rvalue_mutating;

  if (auto *storage = dyn_cast<AbstractStorageDecl>(Member)) {
    if (storage->isGetterMutating()) {
      diagIDsubelt = diag::cannot_pass_rvalue_mutating_getter_subelement;
      diagIDmember = diag::cannot_pass_rvalue_mutating_getter;
    }
  }

  auto &cs = getConstraintSystem();
  AssignmentFailure failure(baseExpr, cs, anchor->getLoc(), diagIDsubelt,
                            diagIDmember);
  return failure.diagnoseAsError();
}
