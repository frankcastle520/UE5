// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Build.h"
#include "Misc/MemStack.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

#include "RHIFwd.h"
#include "RHIPipeline.h"
#include "MultiGPU.h"

#ifndef WITH_RHI_BREADCRUMBS
#define WITH_RHI_BREADCRUMBS ((UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT) || WITH_PROFILEGPU)
#endif

// Whether to emit Unreal Insights breadcrumb events on threads involved in RHI command list recording and execution.
#define RHI_BREADCRUMBS_EMIT_CPU (WITH_RHI_BREADCRUMBS && CPUPROFILERTRACE_ENABLED && 1)

// Whether to store the filename and line number of each RHI breadcrumb and emit this data to Insights.
#define RHI_BREADCRUMBS_EMIT_LOCATION (WITH_RHI_BREADCRUMBS && (CPUPROFILERTRACE_ENABLED || GPUPROFILERTRACE_ENABLED) && 1)

#if WITH_RHI_BREADCRUMBS

	//
	// Holds the filename and line number location of the RHI breadcrumb in source.
	//
	struct FRHIBreadcrumbData_Location
	{
#if RHI_BREADCRUMBS_EMIT_LOCATION
		ANSICHAR const* File;
		uint32 Line;
#endif

		FRHIBreadcrumbData_Location(ANSICHAR const* File, uint32 Line)
#if RHI_BREADCRUMBS_EMIT_LOCATION
			: File(File)
			, Line(Line)
#endif
		{}
	};

	//
	// Holds both a stats system ID, and a CSV profiler ID.
	// The computed stat value is emitted to both "stat gpu" and the CSV profiler.
	//
	struct FRHIBreadcrumbData_Stats
	{
#if STATS
		TStatId StatId {};
#endif
#if CSV_PROFILER_STATS
		FName CsvStat = NAME_None;
#endif

		FRHIBreadcrumbData_Stats(TStatId InStatId, FName InCsvStat)
		{
#if STATS
			StatId = InStatId;
#endif
#if CSV_PROFILER_STATS
			CsvStat = InCsvStat;
#endif
		}

		bool ShouldComputeStat() const
		{
#if STATS
			return StatId.IsValidStat();
#elif CSV_PROFILER_STATS
			return CsvStat != NAME_None;
#else
			return false;
#endif
		}

		bool operator == (FRHIBreadcrumbData_Stats const& RHS) const
		{
#if STATS
			return StatId == RHS.StatId;
#elif CSV_PROFILER_STATS
			return CsvStat == RHS.CsvStat;
#else
			return true;
#endif
		}

		friend uint32 GetTypeHash(FRHIBreadcrumbData_Stats const& Stats)
		{
#if STATS
			return GetTypeHash(Stats.StatId);
#elif CSV_PROFILER_STATS
			return GetTypeHash(Stats.CsvStat);
#else
			return 0;
#endif
		}
	};

	//
	// Container for extra profiling-related data for each RHI breadcrumb.
	//
	class FRHIBreadcrumbData
		// Use inheritance for empty-base-optimization.
		: public FRHIBreadcrumbData_Location
		, public FRHIBreadcrumbData_Stats
	{
	public:
		FRHIBreadcrumbData(ANSICHAR const* File, uint32 Line, TStatId StatId, FName CsvStat)
			: FRHIBreadcrumbData_Location(File, Line)
			, FRHIBreadcrumbData_Stats(StatId, CsvStat)
		{}
	};

	class FRHIBreadcrumb;
	class FRHIBreadcrumbAllocator;
	struct FRHIBreadcrumbRange;

	struct FRHIBreadcrumbState
	{
		struct FPipeline
		{
			uint32 MarkerIn = 0, MarkerOut = 0;
		};

		struct FDevice
		{
			TRHIPipelineArray<FPipeline> Pipelines;
		};

		TStaticArray<FDevice, MAX_NUM_GPUS> Devices{ InPlace };

		struct FQueueID
		{
			uint32 DeviceIndex;
			ERHIPipeline Pipeline;

			bool operator == (FQueueID const& RHS) const
			{
				return DeviceIndex == RHS.DeviceIndex && Pipeline == RHS.Pipeline;
			}

			bool operator != (FQueueID const& RHS) const
			{
				return !(*this == RHS);
			}

			friend uint32 GetTypeHash(FQueueID const& ID)
			{
				return HashCombineFast(GetTypeHash(ID.DeviceIndex), GetTypeHash(ID.Pipeline));
			}
		};

		RHI_API void DumpActiveBreadcrumbs(TMap<FQueueID, TArray<FRHIBreadcrumbRange>> const& QueueRanges) const;
	};

	struct FRHIBreadcrumbNode
	{
		FRHIBreadcrumb const& Name;

	private:
		FRHIBreadcrumbNode* Parent = Sentinel;
		FRHIBreadcrumbNode* ListLink = nullptr;
		TStaticArray<FRHIBreadcrumbNode*, uint32(ERHIPipeline::Num)> NextPtrs { InPlace, nullptr };

	public:
		FRHIBreadcrumbAllocator* const Allocator = nullptr;

#if DO_CHECK
		// Used to track use of this breadcrumb on each GPU pipeline. Breadcrumbs can only be begun/ended once per pipe.
		std::atomic<std::underlying_type_t<ERHIPipeline>> BeginPipes = std::underlying_type_t<ERHIPipeline>(ERHIPipeline::None);
		std::atomic<std::underlying_type_t<ERHIPipeline>> EndPipes = std::underlying_type_t<ERHIPipeline>(ERHIPipeline::None);
#endif

		FRHIBreadcrumbNode(FRHIBreadcrumbAllocator& Allocator, FRHIBreadcrumb const& Name)
			: Name(Name)
			, Allocator(&Allocator)
		{}

		FRHIBreadcrumbNode* GetParent() const
		{
			return Parent;
		}

		FRHIBreadcrumbNode*      & GetNextPtr(ERHIPipeline Pipeline)       { return NextPtrs[GetRHIPipelineIndex(Pipeline)]; }
		FRHIBreadcrumbNode* const& GetNextPtr(ERHIPipeline Pipeline) const { return NextPtrs[GetRHIPipelineIndex(Pipeline)]; }

		inline void SetParent(FRHIBreadcrumbNode* Node);

		inline void BeginCPU() const;
		inline void EndCPU() const;

		// Calls BeginCPU() on all the breadcrumb nodes between the root and the specified node.
		static inline void WalkIn(FRHIBreadcrumbNode* Node);

		// Calls EndCPU() on all the breadcrumb nodes between the specified node and the root.
		static inline void WalkOut(FRHIBreadcrumbNode* Node);

		// ----------------------------------------------------
		// Debug logging / crash reporting
		// ----------------------------------------------------

	#if WITH_ADDITIONAL_CRASH_CONTEXTS
		// Logs the stack of breadcrumbs to the crash context, starting from the current node.
		RHI_API void WriteCrashData(struct FCrashContextExtendedWriter& Writer, const TCHAR* ThreadName) const;
	#endif

		RHI_API FString GetFullPath() const;

		static RHI_API FRHIBreadcrumbNode const* FindCommonAncestor(FRHIBreadcrumbNode const* Node0, FRHIBreadcrumbNode const* Node1);
		static RHI_API uint32 GetLevel(FRHIBreadcrumbNode const* Node);

		// A constant pointer value representing an undefined node. Used as the parent pointer for nodes in sub-trees
		// that haven't been attached to the root yet, specifically to be distinct from nullptr which is the root.
		static RHI_API FRHIBreadcrumbNode* const Sentinel;

	private:
		// Constructor for the sentinel value
		FRHIBreadcrumbNode();
		static FRHIBreadcrumbNode SentinelNode;

		friend struct FRHIBreadcrumbList;
	};

	class FRHIBreadcrumbAllocatorArray : public TArray<TSharedRef<class FRHIBreadcrumbAllocator>, TInlineAllocator<2>>
	{
	public:
		inline void AddUnique(FRHIBreadcrumbAllocator* Allocator);
	};

	class FRHIBreadcrumbAllocator : public TSharedFromThis<FRHIBreadcrumbAllocator>
	{
		template <typename...>
		friend class TRHIBreadcrumb;
		friend FRHIBreadcrumbNode;

		FMemStackBase Inner;
		FRHIBreadcrumbAllocatorArray Parents;

		template <typename TType, typename... TArgs>
		TType* Alloc(TArgs&&... Args)
		{
			return new (Inner.Alloc(sizeof(TType), alignof(TType))) TType(Forward<TArgs>(Args)...);
		}

	public:
		FRHIBreadcrumbAllocatorArray const& GetParents() const { return Parents; }

		template<size_t N, typename... TArgs>
		inline FRHIBreadcrumbNode* AllocBreadcrumb(FRHIBreadcrumbData&& Stat, TCHAR const(&FormatString)[N], TArgs&&... Args);

		inline void* Alloc(uint32 Size, uint32 Align)
		{
			return Inner.Alloc(Size, Align);
		}

#if ENABLE_RHI_VALIDATION
		// Used by RHI validation for circular reference detection.
		bool bVisited = false;
#endif
	};

	inline void FRHIBreadcrumbAllocatorArray::AddUnique(FRHIBreadcrumbAllocator* Allocator)
	{
		for (TSharedRef<FRHIBreadcrumbAllocator> const& Existing : *this)
		{
			if (Allocator == &Existing.Get())
			{
				return;
			}
		}

		Add(Allocator->AsShared());
	}

	//
	// A linked list of breadcrumb nodes.
	// Nodes may only be attached to one list at a time.
	//
	struct FRHIBreadcrumbList
	{
		FRHIBreadcrumbNode* First = nullptr;
		FRHIBreadcrumbNode* Last = nullptr;

		void Append(FRHIBreadcrumbNode* Node)
		{
			check(Node && Node != FRHIBreadcrumbNode::Sentinel);
			check(!Node->ListLink);

			if (!First)
			{
				First = Node;
			}

			if (Last)
			{
				Last->ListLink = Node;
			}
			Last = Node;
		}

		[[nodiscard]] auto IterateAndUnlink()
		{
			struct FResult
			{
				FRHIBreadcrumbNode* First;

				auto begin() const
				{
					struct FIterator
					{
						FRHIBreadcrumbNode* Current;
						FRHIBreadcrumbNode* Next;

						FIterator& operator++()
						{
							Current = Next;
							if (Current)
							{
								Next = Current->ListLink;
								Current->ListLink = nullptr;
							}
							else
							{
								Next = nullptr;
							}

							return *this;
						}

						bool operator != (std::nullptr_t) const
						{
							return Current != nullptr;
						}

						FRHIBreadcrumbNode* operator*() const
						{
							return Current;
						}
					};

					FIterator Iterator { nullptr, First };
					++Iterator;
					return Iterator;
				}

				std::nullptr_t end() const { return nullptr; }
			};

			FResult Result { First };
			First = nullptr;
			Last = nullptr;
			return Result;
		}
		
	};

	//
	// A range of breadcrumb nodes for a given GPU pipeline.
	//
	struct FRHIBreadcrumbRange
	{
		FRHIBreadcrumbNode* First;
		FRHIBreadcrumbNode* Last;

		FRHIBreadcrumbRange() = default;

		FRHIBreadcrumbRange(FRHIBreadcrumbNode* SingleNode)
			: First(SingleNode)
			, Last(SingleNode)
		{}

		FRHIBreadcrumbRange(FRHIBreadcrumbNode* First, FRHIBreadcrumbNode* Last)
			: First(First)
			, Last(Last)
		{}

		//
		// Links the nodes in the 'Other' range into this range, after the node specified by 'Prev'.
		// If 'Prev' is nullptr, the other nodes will be inserted at the start of the range.
		//
		void InsertAfter(FRHIBreadcrumbRange const& Other, FRHIBreadcrumbNode* Prev, ERHIPipeline Pipeline)
		{
			// Either both are nullptr, or both are valid
			check(!Other.First == !Other.Last);
			check(!First == !Last);

			if (!Other.First)
			{
				// Other range has no nodes, nothing to do.
				return; 
			}

			// Other range should not already be linked beyond its end.
			check(!Other.Last->GetNextPtr(Pipeline));

			if (!Prev)
			{
				// Insert at the front of the range
				Other.Last->GetNextPtr(Pipeline) = First;
				First = Other.First;

				if (!Last)
				{
					Last = Other.Last;
				}
			}
			else
			{
				// Insert after 'Prev' node

				// We shouldn't have a 'Prev' node if the outer range is empty.
				check(First);

				FRHIBreadcrumbNode* Next = Prev->GetNextPtr(Pipeline);
				Prev->GetNextPtr(Pipeline) = Other.First;
				Other.Last->GetNextPtr(Pipeline) = Next;

				if (Last == Prev)
				{
					// Range was inserted after all other nodes. Update Last pointer.
					Last = Other.Last;
				}
			}
		}

		auto Enumerate(ERHIPipeline Pipeline) const
		{
			// Either both must be null, or both must be non-null
			check(!First == !Last);

			class FOuter
			{
				FRHIBreadcrumbRange const Range;
				ERHIPipeline const Pipeline;

			public:
				FOuter(FRHIBreadcrumbRange const& Range, ERHIPipeline Pipeline)
					: Range(Range)
					, Pipeline(Pipeline)
				{}

				auto begin() const
				{
					struct FIterator
					{
						FRHIBreadcrumbNode* Current;
						FRHIBreadcrumbNode* const Last;
#if DO_CHECK
						FRHIBreadcrumbNode* const First;
#endif
						ERHIPipeline const Pipeline;

						bool operator != (std::nullptr_t) const
						{
							return Current != nullptr;
						}

						FRHIBreadcrumbNode* operator*() const
						{
							return Current;
						}

						FIterator& operator++()
						{
							if (Current == Last)
							{
								Current = nullptr;
							}
							else
							{
								FRHIBreadcrumbNode* Next = Current->GetNextPtr(Pipeline);

								// Next should never be null here. When iterating a non-empty range, we should always expect to reach 'Last' rather than nullptr.
								checkf(Next, TEXT("Nullptr 'Next' breadcrumb found before reaching the 'Last' breadcrumb in the range. (First: 0x%p, Last: 0x%p, Current: 0x%p)"), First, Last, Current);

								Current = Next;
							}

							return *this;
						}
					};

					return FIterator
					{
						  Range.First
						, Range.Last
					#if DO_CHECK
						, Range.First
					#endif
						, Pipeline
					};
				}

				constexpr std::nullptr_t end() const
				{
					return nullptr;
				}
			};

			return FOuter { *this, Pipeline };
		}

		operator bool() const { return First != nullptr; }

		bool operator == (FRHIBreadcrumbRange const& RHS) const
		{
			return First == RHS.First && Last == RHS.Last;
		}

		bool operator != (FRHIBreadcrumbRange const& RHS) const
		{
			return !(*this == RHS);
		}

		friend uint32 GetTypeHash(FRHIBreadcrumbRange const& Range)
		{
			return HashCombineFast(GetTypeHash(Range.First), GetTypeHash(Range.Last));
		}
	};

	class FRHIBreadcrumb
	{
		RHI_API static std::atomic<uint32> NextID;

	protected:
		FRHIBreadcrumb(FRHIBreadcrumbData&& Data)
			: ID(NextID.fetch_add(1, std::memory_order_relaxed) | 0x80000000) // Set the top bit to avoid collision with zero (i.e. "no breadcrumb")
			, Data(Data)
		{}

		FRHIBreadcrumb(FRHIBreadcrumb const&) = delete;
		FRHIBreadcrumb(FRHIBreadcrumb&&) = delete;

	public:
		uint32 const ID;
		FRHIBreadcrumbData const Data;

#if RHI_BREADCRUMBS_EMIT_CPU
		uint32 const CPUTraceMarkerID = 0;
#endif

		void CreateTraceMarkers()
		{
#if RHI_BREADCRUMBS_EMIT_CPU
			if (TRACE_CPUPROFILER_EVENT_MANUAL_IS_ENABLED())
			{
				FBuffer Buffer;
				TCHAR const* Str = GetTCHAR(Buffer);
				const_cast<uint32&>(CPUTraceMarkerID) = FCpuProfilerTrace::OutputDynamicEventType(Str
			#if RHI_BREADCRUMBS_EMIT_LOCATION
					, Data.File, Data.Line
			#endif
				);
			}
#endif
		}

		struct FBuffer
		{
			TCHAR Data[128];
		};
		virtual TCHAR const* GetTCHAR(FBuffer& Storage) const = 0;
		virtual TCHAR const* GetTCHARNoFormat() const = 0;
	};

	inline void FRHIBreadcrumbNode::SetParent(FRHIBreadcrumbNode* Node)
	{
		check(Parent == nullptr || Parent == FRHIBreadcrumbNode::Sentinel);
		Parent = Node;

		if (Parent && Parent != FRHIBreadcrumbNode::Sentinel && Parent->Allocator != Allocator)
		{
			Allocator->Parents.AddUnique(Parent->Allocator);
		}
	}

	inline void FRHIBreadcrumbNode::BeginCPU() const
	{
#if RHI_BREADCRUMBS_EMIT_CPU
		if (Name.CPUTraceMarkerID)
		{
			FCpuProfilerTrace::OutputBeginEvent(Name.CPUTraceMarkerID);
		}
#endif
	}

	inline void FRHIBreadcrumbNode::EndCPU() const
	{
#if RHI_BREADCRUMBS_EMIT_CPU
		if (Name.CPUTraceMarkerID)
		{
			FCpuProfilerTrace::OutputEndEvent();
		}
#endif
	}

	inline void FRHIBreadcrumbNode::WalkIn(FRHIBreadcrumbNode* Node)
	{
#if RHI_BREADCRUMBS_EMIT_CPU
		if (TRACE_CPUPROFILER_EVENT_MANUAL_IS_ENABLED())
		{
			auto Recurse = [](FRHIBreadcrumbNode* Current, auto& Recurse) -> void
			{
				if (!Current || Current == Sentinel)
					return;

				Recurse(Current->GetParent(), Recurse);
				Current->BeginCPU();
			};
			Recurse(Node, Recurse);
		}
#endif
	}

	inline void FRHIBreadcrumbNode::WalkOut(FRHIBreadcrumbNode* Node)
	{
#if RHI_BREADCRUMBS_EMIT_CPU
		if (TRACE_CPUPROFILER_EVENT_MANUAL_IS_ENABLED())
		{
			while (Node && Node != Sentinel)
			{
				Node->EndCPU();
				Node = Node->GetParent();
			}
		}
#endif
	}

	// Generic copy-by-value
	template <typename T>
	struct TRHIBreadcrumbValue
	{
		static_assert(!(std::is_same_v<T, TCHAR*> || std::is_same_v<T, TCHAR const*>), "Do not use raw TCHAR pointers with RHI breadcrumbs. Pass the FString, FName, or string literal instead.");

		T Value;
		TRHIBreadcrumbValue(T const& Value)
			: Value(Value)
		{}

		struct FConvert
		{
			T const& Inner;
			FConvert(TRHIBreadcrumbValue const& Value)
				: Inner(Value.Value)
			{}
		};
	};

	// String literal - keep the string pointer
	template <size_t N>
	struct TRHIBreadcrumbValue<TCHAR[N]>
	{
		TCHAR const* Value;

		TRHIBreadcrumbValue(TCHAR const* Value)
			: Value(Value)
		{}

		struct FConvert
		{
			TCHAR const* Inner;
			FConvert(TRHIBreadcrumbValue const& Value)
				: Inner(Value.Value)
			{}
		};
	};

	// FName - keep the FName itself and defer resolving
	template <>
	struct TRHIBreadcrumbValue<FName>
	{
		FName Value;
		TRHIBreadcrumbValue(FName const& Value)
			: Value(Value)
		{}

		struct FConvert
		{
			TCHAR Inner[128];
			FConvert(TRHIBreadcrumbValue const& Name)
			{
				Name.Value.ToString(Inner);
			}
		};
	};

	// FDebugName - keep the FDebugName itself and defer resolving
	template <>
	struct TRHIBreadcrumbValue<FDebugName>
	{
		FDebugName Value;
		TRHIBreadcrumbValue(FDebugName const& Value)
			: Value(Value)
		{}

		struct FConvert
		{
			TCHAR Inner[128];
			FConvert(TRHIBreadcrumbValue const& Name)
			{
				Name.Value.ToString(Inner);
			}
		};
	};

	// FString - Take an immediate copy of the string. Total length is limited by fixed buffer size.
	template <>
	struct TRHIBreadcrumbValue<FString>
	{
		TCHAR Buffer[128];
		TRHIBreadcrumbValue(FString const& Value)
		{
			FCString::Strncpy(Buffer, *Value, UE_ARRAY_COUNT(Buffer));
		}

		struct FConvert
		{
			TCHAR const* Inner;
			FConvert(TRHIBreadcrumbValue const& String)
				: Inner(String.Buffer)
			{}
		};
	};

	// Breadcrumb implementation for printf formatted names
	template <typename... TArgs>
	class TRHIBreadcrumb final : public FRHIBreadcrumb
	{
		friend FRHIBreadcrumbAllocator;

		TCHAR const* FormatString;
		std::tuple<TRHIBreadcrumbValue<TArgs>...> Values;

		template <size_t... Indices>
		void ToStringImpl(FBuffer& Buffer, std::index_sequence<Indices...>) const
		{
			// Perform type conversions (call ToString() on FName etc)
			std::tuple<typename TRHIBreadcrumbValue<TArgs>::FConvert...> Converted
			{
				std::get<Indices>(Values)...
			};

			FCString::Snprintf(
				  Buffer.Data
				, UE_ARRAY_COUNT(Buffer.Data)
				, reinterpret_cast<TCHAR const(&)[1]>(*FormatString) // Cast hack to workaround static_assert in Snprintf checking for a string literal. 
				, (std::get<Indices>(Converted).Inner)...
			);
		}

		TRHIBreadcrumb(TRHIBreadcrumb const& Other) = delete;

	public:
		TRHIBreadcrumb(FRHIBreadcrumbData&& Data, TCHAR const* FormatString, TArgs const&... Args)
			: FRHIBreadcrumb(MoveTemp(Data))
			, FormatString(FormatString)
			, Values(Args...)
		{
			CreateTraceMarkers();
		}

		virtual TCHAR const* GetTCHAR(FBuffer& Buffer) const override
		{
			ToStringImpl(Buffer, std::make_index_sequence<sizeof...(TArgs)>{});
			return Buffer.Data;
		}

		virtual TCHAR const* GetTCHARNoFormat() const override
		{
			return FormatString;
		}
	};

	// Breadcrumb implementation for string literals
	template <>
	class TRHIBreadcrumb<> final : public FRHIBreadcrumb
	{
		friend FRHIBreadcrumbAllocator;

		TCHAR const* StringLiteral;

		TRHIBreadcrumb(TRHIBreadcrumb const& Other) = delete;

	public:
		TRHIBreadcrumb(FRHIBreadcrumbData&& Data, TCHAR const* StringLiteral)
			: FRHIBreadcrumb(MoveTemp(Data))
			, StringLiteral(StringLiteral)
		{
			CreateTraceMarkers();
		}

		virtual TCHAR const* GetTCHAR(FBuffer&) const override
		{
			return StringLiteral;
		}

		virtual TCHAR const* GetTCHARNoFormat() const override
		{
			return StringLiteral;
		}
	};

	template<size_t N, typename... TArgs>
	inline FRHIBreadcrumbNode* FRHIBreadcrumbAllocator::AllocBreadcrumb(FRHIBreadcrumbData&& Data, TCHAR const(&FormatString)[N], TArgs&&... Args)
	{
		struct FStorage
		{
			TRHIBreadcrumb<std::decay_t<TArgs>...> Name;
			FRHIBreadcrumbNode Node;

			FStorage(FRHIBreadcrumbAllocator& Allocator, FRHIBreadcrumbData&& Data, TCHAR const(&FormatString)[N], TArgs&&... Args)
				: Name(MoveTemp(Data), FormatString, Forward<TArgs>(Args)...)
				, Node(Allocator, Name)
			{}
		};

		return &Alloc<FStorage>(*this, MoveTemp(Data), FormatString, Forward<TArgs>(Args)...)->Node;
	}

	class FRHIBreadcrumbNodeRef
	{
	private:
		FRHIBreadcrumbNode* Node = nullptr;
		TSharedPtr<FRHIBreadcrumbAllocator> AllocatorRef;

	public:
		FRHIBreadcrumbNodeRef() = default;
		FRHIBreadcrumbNodeRef(FRHIBreadcrumbNode* Node)
			: Node(Node)
		{
			if (Node && Node != FRHIBreadcrumbNode::Sentinel)
			{
				AllocatorRef = Node->Allocator->AsShared();
			}
		}

		operator FRHIBreadcrumbNode* () const { return Node; }
		operator bool() const { return !!Node; }

		FRHIBreadcrumbNode* operator -> () const { return Node; }
		FRHIBreadcrumbNode* Get() const { return Node; }
	};

	//
	// A helper class to manually create, begin and end a breadcrumb on a given RHI command list.
	// For use in places where the Begin/End operations are separate, and a scoped breadcrumb event is not appropriate.
	//
	class FRHIBreadcrumbEventManual
	{
		// Must be a reference. End() may be called with a different RHI command list than the one we 
		// received in the constructor, so we need to keep the underlying RHI breadcrumb allocator alive.
		FRHIBreadcrumbNodeRef Node;
	#if DO_CHECK
		ERHIPipeline const Pipeline;
		uint32 ThreadId;
	#endif

	public:
		template<size_t N, typename... TArgs>
		inline FRHIBreadcrumbEventManual(FRHIComputeCommandList& RHICmdList, FRHIBreadcrumbData&& Data, TCHAR const(&FormatString)[N], TArgs&&... Args);

		inline void End(FRHIComputeCommandList& RHICmdList);

		inline ~FRHIBreadcrumbEventManual();
	};

	//
	// A helper class for scoped breadcrumb. Used by the RHI_BREADCRUMB_EVENT macros.
	//
	class FRHIBreadcrumbEventScope
	{
		FRHIComputeCommandList& RHICmdList;
		FRHIBreadcrumbNode* const Node;
		ERHIPipeline const Pipeline;

		FRHIBreadcrumbEventScope(FRHIBreadcrumbEventScope const&) = delete;
		FRHIBreadcrumbEventScope(FRHIBreadcrumbEventScope&&) = delete;

		template<size_t N, typename... TArgs>
		inline FRHIBreadcrumbEventScope(FRHIComputeCommandList& InRHICmdList, FRHIBreadcrumbData&& Data, ERHIPipeline InPipeline, bool bCondition, TCHAR const(&FormatString)[N], TArgs&&... Args);

	public:
		// Top-of-pipe breadcrumb event scope for RHI command lists
		template<size_t N, typename... TArgs>
		inline FRHIBreadcrumbEventScope(FRHIComputeCommandList& InRHICmdList, FRHIBreadcrumbData&& Data, bool bCondition, TCHAR const(&FormatString)[N], TArgs&&... Args);

		// Bottom-of-pipe breadcrumb event scope for RHI contexts
		template<size_t N, typename... TArgs>
		inline FRHIBreadcrumbEventScope(IRHIComputeContext& InRHIContext, FRHIBreadcrumbData&& Data, bool bCondition, TCHAR const(&FormatString)[N], TArgs&&... Args);

		inline ~FRHIBreadcrumbEventScope();
	};

	#define RHI_BREADCRUMB_EVENT(                 RHICmdList_Or_RHIContext,                  Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), true     , TEXT(Format), ## __VA_ARGS__)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL(     RHICmdList_Or_RHIContext,       Condition, Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), Condition, TEXT(Format), ## __VA_ARGS__)
#if HAS_GPU_STATS
	#define RHI_BREADCRUMB_EVENT_STAT(            RHICmdList_Or_RHIContext, Stat,            Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, GET_STATID(Stat_GPU_##Stat), CSV_STAT_FNAME(Stat)), true     , TEXT(Format), ## __VA_ARGS__)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_STAT(RHICmdList_Or_RHIContext, Stat, Condition, Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, GET_STATID(Stat_GPU_##Stat), CSV_STAT_FNAME(Stat)), Condition, TEXT(Format), ## __VA_ARGS__)
#else
	#define RHI_BREADCRUMB_EVENT_STAT(            RHICmdList_Or_RHIContext, Stat,            Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), true     , TEXT(Format), ## __VA_ARGS__)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_STAT(RHICmdList_Or_RHIContext, Stat, Condition, Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), Condition, TEXT(Format), ## __VA_ARGS__)
#endif

	// Used only for back compat with SCOPED_DRAW_EVENTF
	#define RHI_BREADCRUMB_EVENT_STR_DEPRECATED(            RHICmdList_Or_RHIContext,            Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), true     , Format, ## __VA_ARGS__)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_STR_DEPRECATED(RHICmdList_Or_RHIContext, Condition, Format, ...) FRHIBreadcrumbEventScope ANONYMOUS_VARIABLE(BreadcrumbEvent)(RHICmdList_Or_RHIContext, FRHIBreadcrumbData(__FILE__, __LINE__, TStatId(), NAME_None), Condition, Format, ## __VA_ARGS__)

#else // WITH_RHI_BREADCRUMBS == 0

	#define RHI_BREADCRUMB_EVENT(...)
	#define RHI_BREADCRUMB_EVENT_STAT(...)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL(...)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_STAT(...)

	#define RHI_BREADCRUMB_EVENT_STR_DEPRECATED(...)
	#define RHI_BREADCRUMB_EVENT_CONDITIONAL_STR_DEPRECATED(...)

#endif
//
// Used to override the static_assert caused when choosing between two string literals with a ternary operator, like so:
// SCOPED_DRAW_EVENTF(RHICmdList, EventName, TEXT("Name=%s"), RHI_BREADCRUMB_FORCE_STRING_LITERAL(bCondition ? TEXT("True") : TEXT("False"))
// 
// !! DO NOT USE THIS MACRO FOR NON-STRING LITERALS !!
//
#define RHI_BREADCRUMB_FORCE_STRING_LITERAL(TCharPointer) reinterpret_cast<TCHAR const(*)[1]>(TCharPointer)
