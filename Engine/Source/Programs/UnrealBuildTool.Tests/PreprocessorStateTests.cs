// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using UnrealBuildTool;

namespace UnrealBuildToolTests
{
	[TestClass]
	public class PreprocessorStateTests
	{
		[TestMethod]
		public void Run()
		{
			PreprocessorState baseState = new PreprocessorState();
			baseState.DefineMacro(ParseMacro("FOO", null, "123"));
			baseState.DefineMacro(ParseMacro("BAR", new List<string> { "Arg1", "Arg2" }, "Arg1 + Arg2"));
			Assert.AreEqual("m BAR(Arg1, Arg2)=Arg1 + Arg2\nm FOO=123\n", FormatState(baseState));

			// Modify the state and revert it
			{
				PreprocessorState state = new PreprocessorState(baseState);
				Assert.AreEqual(FormatState(baseState), FormatState(state));

				PreprocessorTransform transform1 = state.BeginCapture();
				state.UndefMacro(Identifier.FindOrAdd("FOO"));
				state.DefineMacro(ParseMacro("FOO2", null, "FOO()"));
				state.PushBranch(PreprocessorBranch.Active);
				Assert.AreEqual("b Active\nm BAR(Arg1, Arg2)=Arg1 + Arg2\nm FOO2=FOO()\n", FormatState(state));
				Assert.AreEqual("+b Active\n+m FOO undef\n+m FOO2=FOO()\n", FormatTransform(transform1));

				PreprocessorState state2 = new PreprocessorState(baseState);
				state2.TryToApply(transform1);
				Assert.AreEqual(FormatState(state), FormatState(state2));

				PreprocessorTransform transform2 = state.BeginCapture();
				state.PopBranch();
				state.UndefMacro(Identifier.FindOrAdd("FOO"));
				state.DefineMacro(ParseMacro("FOO", null, "123"));
				state.UndefMacro(Identifier.FindOrAdd("FOO2"));
				Assert.AreEqual(FormatState(baseState), FormatState(state));
				Assert.AreEqual("-b Active\n+m FOO=123\n+m FOO2 undef\n", FormatTransform(transform2));

				state2.TryToApply(transform2);
				Assert.AreEqual(FormatState(baseState), FormatState(state2));
			}

			// Check the tracking of branches
			{
				PreprocessorState state = new PreprocessorState(baseState);
				PreprocessorTransform transform1 = state.BeginCapture();
				state.IsCurrentBranchActive();
				Assert.AreEqual("=b Active\n", FormatTransform(transform1));

				state.PushBranch(PreprocessorBranch.Active);
				PreprocessorTransform transform2 = state.BeginCapture();
				state.IsCurrentBranchActive();
				state.PopBranch();
				state.EndCapture();
				Assert.AreEqual("-b Active\n", FormatTransform(transform2));

				state.PushBranch(0);
				Assert.AreEqual("False", state.CanApply(transform2).ToString());

				state.PopBranch();
				state.PushBranch(PreprocessorBranch.Active);
				Assert.AreEqual("True", state.CanApply(transform2).ToString());
			}
		}

		static string FormatState(PreprocessorState state)
		{
			StringBuilder result = new StringBuilder();
			foreach (PreprocessorBranch branch in state.CurrentBranches)
			{
				result.AppendFormat("b {0}\n", branch);
			}
			foreach (PreprocessorMacro macro in state.CurrentMacros.OrderBy(x => x.Name))
			{
				result.AppendFormat("m {0}\n", macro.ToString().TrimEnd());
			}
			return result.ToString();
		}

		static string FormatTransform(PreprocessorTransform transform)
		{
			StringBuilder result = new StringBuilder();
			if (transform.RequireTopmostActive.HasValue)
			{
				result.AppendFormat("=b {0}\n", transform.RequireTopmostActive.Value ? "Active" : "0");
			}
			foreach (PreprocessorBranch branch in transform.RequiredBranches)
			{
				result.AppendFormat("-b {0}\n", branch);
			}
			foreach (PreprocessorBranch branch in transform.NewBranches)
			{
				result.AppendFormat("+b {0}\n", branch);
			}
			foreach (KeyValuePair<Identifier, PreprocessorMacro?> macro in transform.RequiredMacros)
			{
				if (macro.Value == null)
				{
					result.AppendFormat("=m {0} undef\n", macro.Key);
				}
				else
				{
					result.AppendFormat("=m {0}\n", macro.Value.ToString().TrimEnd());
				}
			}
			foreach (KeyValuePair<Identifier, PreprocessorMacro?> macro in transform.NewMacros)
			{
				if (macro.Value == null)
				{
					result.AppendFormat("+m {0} undef\n", macro.Key);
				}
				else
				{
					result.AppendFormat("+m {0}\n", macro.Value.ToString().TrimEnd());
				}
			}
			return result.ToString();
		}

		static PreprocessorMacro ParseMacro(string name, List<string>? parameters, string value)
		{
			List<Token> tokens = new List<Token>();

			using TokenReader reader = new TokenReader(value);
			while (reader.MoveNext())
			{
				tokens.Add(reader.Current);
			}

			if (parameters == null)
			{
				return new PreprocessorMacro(Identifier.FindOrAdd(name), null, tokens);
			}
			else
			{
				return new PreprocessorMacro(Identifier.FindOrAdd(name), parameters.Select(x => Identifier.FindOrAdd(x)).ToList(), tokens);
			}
		}
	}
}
