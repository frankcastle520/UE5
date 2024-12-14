// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Parser Public API

#pragma once

#include "uLang/Common/Text/TextRange.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Misc/Optional.h"

namespace Verse { namespace Vst { struct Node; } }

namespace uLang
{

class CAstNode;

/**
 * The severity of a diagnostic.
 */
enum class EDiagnosticSeverity : uint8_t
{
    Ok,
    Info,
    Warning,
    Error,
};

/**
 * Applies the given visitor macro for each diagnostic.
 */
#define VERSE_ENUM_DIAGNOSTICS(v) \
    v(0,    Ok,      Ok,                                            "Ok") \
    /* Warnings (2000-3000) */ \
    v(2000, Warning, WarnSemantic_UnreachableCode,                  "Unreachable code - previous expression is guaranteed to exit early.") \
    v(2001, Warning, WarnSemantic_EmptyBlock,                       "Expected one or more expressions in the code block but it is empty.") \
    v(2002, Warning, WarnSemantic_VoidFunctionReturningValue,       "This function's return type is void, but this return provides value other than false. The return value will be discarded.") \
    v(2003, Warning, WarnSemantic_ScopeQualifierShouldBeSuper,      "Class-scope qualifier (%s:) won't invoke the base-method. Perhaps (super:) was intended.") \
    v(2004, Warning, WarnSemantic_ScopeQualifierBeyondSuper,        "Class-scope qualifier (%s:) won't invoke the base-method. Explicitly calling ancestor-versions of overridden functions beyond the immediate base is not allowed.") \
    v(2005, Warning, WarnSemantic_CompatibilityRequirementMissing,  "") \
    v(2006, Warning, WarnSemantic_CompatibilityRequirementAmbiguous,"") \
    v(2007, Warning, WarnSemantic_CompatibilityRequirementType,     "") \
    v(2008, Warning, WarnSemantic_CompatibilityRequirementValue,    "") \
    v(2009, Warning, WarnSemantic_UseOfDeprecatedDefinition,        "") \
    v(2010, Warning, WarnSemantic_EpicPackageTargetingOldVersion,   "") \
    v(2011, Warning, WarnSemantic_DeprecatedFailureOnSetRhs,        "This expression in the right operand of 'set ... = ...' can fail, but the meaning of failure here will change in a future version of Verse. " \
                                                                    "To preserve the current meaning of this code in future versions of Verse, you should move the expression that can fail outside the 'set'.\n" \
                                                                    "For example, if you have the expression:\n" \
                                                                    "    set Variable = ExpressionThatMightFail[],\n" \
                                                                    "you can change it to the following two expressions to preserve the meaning:\n" \
                                                                    "    Value := ExpressionThatMightFail[]\n" \
                                                                    "    set Variable = Value") \
    v(2012, Warning, WarnSemantic_DeprecatedFailureInMapLiteralKey, "This expression in a key of a map literal can fail, but the meaning of failure here will change in a future version of Verse. " \
                                                                    "To preserve the current meaning of this code in future versions of Verse, you should move the expression that can fail outside the 'map' key expression.\n" \
                                                                    "For example, if you have the expression:\n" \
                                                                    "    Map := map{ExpressionThatMightFail[] => Value},\n" \
                                                                    "you can change it to the following two expressions to preserve the meaning:\n" \
                                                                    "    Key := ExpressionThatMightFail[]\n" \
                                                                    "    Map := map{Key => Value}") \
    v(2013, Warning, WarnSemantic_StricterErrorCheck,               "") \
    v(2015, Warning, WarnSemantic_ReservedFutureIdentifier,         "This identifier has been reserved in a future version of Verse. You should rename this identifier.") \
    v(2016, Warning, WarnSemantic_DeprecatedNonPublicStructField,   "Support for non-public struct fields is deprecated, and will become an error in a future version of Verse.") \
    v(2017, Warning, WarnSemantic_ContainerLookupAlwaysFails,       "This container lookup is unlikely to succeed. (Did you mean to use a different key?)") \
    v(2018, Warning, WarnSemantic_DeprecatedUniqueWithoutAllocates, "") \
    v(2100, Warning, WarnSystem_CannotReadPackage,                  "Error reading text file") \
    v(2200, Warning, WarnProject_EmptyProject,                      "Project contains no code.") \
    /* Errors (3000+) */ \
    v(3000, Error,   ErrSystem_CannotReadText,                      "Error reading text file") \
    v(3001, Error,   ErrSystem_CannotReadVst,                       "Error reading text file") \
    v(3002, Error,   ErrSystem_BadPackageFileName,                  "") \
    v(3003, Error,   ErrSystem_IllegalSubPackage,                   "") \
    v(3004, Error,   ErrSystem_DuplicateDigestFile,                 "") \
    v(3005, Error,   ErrSystem_InvalidModuleName,                   "") \
    v(3006, Error,   ErrSystem_CannotWriteText,                     "") \
    v(3007, Error,   ErrSystem_CannotCreateDirectory,               "") \
    v(3008, Error,   ErrSystem_CannotDeleteDirectory,               "") \
    v(3009, Error,   ErrSystem_BadModuleFileName,                   "") \
    v(3010, Error,   ErrSystem_InvalidModuleFile,                   "") \
    v(3011, Error,   ErrSystem_UnexpectedDigestFile,                "") \
    v(3012, Error,   ErrSystem_InconsistentNativeFileExtension,     "") \
    v(3013, Error,   ErrSystem_InvalidVerseVersion,                 "") \
    v(3014, Error,   ErrSystem_BadSnippetFileName,                  "") \
    /* Syntax errors */ \
    v(3100, Error,   ErrSyntax_InternalError,                       "Internal parser error") \
    v(3101, Error,   ErrSyntax_Unimplemented,                       "Feature is not yet implemented.") \
    v(3102, Error,   ErrSyntax_UnexpectedClauseTag,                 "Clause tag `[X]` is unexpected in this context.") \
    v(3103, Error,   ErrSyntax_ExpectedIfCondition,                 "Expected a condition block before `then` block while parsing `if`.") \
    v(3104, Error,   ErrSyntax_DanglingEquals,                      "Dangling '=' or missing ':type' for function definition! Hint: a function definition needs a type like 'f():int' or a definition 'f():int=2*2'.") \
    v(3105, Error,   ErrSyntax_ExpectedExpression,                  "Expected an expression and found an invalid character.") \
    v(3106, Error,   ErrSyntax_MalformedPackageFile,                "") \
    v(3107, Error,   ErrSyntax_MalformedProjectFile,                "") \
    v(3108, Error,   ErrSyntax_MalformedModuleFile,                 "") \
    v(3109, Error,   ErrSyntax_UnrecognizedFloatBitWidth,           "When using a float `f` suffix, the bit width must be specified explicitly. Use `f64` (the only currently acceptable usage) or omit a float suffix if a decimal part is present - i.e. `42f64` or `42.0`") \
    /* Semantic errors */ \
    v(3500, Error,   ErrSemantic_Internal,                          "Encountered an internal error (e.g. a malformed syntax node).") \
    v(3501, Error,   ErrSemantic_Placeholder,                       "A placeholder is present. Code cannot be executed.") \
    v(3502, Error,   ErrSemantic_Unimplemented,                     "Language feature is not yet implemented.") \
    v(3503, Error,   ErrSemantic_AmbiguousTypeVariable,             "") \
    v(3504, Error,   ErrSemantic_UnknownPackageDependency,          "") \
    v(3505, Error,   ErrSemantic_CircularPackageDependency,         "") \
    v(3506, Error,   ErrSemantic_UnknownIdentifier,                 "Unknown identifier %s.") \
    v(3507, Error,   ErrSemantic_DefinitionNotFromDependentPackage, "") \
    v(3508, Error,   ErrSemantic_UnexpectedNumberOfArguments,       "%s.%s expects %d arguments, but %d given.") \
    v(3509, Error,   ErrSemantic_IncompatibleArgument,              "The argument for parameter %s of %s.%s is of type %s which is incompatible with the expected type %s.") \
    v(3510, Error,   ErrSemantic_IncompatibleReturnValue,           "The return value of %s.%s is of type %s which is incompatible with the expected type %s.") \
    v(3511, Error,   ErrSemantic_IncompatibleFailure,               "`%s.%s` is invoked with incorrect failure bracketing style.") \
    v(3512, Error,   ErrSemantic_EffectNotAllowed,                  "This effect is not allowed in this context.") \
    v(3513, Error,   ErrSemantic_ExpectedFallibleExpression,        "Expected an expression that can fail.") \
    v(3514, Error,   ErrSemantic_RedefinitionOfReservedIdentifier,  "Cannot use reserved identifier `%s` as definition name.") \
    v(3515, Error,   ErrSemantic_MutableMissingType,                "Missing type for `^` or `var` definition.") \
    v(3516, Error,   ErrSemantic_ExpectedPointerType,               "Expected pointer type.") \
    v(3517, Error,   ErrSemantic_ExpectedDereferencedPointer,       "Expected a dereferenced pointer (e.g. Pointer^)") \
    v(3518, Error,   ErrSemantic_AmbiguousOverload,                 "Ambiguous function overload") \
    v(3519, Error,   ErrSemantic_ConcreteClassDataMemberLacksValue, "") \
    v(3520, Error,   ErrSemantic_ExpectedIterationIterable,         "The right hand side of an iteration mapping (lhs:rhs) must be something such as an array that can be iterated.") \
    v(3521, Error,   ErrSemantic_AsyncRequiresTaskClass,            "Definition of an async function found, but no task class exists.") \
    v(3522, Error,   ErrSemantic_ExpectedImmediateExpr,             "Found async expression (such as a coroutine call or concurrency primitive) when an immediate expression (such as a function call) was desired.\nMaybe put this code in a coroutine or wrap it in a `branch` or `spawn` to make it immediate?") \
    v(3523, Error,   ErrSemantic_IncorrectOverride,                 "Either override without override attribute, or override attribute without override.") \
    v(3524, Error,   ErrSemantic_ExpectIterable,                    "Needs something to iterate over") \
    v(3525, Error,   ErrSemantic_ExpectedSingleExpression,          "Expected a single expression, but found more than one.") \
    v(3526, Error,   ErrSemantic_MalformedConditional,              "Malformed conditional expression.") \
    v(3527, Error,   ErrSemantic_PrefixOpNoOperand,                 "Prefix operation without operand.") \
    v(3528, Error,   ErrSemantic_BinaryOpNoOperands,                "Binary operation without operands.") \
    v(3529, Error,   ErrSemantic_BinaryOpExpectedTwoOperands,       "Binary operation requires two operands.") \
    v(3530, Error,   ErrSemantic_ExpectedCoroutine,                 "A `branch` may only be used within the body of a coroutine.") \
    v(3531, Error,   ErrSemantic_UnicodeOutOfRange,                 "Unicode character is out of supported range.") \
    v(3532, Error,   ErrSemantic_AmbiguousDefinition,               "This symbol conflicts with another definition in scope.") \
    v(3534, Error,   ErrSemantic_InvalidPositionForReturn,          "Invalid position for return; return must not occur as a subexpression of another return") \
    v(3535, Error,   ErrSemantic_ReturnInFailureContext,            "Explicit return out of a failure context is not allowed") \
    v(3536, Error,   ErrSemantic_TupleElementIdxRange,              "Tuple element access expected an integer literal within the range 0-%i and got %s.") \
    v(3537, Error,   ErrSemantic_InvalidContextForUsing,            "'using' macro may only specify modules at module scope and local variables at local scope.") \
    v(3538, Error,   ErrSemantic_ExpectedAsyncExprs,                "Expected async expression") \
    v(3539, Error,   ErrSemantic_ExpectedAsyncExprNumber,           "Expected correct number of async expressions") \
    v(3540, Error,   ErrSemantic_MalformedParameter,                "Parameter must be a type spec.") \
    v(3541, Error,   ErrSemantic_MultipleReturnValuesUnsupported,   "Multiple return values are not supported") \
    v(3542, Error,   ErrSemantic_InvalidReturnType,                 "Return type is not valid") \
    v(3543, Error,   ErrSemantic_AccessLevelConflict,               "Conflicting access levels: [access levels]. Only one access level may be used or omit for default access.") \
    v(3544, Error,   ErrSemantic_MalformedMacro,                    "") \
    v(3545, Error,   ErrSemantic_UnrecognizedMacro,                 "") \
    v(3546, Error,   ErrSemantic_ExpectedIdentifier,                "Expected identifier") \
    v(3547, Error,   ErrSemantic_ExpectedType,                      "Expected type") \
    v(3548, Error,   ErrSemantic_UnexpectedIdentifier,              "Unexpected identifier") \
    v(3549, Error,   ErrSemantic_LhsNotDefineable,                  "The left hand side of this definition is an expression that cannot be defined.") \
    v(3550, Error,   ErrSemantic_CannotAccessInstanceMember,        "Can't access instance member `%s.%s` while in `%s` class scope.") \
    v(3551, Error,   ErrSemantic_MayNotSkipOutOfSpawn,              "May not skip out of `spawn`.") \
    v(3552, Error,   ErrSemantic_Unsupported, /* dupe of ErrSemantic_Unimplemented? */ "Features that are not implemented yet") \
    v(3553, Error,   ErrSemantic_InvalidAttribute,                  "Unable to create attribute expression") \
    v(3554, Error,   ErrSemantic_FloatLiteralOutOfRange,            "") \
    v(3555, Error,   ErrSemantic_IntegerLiteralOutOfRange,          "") \
    v(3556, Error,   ErrSemantic_MayNotSkipOutOfBranch,             "May not skip out of `branch`.") \
    v(3557, Error,   ErrSemantic_InterfaceOrClassInheritsFromItself,"Interface or class inherits from itself.") \
    v(3558, Error,   ErrSemantic_ExternalNotAllowed,                "external{} macro must not be used in regular Verse code. It is a placeholder allowed only in digests.") \
    v(3559, Error,   ErrSemantic_TooManyMacroClauses,               "Too many clauses following macro identifier.") \
    v(3560, Error,   ErrSemantic_ExpectedDefinition,                "Expected definition.") \
    v(3561, Error,   ErrSemantic_NativeMemberOfNonNativeClass,      "Native definitions may not be members of a non-native class") \
    v(3562, Error,   ErrSemantic_NonNativeSuperClass,               "Native classes must have a native super-class") \
    v(3563, Error,   ErrSemantic_NonNativeStructInNativeClass,      "Member `struct` contained in a native type must also be native") \
    v(3564, Error,   ErrSemantic_NonNativeStructInNativeFunction,   "`struct` parameters or results used in native functions must also be native") \
    v(3565, Error,   ErrSemantic_InvalidEffectDeclaration,          "") \
    v(3566, Error,   ErrSemantic_MayNotSkipOutOfDefer,              "May not skip out of defer.") \
    v(3567, Error,   ErrSemantic_DeferLocation,                     "A `defer` may not be used here - it must be used within a code block such as a routine, `do`, `if` then/else, `for`, `loop`, `branch` or `spawn` and it must be followed by one or more expressions that it executes after.") \
    v(3568, Error,   ErrSemantic_CannotOverrideFinalMember,         "Cannot declare instance data-member `CurrentClass.dataMember` because its `[SuperClass]` already has [an instance/a class] member with the same `final` attribute.") \
    v(3569, Error,   ErrSemantic_FinalSuperclass,                   "Class `[CurrentClass]` cannot be a subclass of the class `[SuperClass]` which has the `final` attribute.") \
    v(3570, Error,   ErrSemantic_UseOfExperimentalDefinition,       "") \
    v(3571, Error,   ErrSemantic_UnexpectedAbstractClass,           "Cannot instantiate class `[CurrentClass]` because it has the `abstract` attribute. Use a subclass of it.") \
    v(3572, Error,   ErrSemantic_ConstructorFunctionBody,           "Constructor function body must be an archetype instantiation.") \
    v(3573, Error,   ErrSemantic_ConstructorFunctionBodyResultType, "Constructor function result type must exactly match contained archetype instantiation.") \
    v(3574, Error,   ErrSemantic_NoSuperclass,                      "Class `[CurrentClass]` does not have a superclass.") \
    v(3575, Error,   ErrSemantic_CharLiteralDoesNotContainOneChar,  "Character literal doesn't contain exactly one character.") \
    v(3576, Error,   ErrSemantic_FailedResolveOfGenericsSignature,  "Failed to resolve the generic call signature from the call site context.") \
    v(3577, Error,   ErrSemantic_UnexpectedExpression,              "Unexpected expression") \
    v(3578, Error,   ErrSemantic_ExpectedExprs,                     "Expected one or more expressions in the code block body and found none.") \
    v(3579, Error,   ErrSemantic_InfiniteIteration,                 "To prevent infinite immediate iteration, `loop` must have one or more subexpressions that are either async (such as a coroutine) or a jump out (such as `break` or `return`).") \
    v(3580, Error,   ErrSemantic_ExpectedExternal,                  "external{} macro expected here since the code is a digest.") \
    v(3581, Error,   ErrSemantic_BreakNotInBreakableContext,        "This `break` is not in a breakable context. `break` may currently only be used inside a `loop`.") \
    v(3582, Error,   ErrSemantic_CannotInitDataMemberWithSideEffect,"Expressions with potential side effects cannot be used when defining data-members.") \
    v(3583, Error,   ErrSemantic_StructContainsItself,              "Structs may not contain themselves.") \
    v(3584, Error,   ErrSemantic_OnlyFunctionsInInterfaceBody,      "Expected function signature in interface definition body.") \
    v(3585, Error,   ErrSemantic_FunctionSignatureMustDeclareReturn,"Function declaration must declare return type or body.") \
    v(3586, Error,   ErrSemantic_ExpectedTypeDefinition,            "Expected type definition macro (e.g. `class`, `enum`, or `interface`).") \
    v(3587, Error,   ErrSemantic_InvalidScopePath,                  "Invalid scope path") \
    v(3588, Error,   ErrSemantic_AmbiguousIdentifier,               "") \
    v(3589, Error,   ErrSemantic_MultipleSuperClasses,              "Classes may only inherit from a single class") \
    v(3590, Error,   ErrSemantic_ExpectedInterfaceOrClass,          "Expected interface or class") \
    v(3591, Error,   ErrSemantic_AbstractFunctionInNonAbstractClass,"Non-abstract class inherits abstract function `%s` from `%s` but does not provide an implementation.") \
    v(3592, Error,   ErrSemantic_RedundantInterfaceInheritance,     "Redundant interface inheritance") \
    v(3593, Error,   ErrSemantic_Inaccessible,                      "") \
    v(3594, Error,   ErrSemantic_InvalidAccessLevel,                "Access levels protected and private are only allowed inside classes.") \
    v(3595, Error,   ErrSemantic_StructSuperType,                   "Structs may not inherit from any other types.") \
    v(3596, Error,   ErrSemantic_InvalidAttributeScope,             "Attribute does not have the right attribute scope.") \
    v(3597, Error,   ErrSemantic_NativeWithBody,                    "Functions declared native must not have a body or empty assignment.") \
    v(3598, Error,   ErrSemantic_UnexpectedAbstractFunction,        "Unexpected abstract function outside class or interface.") \
    v(3599, Error,   ErrSemantic_ExpectedInterface,                 "Expected interface.") \
    v(3600, Error,   ErrSemantic_MissingDataMemberInitializer,      "Archetype must initialize data member `%s`.") \
    v(3601, Error,   ErrSemantic_MissingValueInitializer,           "`%s` must be initialized with a default value.") \
    v(3602, Error,   ErrSemantic_OverrideSignatureMismatch,         "Signature of overriding function must match the signature of the overridden function.") \
    v(3603, Error,   ErrSemantic_StructFunction,                    "Structs may not contain functions.") \
    v(3604, Error,   ErrSemantic_AttributeNotAllowed,               "Valid attribute, but not allowed here.") \
    v(3605, Error,   ErrSemantic_NotEnoughMacroClauses,             "Not enough macro clauses") \
    v(3606, Error,   ErrSemantic_NominalTypeInAnonymousContext,     "Nominal type in anonymous context") \
    v(3607, Error,   ErrSemantic_StructMutable,                     "Structs may not contain mutable members.") \
    v(3608, Error,   ErrSemantic_ExpectedFunction,                  "Expected function") \
    v(3609, Error,   ErrSemantic_AmbiguousOverride,                 "Ambiguous function or data member override") \
    v(3610, Error,   ErrSemantic_InvalidContextForBlock,            "'block' macro may only be used at class or function scope.") \
    v(3611, Error,   ErrSemantic_UnexpectedQualifier,               "Qualifier is unexpected in this context") \
    v(3612, Error,   ErrSemantic_InvalidQualifier,                  "Invalid qualifier") \
    v(3613, Error,   ErrSemantic_ConflictingAttributeScope,         "Conflicting attribute scopes") \
    v(3614, Error,   ErrSemantic_ExpectedModule,                    "Expected module.") \
    v(3615, Error,   ErrSemantic_NoCasePatterns,                    "Case statement should have at least one pattern.") \
    v(3616, Error,   ErrSemantic_UnreachableCases,                  "Case statement has unreachable cases.") \
    v(3617, Error,   ErrSemantic_InvalidCasePattern,                "Case pattern must be a literal or `_`.") \
    v(3618, Error,   ErrSemantic_CaseTypeMismatch,                  "Case pattern has a different type than the case value.") \
    v(3619, Error,   ErrSemantic_EmptyValueClause,                  "Case expression must have a value.") \
    v(3620, Error,   ErrSemantic_BadCasePattern,                    "Case patterns must be of the form `a => b`.") \
    v(3621, Error,   ErrSemantic_SquareBracketFuncDefsDisallowed,   "Function definitions with `[` and `]` are disallowed; did you mean `(...)<decides>`?") \
    v(3622, Error,   ErrSemantic_EmptyOption,                       "option{} requires an argument; did you mean `false`?") \
    v(3623, Error,   ErrSemantic_MismatchedPartialAttributes,       "Attributes of partial module definition differ from attributes of related other partial definition.") \
    v(3624, Error,   ErrSemantic_MalformedImplicitParameter,        "Implicit parameter #%d is malformed.") \
    v(3625, Error,   ErrSemantic_DefaultMustBeNamed,                "Parameter #%d should be `?%s`. Default parameters must be prefixed with a question mark `?` to indicate that their name is required.") \
    v(3626, Error,   ErrSemantic_MayNotSkipOutOfArchetype,          "May not skip out of archetype instantiation.") \
    v(3627, Error,   ErrSemantic_IdentifierConstructorAttribute,    "<constructor> is only supported on constructor function invocations contained directly in archetype instantiations.") \
    v(3628, Error,   ErrSemantic_DuplicateNamedValueName,           "Duplicate named value name.") \
    v(3629, Error,   ErrSemantic_NamedMustFollowNamed,              "Parameter #%d must be named `?%s`. Once an earlier parameter is named (indicated with `?`) any parameters that follow must also be named.") \
    v(3630, Error,   ErrSemantic_NamedOrOptNonType,                 "Either `%s` should be a type or it is mistakenly being used as a `?named` argument. Also note that parameters do not need to be named with a `?` in the body of their function.") \
    v(3631, Error,   ErrSemantic_MultipleConstructorInvocations,    "Archetype instantiation may have no more than one constructor invocation.") \
    v(3632, Error,   ErrSemantic_AbstractConcreteClass,             "") \
    v(3633, Error,   ErrSemantic_ConcreteSuperclass,                "") \
    v(3634, Error,   ErrSemantic_UserPackageNotAllowedWithEpicPath, "") \
    v(3635, Error,   ErrSemantic_ConstructorInvocationResultType,   "Constructor invocation in archetype instantiation must be the same class or a superclass of the instantiated class.") \
    v(3636, Error,   ErrSemantic_ExtensionMethodWithoutContext,     "Calling extension method without context.") \
    v(3637, Error,   ErrSemantic_ReservedOperatorName,              "") \
    v(3638, Error,   ErrSemantic_LocalizesRhsMustBeString,          "Localized messages may only be initialized with a string literal.") \
    v(3639, Error,   ErrSemantic_LocalizesMustSpecifyType,          "Localized messages must specify the 'message' type.") \
    v(3640, Error,   ErrSemantic_NamedMustBeInApplicationContext,   "Named parameters only supported in a function application context") \
    v(3641, Error,   ErrSemantic_VarAttributeMustBeInClassOrModule, "Attributes on var only allowed inside a module or a class") \
    v(3642, Error,   ErrSemantic_DuplicateAccessLevel,              "Duplicate access levels: [access levels]. Only one access level may be used or omit for default access.") \
    v(3643, Error,   ErrSemantic_CompatibilityRequirementMissing,   "") \
    v(3644, Error,   ErrSemantic_CompatibilityRequirementAmbiguous, "") \
    v(3645, Error,   ErrSemantic_CompatibilityRequirementType,      "") \
    v(3646, Error,   ErrSemantic_CompatibilityRequirementAccess,    "") \
    v(3647, Error,   ErrSemantic_CompatibilityRequirementInitialize,"") \
    v(3648, Error,   ErrSemantic_CompatibilityRequirementValue,     "") \
    v(3649, Error,   ErrSemantic_CompatibilityRequirementFinal,     "") \
    v(3650, Error,   ErrSemantic_OverrideCantChangeAccessLevel,     "An overridden field cannot change the inherited access level") \
    v(3651, Error,   ErrSemantic_AttributeNotAllowedOnLocalVars,    "Attribute %s is not allowed on local variables.") \
    v(3652, Error,   ErrSemantic_LocalizesEscape,                   "Unrecognized escape character in localized message.") \
    v(3653, Error,   ErrSemantic_AmbiguousDefinitionDidYouMeanToSet,"") \
    v(3654, Error,   ErrSemantic_InvalidQualifierCombination,       "") \
    v(3655, Error,   ErrSemantic_TooLongIdentifier,                  "Identifier is too long") \
    v(3656, Error,   ErrSemantic_MutuallyExclusiveEffects,          "") \
    v(3657, Error,   ErrSemantic_MismatchedPartialQualifier,        "Qualifier of partial module definition differs from qualifier of related other partial definition.") \
    v(3658, Error,   ErrSemantic_BreakInFailureContext,             "`break` may not be used in a failure context.") \
    v(3659, Error,   ErrSemantic_UnknownIdentifier_WithUsing,       "Unknown identifier %s in '%s'. Did you forget to specify using { %s }?") \
    v(3660, Error,   ErrSemantic_LogicWithoutExpression,            "Empty logic{} is not allowed, need at least one expression.") \
    v(3661, Error,   ErrSemantic_AccessSpecifierNotAllowedOnLocal,  "Function local data definition '%s' is not allowed to use access level attributes (e.g. <public>, <internal>)") \
    v(3662, Error,   ErrSemantic_PersistableClassDataMemberNotPersistable, "") \
    v(3663, Error,   ErrSemantic_PersistableClassMustBeFinal,       "`persistable` class must be `final`.") \
    v(3664, Error,   ErrSemantic_PersistableClassMustNotBeUnique,   "`persistable` class must not be `unique`.") \
    v(3665, Error,   ErrSemantic_NotPersistableSuperclass,          "") \
    v(3666, Error,   ErrSemantic_ScopedUsingIdentAlreadyPresent,    "") \
    v(3667, Error,   ErrSemantic_ScopedUsingSelfSubtype,            "") \
    v(3668, Error,   ErrSemantic_ScopedUsingExistingSubtype,        "") \
    v(3669, Error,   ErrSemantic_ScopedUsingContextUnsupported,     "") \
    v(3670, Error,   ErrSemantic_IncorrectUseOfAttributeType,       "") \
    v(3671, Error,   ErrSemantic_CustomClassVarAccessorTypeMismatch,   "") \
    v(3672, Error,   ErrSemantic_LocalMustBeUsedAsQualifier,        "Currently, `(local:)` can only be used as a qualifier.") \
    v(3673, Error,   ErrSemantic_MissingFinalFieldInitializer,      "") \
    v(3674, Error,   ErrSemantic_FinalNonFieldDefinition,           "") \
    v(3675, Error,   ErrSemantic_ProfileOnlyAllowedInFunctions,     "") \
    v(3676, Error,   ErrSemantic_PackageRoleMismatch,               "") \
    v(3677, Error,   ErrSemantic_NativePackageDependencyCycle,      "") \
    /* Assembler errors */ \
    v(9000, Error,   ErrAssembler_Internal,                         "Assembler encountered an internal error") \
    v(9001, Error,   ErrAssembler_Unsupported,                      "Assembler cannot generate code because target architecture does not allow it") \
    v(9002, Error,   ErrAssembler_UnresolvedLinking,                "Unable to complete runtime link task.") \
    v(9005, Error,   ErrAssembler_AttributeError,                   "Error applying attribute.") \
    /* Digest generator errors */ \
    v(9101, Error,   ErrDigest_DisallowedUsing,                     "") \
    v(9102, Error,   ErrDigest_Unimplemented,                       "") \
    /* Toolchain errors */ \
    v(9200, Error,   ErrToolchain_Internal,                         "Toolchain encountered an internal error") \
    v(9201, Error,   ErrToolchain_Injection,                        "") \
    /* VPL errors */

/**
 * Possible errors, warnings, intermediary states and Okay diagnostic/analysis results.
 **/
enum class EDiagnostic : uint16_t
{
#define VISIT_DIAGNOSTIC(Code, Severity, EnumName, Description) EnumName,
    VERSE_ENUM_DIAGNOSTICS(VISIT_DIAGNOSTIC)
#undef VISIT_DIAGNOSTIC
};  // EDiagnostic

/**
 * Information about a diagnostic: a reference code, a severity, and a description.
 */
struct SDiagnosticInfo
{
    uint16_t ReferenceCode;
    EDiagnosticSeverity Severity;
    const char* Description;
};

VERSECOMPILER_API const SDiagnosticInfo& GetDiagnosticInfo(EDiagnostic Diagnostic);
VERSECOMPILER_API EDiagnostic GetDiagnosticFromReferenceCode(uint16_t ReferenceCode);

/**
 * Information about the result of a glitch.
 **/ 
struct SGlitchResult
{
    // Id for issue - static so it can be used in online searching/etc.
    EDiagnostic _Id;

    // String describing the result - either built custom at the time the issue was
    // encountered or generated after the fact using _Result and other info contained
    // in this structure and the associated SArgs and CParser objects.
    CUTF8String _Message;

    VERSECOMPILER_API SGlitchResult(EDiagnostic ResultId);
    SGlitchResult(EDiagnostic ResultId, CUTF8String&& Message) : _Id(ResultId), _Message(Move(Message)) {}
    
    const SDiagnosticInfo& GetInfo() const { return GetDiagnosticInfo(_Id); }
    bool IsError() const   { return GetInfo().Severity == EDiagnosticSeverity::Error; }
    bool IsWarning() const { return GetInfo().Severity == EDiagnosticSeverity::Warning; }
};

/**
 * Information about the location of a glitch.
 **/
struct SGlitchLocus
{
    // Path of the text snippet where the glitch occurred (this is usually the fully qualified path of a file)
    CUTF8String _SnippetPath;

    // The range of the code being parsed (relative to specific parse text) with
    // GetBegin() being the start of the most recent syntax element having issues
    // and GetEnd() being the best guess for the end of that syntax element (or the
    // end index of the code being parsed).
    STextRange _Range;

    // Row/column where the issue was encountered - generally inclusively in _Range below
    STextPosition _ResultPos;

    // Unique identifier for abstract syntax tree element where glitch occurred or 0
    // if general error without associated Vst element.
    uintptr_t _VstIdentifier = 0;

    SGlitchLocus() {}
    SGlitchLocus(const CUTF8String& SnippetPath, const STextRange& Range, uintptr_t VstIdentifier) : _SnippetPath(SnippetPath), _Range(Range), _ResultPos(Range.GetEnd()), _VstIdentifier(VstIdentifier) {}
    VERSECOMPILER_API SGlitchLocus(const Verse::Vst::Node* VstNode);
    VERSECOMPILER_API SGlitchLocus(const CAstNode* AstNode);

    /**
     * Returns a human-readable message describing a locus formatted for Visual Studio that
     * will jump to the file and the specified lines/columns when double-clicked in the VS
     * Output log. See https://docs.microsoft.com/en-us/cpp/build/formatting-the-output-of-a-custom-build-step-or-build-event
     *
     * filename(line#,column#, line#,column#])
     *
     * Example:
     * C:\sourcefile.Verse(134,10, 134,16)
     **/
    VERSECOMPILER_API CUTF8String AsFormattedString() const;
};

/**
 * Info describing a syntax error/warning that was encountered during parse.
 **/
struct SGlitch : public CSharedMix
{
    // Public data members

        // What happened
        SGlitchResult _Result;

        // Where it happened
        SGlitchLocus _Locus;

    // Methods 

        ULANG_FORCEINLINE SGlitch(SGlitchResult&& Result, SGlitchLocus&& Locus)
            : _Result(Move(Result)), _Locus(Move(Locus)) {}

        /**
         * Returns a human-readable message describing a Glitch formatted for Visual Studio that
         * will jump to the file and the specified lines/columns when double-clicked in the VS
         * Output log. See https://docs.microsoft.com/en-us/cpp/build/formatting-the-output-of-a-custom-build-step-or-build-event
         *
         * {filename(line#,column#, line#,column#]) | toolname} : [ any text ] {error | warning} code+number:localizable string [ any text ]
         *
         * Example:
         * C:\sourcefile.Verse(134,10, 134,16): Verse compile error V3510: The return value of `SomeClass.some_function` is of type `int` which is incompatible with the expected type `string`.
         **/
        ULANG_FORCEINLINE              CUTF8String        AsFormattedString() const  { return FormattedString(_Result._Message.AsCString(), _Locus._SnippetPath.AsCString(), _Locus._Range, _Result.GetInfo().Severity, _Result._Id); }
        VERSECOMPILER_API static CUTF8String FormattedString(const char* Message, const char* Path, const STextRange& Range, EDiagnosticSeverity Severity = EDiagnosticSeverity::Error, EDiagnostic Diagnostic = EDiagnostic::Ok);

};  // SGlitch

/** Convert a Row/Col offset to a byte offset from beginning of 'Source' */
VERSECOMPILER_API TOptional<int32_t> ScanToRowCol(CUTF8StringView const& Source, const STextPosition& Position);

/** Given a 'Range', return a corresponding string subview of 'Source' */
VERSECOMPILER_API CUTF8StringView TextRangeToStringView(CUTF8StringView const& Source, STextRange const& Range);

}  // namespace uLang
