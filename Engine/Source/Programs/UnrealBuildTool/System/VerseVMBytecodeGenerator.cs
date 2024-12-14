// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	namespace VerseVMBytecode
	{
		internal enum CppType
		{
			LabelOffset,
			ClassKind,
			Bool,
			Int,
		}

		internal enum Role
		{
			Use,
			Immediate,  // This means that the operand will be embedded in the bytecode itself.
			UnifyDef,
			ClobberDef,
		}

		internal enum Arity
		{
			Fixed,
			Optional,
			Variadic,
		}

		static class Extensions
		{
			public static string ToCpp(this Role TheRole)
			{
				switch (TheRole)
				{
					case Role.Use:
						return "EOperandRole::Use";
					case Role.Immediate:
						return "EOperandRole::Immediate";
					case Role.UnifyDef:
						return "EOperandRole::UnifyDef";
					case Role.ClobberDef:
						return "EOperandRole::ClobberDef";
					default:
						break;
				}
				return "#error \"Unknown role.\"";
			}

			public static string DefCppType(this Argument TheArg)
			{
				switch (TheArg.Role)
				{
					case Role.Use:
						return "FValueOperand";
					case Role.Immediate:
						return $"{TheArg.CppTypeName}";
					case Role.UnifyDef:  // fallthrough
					case Role.ClobberDef:
						return "FRegisterIndex";
					default:
						throw new ArgumentException("Unknown role.");
				}
			}

			public static string ToCpp(this CppType Type)
			{
				switch (Type)
				{
					case CppType.LabelOffset:
						return "FLabelOffset";
					case CppType.ClassKind:
						return "VClass::EKind";
					case CppType.Bool:
						return "bool";
					case CppType.Int:
						return "int32";
				}
				return "#error";
			}

			public static string ToCpp(this bool Bool)
			{
				return Bool ? "true" : "false";
			}

			public static IEnumerable<T> Yield<T>(this T item)
			{
				yield return item;
			}
		}

		internal class Argument
		{
			public string Name;
			public Role Role;
			public Arity Arity;

			/// <summary>
			/// If this is an immediate operand, the value will be embedded in the opcode itself.
			/// This should be set to the the string name of the underlying operand native type.
			/// </summary>
			public string CppTypeName;

			public Argument(string InName, Role InRole, Arity InArity, string InCppTypeName)
			{
				Name = InName;
				Role = InRole;
				Arity = InArity;
				CppTypeName = InCppTypeName;
			}
		}

		internal class Constant
		{
			public string Name;
			public CppType Type;
			public Arity Arity;

			public Constant(string InName, CppType InType, Arity InArity)
			{
				Name = InName;
				Type = InType;
				Arity = InArity;
			}

			public bool IsJump()
			{
				return Type == CppType.LabelOffset;
			}
		}

		internal class Instruction
		{
			public string Name;
			public List<Argument> Args = new List<Argument>();
			public List<Constant> Consts = new List<Constant>();
			public bool _CapturesEffectToken = false;
			public bool _CreatesNewReturnEffectToken = false;
			public bool _Suspends = false;
			public bool _Jumps = false;

			public Instruction(string _Name)
			{
				Name = _Name;
			}

			public string CppName => $"FOp{Name}";

			public string CppCapturesName => $"F{Name}SuspensionCaptures";

			public Instruction Arg(string InName, Role InRole, Arity InArity, string InCppTypeName)
			{
				Args.Add(new Argument(InName, InRole, InArity, InCppTypeName));
				return this;
			}

			public Instruction Arg(string InName, Role InRole, Arity InArity)
			{
				return Arg(InName, InRole, InArity, "");
			}

			public Instruction Arg(in string InName, in Role InRole)
			{
				return Arg(InName, InRole, Arity.Fixed);
			}

			public Instruction Const(string Name, CppType Type, Arity InArity)
			{
				Consts.Add(new Constant(Name, Type, InArity));
				return this;
			}

			public Instruction Const(string Name, CppType Type)
			{
				return Const(Name, Type, Arity.Fixed);
			}

			public Instruction Jump(string Name, Arity InArity)
			{
				_Jumps = true;
				return Const(Name, CppType.LabelOffset, InArity);
			}

			public Instruction Jump(string Name)
			{
				return Jump(Name, Arity.Fixed);
			}

			public Instruction CapturesEffectToken()
			{
				_CapturesEffectToken = true;
				return this;
			}

			public Instruction CreatesNewReturnEffectToken()
			{
				_CreatesNewReturnEffectToken = true;
				return this;
			}

			public Instruction Suspends()
			{
				_Suspends = true;
				return this;
			}
		}
	}
}

namespace UnrealBuildTool
{
	using VerseVMBytecode;

	/// <summary>
	/// Generates bytecode and bytecode helpers for the VerseVM.
	/// </summary>
	public class VerseVMBytecodeGenerator
	{
		readonly List<Instruction> Instructions = new List<Instruction>();

		Instruction Inst(string Name)
		{
			Instruction I = new Instruction(Name);
			Instructions.Add(I);
			return I;
		}

		static string Preamble()
		{
			StringBuilder S = new StringBuilder();
			S.Append("// Copyright Epic Games, Inc. All Rights Reserved.\n\n");
			S.Append("// WARNING: This code is autogenerated by VerseVMBytecodeGenerator.cs. Do not edit directly\n\n");
			S.Append("#pragma once\n\n");
			return S.ToString();
		}

		string EmitBytecodeMacroList()
		{
			StringBuilder S = new StringBuilder();
			S.Append(Preamble());

			S.Append("// IWYU pragma: private, include \"VVMBytecodeOps.h\"\n\n");

			S.Append("#define VERSE_ENUM_OPS(v) \\\n");
			foreach (Instruction Inst in Instructions)
			{
				S.Append($"    v({Inst.Name}) \\\n");
			}
			S.Append('\n');

			return S.ToString();
		}

		string EmitBytecodeAndCaptureDefs()
		{
			StringBuilder S = new StringBuilder();
			S.Append(Preamble());

			S.Append("// IWYU pragma: private, include \"VVMBytecodesAndCaptures.h\"\n\n");

			S.Append("namespace Verse {\n");

			// Emit bytecode structs.
			foreach (Instruction Inst in Instructions)
			{
				S.Append($"struct {Inst.CppName} : public FOp\n");
				S.Append("{\n");

				// Define fields
				foreach (Argument Arg in Inst.Args)
				{
					switch (Arg.Arity)
					{
						case Arity.Fixed:
						case Arity.Optional:
							if (Arg.Role == Role.Immediate)
							{
								S.Append($"    TWriteBarrier<{Arg.DefCppType()}> {Arg.Name};\n");
							}
							else
							{
								S.Append($"    {Arg.DefCppType()} {Arg.Name};\n");
							}
							break;
						case Arity.Variadic:
							if (Arg.Role == Role.Immediate)
							{
								S.Append($"    TOperandRange<TWriteBarrier<{Arg.DefCppType()}>> {Arg.Name};\n");
							}
							else
							{
								S.Append($"    TOperandRange<{Arg.DefCppType()}> {Arg.Name};\n");
							}
							break;
					}
				}
				foreach (Constant Const in Inst.Consts)
				{
					switch (Const.Arity)
					{
						case Arity.Fixed:
							S.Append($"    {Const.Type.ToCpp()} {Const.Name};\n");
							break;
						case Arity.Optional:
							S.Append($"    TOptional<{Const.Type.ToCpp()}> {Const.Name};\n");
							break;
						case Arity.Variadic:
							S.Append($"    TOperandRange<{Const.Type.ToCpp()}> {Const.Name};\n");
							break;
					}
				}
				S.Append('\n');

				S.Append($"    static constexpr EOpcode StaticOpcode = EOpcode::{Inst.Name};\n");
				S.Append($"    static constexpr bool bHasJumps = {Inst._Jumps.ToCpp()};\n\n");

				// Constructor
				bool bWriteBarrier = Inst.Args.Any(Arg => Arg.Role == Role.Immediate && Arg.Arity != Arity.Variadic);
				IEnumerable<string> Context = bWriteBarrier ? Extensions.Yield("FAccessContext Context") : Enumerable.Empty<string>();
				IEnumerable<string> Operands = Inst.Args.Select(Arg =>
				{
					switch (Arg.Arity)
					{
						case Arity.Fixed:
						case Arity.Optional:
							if (Arg.Role == Role.Immediate)
							{
								if (Arg.CppTypeName == "VValue")
								{
									return $"VValue {Arg.Name}";
								}
								else if (Arg.Arity == Arity.Fixed)
								{
									return $"{Arg.DefCppType()}& {Arg.Name}";
								}
								else
								{
									return $"{Arg.DefCppType()}* {Arg.Name}";
								}
							}
							else
							{
								return $"{Arg.DefCppType()} {Arg.Name}";
							}
						case Arity.Variadic:
							if (Arg.Role == Role.Immediate)
							{
								return $"TOperandRange<TWriteBarrier<{Arg.DefCppType()}>> {Arg.Name}";
							}
							else
							{
								return $"TOperandRange<{Arg.DefCppType()}> {Arg.Name}";
							}
						default:
							return "UnknownArity";
					}
				});
				IEnumerable<string> Constants = Inst.Consts.Select(Const =>
				{
					switch (Const.Arity)
					{
						case Arity.Fixed:
							return $"{Const.Type.ToCpp()} {Const.Name}";
						case Arity.Optional:
							return $"TOptional<{Const.Type.ToCpp()}> {Const.Name}";
						case Arity.Variadic:
							return $"TOperandRange<{Const.Type.ToCpp()}> {Const.Name}";
						default:
							return "UnknownArity";
					}
				});
				S.Append($"    {Inst.CppName}({String.Join(", ", Context.Concat(Operands).Concat(Constants))})\n");
				S.Append("        : FOp(StaticOpcode)\n");
				foreach (Argument Arg in Inst.Args)
				{
					string ContextString = (Arg.Role == Role.Immediate && Arg.Arity != Arity.Variadic) ? "Context, " : "";
					S.Append($"        , {Arg.Name}({ContextString}{Arg.Name})\n");
				}
				foreach (Constant Const in Inst.Consts)
				{
					S.Append($"        , {Const.Name}({Const.Name})\n");
				}
				S.Append("    {}\n\n");

				// Reflection methods 
				EmitReflectionMethods(S, Inst, false);
				S.Append('\n');

				S.Append("    template <typename FunctionType>\n");
				S.Append("    void ForEachJump(FunctionType&& Function)\n");
				S.Append("    {\n");
				foreach (Constant Const in Inst.Consts.Where(C => C.IsJump()))
				{
					S.Append($"        Function({Const.Name}, TEXT(\"{Const.Name}\"));\n");
				}
				S.Append("    }\n");

				S.Append("};\n");
				S.Append($"static_assert(alignof({Inst.CppName}) >= 8);\n\n");
			}

			// Emit captures structs.
			foreach (Instruction Inst in Instructions.Where(I => I._Suspends))
			{
				S.Append($"struct {Inst.CppCapturesName}\n");
				S.Append("{\n");

				// Generate the fields.
				foreach (Argument Arg in Inst.Args)
				{
					switch (Arg.Arity)
					{
						case Arity.Fixed:
						case Arity.Optional:
							if (Arg.Role == Role.Immediate)
							{
								S.Append($"    TWriteBarrier<{Arg.DefCppType()}> {Arg.Name};\n");
							}
							else
							{
								S.Append($"    TWriteBarrier<VValue> {Arg.Name};  \n");
							}
							break;
						case Arity.Variadic:
							if (Arg.Role == Role.Immediate)
							{
								S.Append($"    TArray<TWriteBarrier<{Arg.DefCppType()}>> {Arg.Name};\n");
							}
							else
							{
								S.Append($"    TArray<TWriteBarrier<VValue>> {Arg.Name};\n");
							}
							break;
					}
				}
				if (Inst._CapturesEffectToken)
				{
					S.Append($"    TWriteBarrier<VValue> EffectToken;\n");
				}
				if (Inst._CreatesNewReturnEffectToken)
				{
					S.Append($"    TWriteBarrier<VValue> ReturnEffectToken;\n");
				}
				foreach (Constant Const in Inst.Consts)
				{
					switch (Const.Arity)
					{
						case Arity.Fixed:
							S.Append($"    {Const.Type.ToCpp()} {Const.Name};\n");
							break;
						case Arity.Optional:
							S.Append($"    TOptional<{Const.Type.ToCpp()}> {Const.Name};\n");
							break;
						case Arity.Variadic:
							S.Append($"    TOperandRange<{Const.Type.ToCpp()}> {Const.Name};\n");
							break;
					}
				}
				S.Append('\n');

				// Generate the constructor.
				{
					IEnumerable<string> Context = Extensions.Yield("FAccessContext Context");
					IEnumerable<string> Operands = Inst.Args.Select(Arg =>
					{
						switch (Arg.Arity)
						{
							case Arity.Fixed:
							case Arity.Optional:
								if (Arg.Role == Role.Immediate)
								{
									if (Arg.CppTypeName == "VValue")
									{
										return $"VValue {Arg.Name}";
									}
									else if (Arg.Arity == Arity.Fixed)
									{
										return $"{Arg.DefCppType()}& {Arg.Name}";
									}
									else
									{
										return $"{Arg.DefCppType()}* {Arg.Name}";
									}
								}
								else
								{
									return $"VValue {Arg.Name}";
								}
							case Arity.Variadic:
								if (Arg.Role == Role.Immediate)
								{
									return $"TArray<TWriteBarrier<{Arg.DefCppType()}>>&& {Arg.Name}";
								}
								else
								{
									return $"TArray<TWriteBarrier<VValue>>&& {Arg.Name}";
								}
							default:
								return $"UnknownArity {Arg.Name}";
						}
					});
					IEnumerable<string> EffectToken = Inst._CapturesEffectToken ? Extensions.Yield("VValue EffectToken") : Enumerable.Empty<string>();
					IEnumerable<string> ReturnEffectToken = Inst._CreatesNewReturnEffectToken ? Extensions.Yield("VValue ReturnEffectToken") : Enumerable.Empty<string>();
					IEnumerable<string> Constants = Inst.Consts.Select(Const =>
					{
						switch (Const.Arity)
						{
							case Arity.Fixed:
								return $"{Const.Type.ToCpp()} {Const.Name}";
							case Arity.Optional:
								return $"TOptional<{Const.Type.ToCpp()}> {Const.Name}";
							case Arity.Variadic:
								return $"TOperandRange<{Const.Type.ToCpp()}> {Const.Name}";
							default:
								return $"UnknownArity {Const.Name}";
						}
					});
					S.Append($"    {Inst.CppCapturesName}({String.Join(", ", Context.Concat(Operands).Concat(EffectToken).Concat(ReturnEffectToken).Concat(Constants))})\n");
					string Prefix = ":";
					foreach (Argument Arg in Inst.Args)
					{
						switch (Arg.Arity)
						{
							case Arity.Fixed:
							case Arity.Optional:
								S.Append($"        {Prefix} {Arg.Name}(Context, {Arg.Name})\n");
								break;
							case Arity.Variadic:
								S.Append($"        {Prefix} {Arg.Name}(MoveTemp({Arg.Name}))\n");
								break;
						}
						Prefix = ",";
					}
					if (Inst._CapturesEffectToken)
					{
						S.Append($"        {Prefix} EffectToken(Context, EffectToken)\n");
						Prefix = ",";
					}
					if (Inst._CreatesNewReturnEffectToken)
					{
						S.Append($"        {Prefix} ReturnEffectToken(Context, ReturnEffectToken)\n");
						Prefix = ",";
					}
					foreach (Constant Const in Inst.Consts)
					{
						S.Append($"        {Prefix} {Const.Name}({Const.Name})\n");
						Prefix = ",";
					}
					S.Append("    {}\n");
				}
				S.Append('\n');

				// Generate the copy constructor.
				{
					S.Append($"    {Inst.CppCapturesName}(FAccessContext Context, const {Inst.CppCapturesName}& Other)\n");
					string Prefix = ":";
					foreach (Argument Arg in Inst.Args)
					{
						switch (Arg.Arity)
						{
							case Arity.Fixed:
							case Arity.Optional:
								S.Append($"        {Prefix} {Arg.Name}(Context, Other.{Arg.Name}.Get())\n");
								break;
							case Arity.Variadic:
								S.Append($"        {Prefix} {Arg.Name}(Other.{Arg.Name})\n");
								break;
						}
						Prefix = ",";
					}
					if (Inst._CapturesEffectToken)
					{
						S.Append($"        {Prefix} EffectToken(Context, Other.EffectToken.Get())\n");
						Prefix = ",";
					}
					if (Inst._CreatesNewReturnEffectToken)
					{
						S.Append($"        {Prefix} ReturnEffectToken(Context, Other.ReturnEffectToken.Get())\n");
						Prefix = ",";
					}
					foreach (Constant Const in Inst.Consts)
					{
						S.Append($"        {Prefix} {Const.Name}(Other.{Const.Name})\n");
						Prefix = ",";
					}
					S.Append("    {}\n");
				}
				S.Append('\n');

				EmitReflectionMethods(S, Inst, true);
				S.Append("};\n\n");
			}

			S.Append("} // namespace Verse\n");

			return S.ToString();
		}

		void EmitReflectionMethods(StringBuilder S, Instruction Inst, bool bIsSuspensionCapture)
		{
			S.Append("    template <typename FunctionType>\n");
			S.Append("    void ForEachOperand(FunctionType&& Function)\n");
			S.Append("    {\n");
			foreach (Argument Arg in Inst.Args)
			{
				S.Append($"        Function({Arg.Role.ToCpp()}, {Arg.Name}, TEXT(\"{Arg.Name}\"));\n");
			}
			if (bIsSuspensionCapture)
			{
				if (Inst._CapturesEffectToken)
				{
					S.Append($"        Function({Role.Use.ToCpp()}, EffectToken, TEXT(\"EffectToken\"));\n");
				}
				if (Inst._CreatesNewReturnEffectToken)
				{
					S.Append($"        Function({Role.UnifyDef.ToCpp()}, ReturnEffectToken, TEXT(\"ReturnEffectToken\"));\n");
				}
			}
			S.Append("    }\n");
		}

		string EmitMakeCapturesFunctions()
		{
			StringBuilder S = new StringBuilder();
			S.Append(Preamble());

			foreach (Instruction Inst in Instructions.Where(I => I._Suspends))
			{
				S.Append($"FORCEINLINE {Inst.CppCapturesName} MakeCaptures({Inst.CppName}& Op)\n{{\n");

				if (Inst._CapturesEffectToken)
				{
					S.Append("    VValue IncomingEffectToken = EffectToken.Get(Context);\n");
				}
				if (Inst._CreatesNewReturnEffectToken)
				{
					S.Append("    VValue ReturnEffectToken = VValue::Placeholder(VPlaceholder::New(Context, 0));\n");
					S.Append("    EffectToken.Set(Context, ReturnEffectToken);\n");
				}
				foreach (Argument Arg in Inst.Args.Where(A => A.Arity == Arity.Variadic))
				{
					if (Arg.Role == Role.Immediate)
					{
						S.Append($"    TArray<TWriteBarrier<{Arg.DefCppType()}>> Array{Arg.Name};\n");
						S.Append($"    for (auto& CurrentValue : GetOperands(Op.{Arg.Name}))\n");
						S.Append($"    {{\n");
						S.Append($"        Array{Arg.Name}.Add({{Context, *CurrentValue}});\n");
						S.Append($"    }}\n");
					}
					else
					{
						S.Append($"    TArray<TWriteBarrier<VValue>> Array{Arg.Name};\n");
						S.Append($"    for (auto& CurrentValue : GetOperands(Op.{Arg.Name}))\n");
						S.Append($"    {{\n");
						S.Append($"        Array{Arg.Name}.Add({{Context, GetOperand(CurrentValue)}});\n");
						S.Append($"    }}\n");
					}
				}
				S.Append($"    return {Inst.CppCapturesName}(Context");
				foreach (Argument Arg in Inst.Args)
				{
					switch (Arg.Arity)
					{
						case Arity.Fixed:
						case Arity.Optional:
							if (Arg.Role == Role.Immediate)
							{
								if (Arg.CppTypeName == "VValue")
								{
									S.Append($", Op.{Arg.Name}.Get()");
								}
								else
								{
									S.Append($", *Op.{Arg.Name}");
								}
							}
							else
							{
								S.Append($", GetOperand(Op.{Arg.Name})");
							}
							break;
						case Arity.Variadic:
							S.Append($", MoveTemp(Array{Arg.Name})");
							break;
					}
				}
				if (Inst._CapturesEffectToken)
				{
					S.Append(", IncomingEffectToken");
				}
				if (Inst._CreatesNewReturnEffectToken)
				{
					S.Append(", ReturnEffectToken");
				}
				foreach (Constant Const in Inst.Consts)
				{
					S.Append($", Op.{Const.Name}");
				}
				S.Append(");\n");

				S.Append("}\n\n");
			}

			return S.ToString();
		}

		string EmitCaptureSwitch()
		{
			StringBuilder S = new StringBuilder();
			S.Append(Preamble());

			S.Append("// IWYU pragma: private, include \"VVMCaptureSwitch.h\"\n\n");

			S.Append("namespace Verse {\n");
			S.Append("template <typename TFunc>\n");
			S.Append("void VBytecodeSuspension::CaptureSwitch(const TFunc& Func)\n");
			S.Append("{\n");
			S.Append("    switch (PC->Opcode)\n");
			S.Append("    {\n");
			foreach (Instruction Inst in Instructions.Where(I => I._Suspends))
			{
				S.Append($"        case EOpcode::{Inst.Name}:\n");
				S.Append("        {\n");
				S.Append($"            Func(GetCaptures<{Inst.CppCapturesName}>());\n");
				S.Append("            break;\n");
				S.Append("        }\n");
			}
			S.Append("        default:\n");
			S.Append("        {\n");
			S.Append("            V_DIE(\"Opcode doesn't have a captures\");\n");
			S.Append("            break;\n");
			S.Append("        }\n");
			S.Append("    }\n");
			S.Append("}\n");
			S.Append("} // namespace Verse\n");

			return S.ToString();
		}

		VerseVMBytecodeGenerator(ILogger Logger, DirectoryReference GenDirectory)
		{
			DefineOps();

			Action<string, Func<string>> GenFile = (FileName, Method) =>
			{
				FileReference File = FileReference.Combine(GenDirectory, FileName);
				bool bWritten = FileReference.WriteAllTextIfDifferent(File, Method());
				Logger.LogDebug($"\tWriting out generated header file. Changed:{bWritten} Path:'{File}'");
			};

			GenFile("VVMBytecodeOps.gen.h", EmitBytecodeMacroList);
			GenFile("VVMBytecodesAndCaptures.gen.h", EmitBytecodeAndCaptureDefs);
			GenFile("VVMMakeCapturesFuncs.gen.h", EmitMakeCapturesFunctions);
			GenFile("VVMCaptureSwitch.gen.h", EmitCaptureSwitch);
		}

		/// <summary>
		/// Entrypoint to generate the bytecode. Generated code will go in Directory.
		/// </summary>
		public static void Generate(ILogger Logger, DirectoryReference Directory)
		{
			Logger.LogDebug($"VerseVMBytecodeGenerator.Generate, generating CPP headers in: '{Directory}'");
			new VerseVMBytecodeGenerator(Logger, Directory);
		}

		void DefineOps()
		{
			string[] BinOps =
			{
				"Add", "Sub", "Mul", "Div", "Mod"
			};
			foreach (string Op in BinOps)
			{
				Inst(Op)
					.Arg("Dest", Role.UnifyDef)
					.Arg("LeftSource", Role.Use)
					.Arg("RightSource", Role.Use)
					.Suspends();
			}

			Inst("MutableAdd")
				.Arg("Dest", Role.UnifyDef)
				.Arg("LeftSource", Role.Use)
				.Arg("RightSource", Role.Use)
				.Suspends();

			string[] UnaryOps =
			{
				"Neg",
				"Query"
			};
			foreach (string Op in UnaryOps)
			{
				Inst(Op)
					.Arg("Dest", Role.UnifyDef)
					.Arg("Source", Role.Use)
					.Suspends();
			}

			Inst("Err");

			Inst("Move")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Source", Role.Use);

			Inst("Reset")
				.Arg("Dest", Role.ClobberDef);

			Inst("Jump")
				.Jump("JumpOffset");
			Inst("JumpIfInitialized")
				.Arg("Source", Role.Use)
				.Jump("JumpOffset");
			Inst("Switch")
				.Arg("Which", Role.Use)
				.Jump("JumpOffsets", Arity.Variadic);

			// These labels are needed for lenient execution.
			// On success, EndFailureContext falls through.
			// On failure, it jumps to OnFailure.
			// When there are still unresolved suspensions in this failure context,
			// we jump to "Done" to continue lenient execution.
			Inst("BeginFailureContext")
				.Jump("OnFailure");
			Inst("EndFailureContext")
				.Jump("Done");

			Inst("BeginTask")
				.Arg("Dest", Role.UnifyDef)
				.Const("bAttached", CppType.Bool)
				.Jump("OnYield");
			Inst("EndTask")
				.Arg("Write", Role.ClobberDef, Arity.Optional)
				.Arg("Signal", Role.Use, Arity.Optional)
				.Arg("Value", Role.Use);

			Inst("NewSemaphore")
				.Arg("Dest", Role.UnifyDef);
			Inst("WaitSemaphore")
				.Arg("Source", Role.Use)
				.Const("Count", CppType.Int);

			Inst("Call")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Callee", Role.Use)
				.Arg("Arguments", Role.Use, Arity.Variadic)
				.CapturesEffectToken()
				.CreatesNewReturnEffectToken()
				.Suspends();

			Inst("CallNamed")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Callee", Role.Use)
				.Arg("Arguments", Role.Use, Arity.Variadic)
				.Arg("NamedArguments", Role.Immediate, Arity.Variadic, "VUniqueString")
				.Arg("NamedArgumentVals", Role.Use, Arity.Variadic)
				.CapturesEffectToken()
				.CreatesNewReturnEffectToken()
				.Suspends();

			Inst("Return")
				.Arg("Value", Role.Use);
			Inst("ResumeUnwind");

			Inst("NewVar")
				.Arg("Dest", Role.UnifyDef);
			Inst("VarGet")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Var", Role.Use)
				.CapturesEffectToken()
				.Suspends();
			Inst("VarSet")
				.Arg("Var", Role.Use)
				.Arg("Value", Role.Use)
				.CapturesEffectToken()
				.Suspends();

			Inst("Freeze")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Value", Role.Use)
				.CapturesEffectToken()
				.Suspends();

			Inst("Melt")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Value", Role.Use)
				.CapturesEffectToken()
				.Suspends();

			Inst("Length")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Container", Role.Use)
				.Suspends();

			Inst("CallSet")
				.Arg("Container", Role.Use)
				.Arg("Index", Role.Use)
				.Arg("ValueToSet", Role.Use)
				.CapturesEffectToken()
				.Suspends();

			Inst("NewArray")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Values", Role.Use, Arity.Variadic)
				.Suspends();

			Inst("NewMutableArray")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Values", Role.Use, Arity.Variadic)
				.Suspends();

			Inst("NewMutableArrayWithCapacity")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Size", Role.Use)
				.Suspends();

			Inst("ArrayAdd")
				.Arg("Container", Role.Use)
				.Arg("ValueToAdd", Role.Use)
				.CapturesEffectToken()
				.Suspends();

			// This in place converts a VMutableArray into a VArray.
			// This can get away with being non-transactional because we
			// call it on data structures before they become observable
			// to user code.
			Inst("InPlaceMakeImmutable")
				.Arg("Container", Role.Use)
				.Suspends();

			Inst("NewOption")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Value", Role.Use)
				.Suspends();

			Inst("NewMap")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Keys", Role.Use, Arity.Variadic)
				.Arg("Values", Role.Use, Arity.Variadic)
				.Suspends();

			Inst("MapKey")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Map", Role.Use)
				.Arg("Index", Role.Use)
				.Suspends();

			Inst("MapValue")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Map", Role.Use)
				.Arg("Index", Role.Use)
				.Suspends();

			Inst("NewClass")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Package", Role.Immediate, Arity.Fixed, "VPackage")
				.Arg("Name", Role.Immediate, Arity.Fixed, "VArray")
				.Arg("UEMangledName", Role.Immediate, Arity.Fixed, "VArray")
				.Arg("ImportStruct", Role.Immediate, Arity.Optional, "VValue")
				.Const("bNative", CppType.Bool)
				.Const("ClassKind", CppType.ClassKind)
				.Arg("Inherited", Role.Use, Arity.Variadic)
				.Arg("Constructor", Role.Immediate, Arity.Fixed, "VConstructor")
				.Suspends();

			Inst("NewObject")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Class", Role.Use)
				.Arg("Fields", Role.Immediate, Arity.Fixed, "VUniqueStringSet")
				.Arg("Values", Role.Use, Arity.Variadic)
				.Suspends();

			Inst("LoadField")
				.Arg("Dest", Role.UnifyDef)
				.Arg("Object", Role.Use)
				.Arg("Name", Role.Immediate, Arity.Fixed, "VUniqueString")
				.Suspends();

			Inst("LoadFieldFromSuper")
				.Arg("Dest", Role.UnifyDef)
				// This is used to store `(super:)` and in the future, anything else that a generic lambda capture would encompass.
				.Arg("Scope", Role.Use)
				.Arg("Self", Role.Use)
				.Arg("Name", Role.Immediate, Arity.Fixed, "VUniqueString")
				.Suspends();

			Inst("UnifyField")
				.Arg("Object", Role.Use)
				.Arg("Name", Role.Immediate, Arity.Fixed, "VUniqueString")
				.Arg("Value", Role.Use)
				.Suspends();

			Inst("SetField")
				.Arg("Object", Role.Use)
				.Arg("Name", Role.Immediate, Arity.Fixed, "VUniqueString")
				.Arg("Value", Role.Use)
				.CapturesEffectToken()
				.Suspends();

			string[] ComparisonOps =
			{
				"Neq", "Lt", "Lte", "Gt", "Gte"
			};
			foreach (string Op in ComparisonOps)
			{
				Inst(Op)
					.Arg("Dest", Role.UnifyDef)
					.Arg("LeftSource", Role.Use)
					.Arg("RightSource", Role.Use)
					.Suspends();
			}
		}
	}
}

