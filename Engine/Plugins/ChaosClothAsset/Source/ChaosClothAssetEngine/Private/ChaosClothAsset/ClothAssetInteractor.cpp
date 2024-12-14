// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothAssetInteractor.h"
#include "Chaos/CollectionPropertyFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothAssetInteractor)

namespace UE::Chaos::ClothAsset::Private
{
	// Put aliases in the same order as how the if/else works in FClothConstraints::Create[Type]Constraints to ensure
	// the property that is actually used by the solver is used here.

	// SimulationBendingConfigNode
	static const TArray<FString> BendingStiffnessWarpAliases =
	{
		TEXT("XPBDAnisoBendingStiffnessWarp"),
	};
	static const TArray<FString> BendingStiffnessWeftAliases =
	{
		TEXT("XPBDAnisoBendingStiffnessWeft"),
	};
	static const TArray<FString> BendingStiffnessBiasAliases =
	{
		TEXT("XPBDAnisoBendingStiffnessBias"),
	};
	static const TArray<FString> BendingDampingAliases =
	{
		TEXT("XPBDAnisoBendingDamping"),
		TEXT("XPBDBendingElementDamping"),
		TEXT("XPBDBendingSpringDamping"),
	};
	static const TArray<FString> BucklingRatioAliases =
	{
		TEXT("XPBDAnisoBucklingRatio"),
		TEXT("XPBDBucklingRatio"),
		TEXT("BucklingRatio"),
	};
	static const TArray<FString> BucklingStiffnessWarpAliases =
	{
		TEXT("XPBDAnisoBucklingStiffnessWarp"),
	};
	static const TArray<FString> BucklingStiffnessWeftAliases =
	{
		TEXT("XPBDAnisoBucklingStiffnessWeft"),
	};
	static const TArray<FString> BucklingStiffnessBiasAliases =
	{
		TEXT("XPBDAnisoBucklingStiffnessBias"),
	};
	static const TArray<FString> BendingStiffnessAliases =
	{
		TEXT("XPBDBendingElementStiffness"),
		TEXT("BendingElementStiffness"),
		TEXT("XPBDBendingSpringStiffness"),
		TEXT("BendingSpringStiffness"),
	};
	static const TArray<FString> BucklingStiffnessAliases =
	{
		TEXT("XPBDBucklingStiffness"),
		TEXT("BucklingStiffness"),
	};

	// SimulationStretchConfigNode
	static const TArray<FString> StretchStiffnessWarpAliases =
	{
		TEXT("XPBDAnisoStretchStiffnessWarp"),
		TEXT("XPBDAnisoSpringStiffnessWarp"),
	};
	static const TArray<FString> StretchStiffnessWeftAliases =
	{
		TEXT("XPBDAnisoStretchStiffnessWeft"),
		TEXT("XPBDAnisoSpringStiffnessWeft"),
	};
	static const TArray<FString> StretchStiffnessBiasAliases =
	{
		TEXT("XPBDAnisoStretchStiffnessBias"),
		TEXT("XPBDAnisoSpringStiffnessBias"),
	};
	static const TArray<FString> StretchDampingAliases =
	{
		TEXT("XPBDAnisoStretchDamping"),
		TEXT("XPBDEdgeSpringDamping"),
		TEXT("XPBDAnisoSpringDamping"),
	};
	static const TArray<FString> StretchStiffnessAliases =
	{
		TEXT("XPBDEdgeSpringStiffness"),
		TEXT("EdgeSpringStiffness"),
	};
	static const TArray<FString> StretchWarpScaleAliases =
	{
		TEXT("XPBDAnisoStretchWarpScale"),
		TEXT("XPBDAnisoSpringWarpScale"),
	};
	static const TArray<FString> StretchWeftScaleAliases =
	{
		TEXT("XPBDAnisoStretchWeftScale"),
		TEXT("XPBDAnisoSpringWeftScale"),
	};
	static const TArray<FString> AreaStiffnessAliases =
	{
		TEXT("XPBDAreaSpringStiffness"),
		TEXT("AreaSpringStiffness"),
	};
	

	static const TMap<FString, TArray<FString>> Aliases =
	{
		{TEXT("BendingStiffnessWarp"), BendingStiffnessWarpAliases},
		{TEXT("BendingStiffnessWeft"), BendingStiffnessWeftAliases},
		{TEXT("BendingStiffnessBias"), BendingStiffnessBiasAliases},
		{TEXT("BendingDamping"), BendingDampingAliases},
		{TEXT("BucklingRatio"), BucklingRatioAliases},
		{TEXT("BucklingStiffnessWarp"), BucklingStiffnessWarpAliases},
		{TEXT("BucklingStiffnessWeft"), BucklingStiffnessWeftAliases},
		{TEXT("BucklingStiffnessBias"), BucklingStiffnessBiasAliases},
		{TEXT("BendingStiffness"), BendingStiffnessAliases},
		{TEXT("BucklingStiffness"), BucklingStiffnessAliases},
		{TEXT("StretchStiffnessWarp"), StretchStiffnessWarpAliases},
		{TEXT("StretchStiffnessWeft"), StretchStiffnessWeftAliases},
		{TEXT("StretchStiffnessBias"), StretchStiffnessBiasAliases},
		{TEXT("StretchDamping"), StretchDampingAliases},
		{TEXT("StretchStiffness"), StretchStiffnessAliases},
		{TEXT("StretchWarpScale"), StretchWarpScaleAliases},
		{TEXT("StretchWeftScale"), StretchWeftScaleAliases},
		{TEXT("AreaStiffness"), AreaStiffnessAliases},
	};


	template<typename T>
	static T GetValueWithAlias(TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade>& PropertyFacade, const FString& PropertyName, const T& DefaultValue, const TFunctionRef<T(int32 KeyIndex)>& GetValue)
	{
		check(PropertyFacade);
		{
			const int32 KeyIndex = PropertyFacade->GetKeyIndex(PropertyName);
			if (KeyIndex != INDEX_NONE)
			{
				return GetValue(KeyIndex);
			}
		}

		if (const TArray<FString>* FoundAliases = Aliases.Find(PropertyName))
		{
			for (const FString& FoundAlias : *FoundAliases)
			{
				const int32 KeyIndex = PropertyFacade->GetKeyIndex(FoundAlias);
				if (KeyIndex != INDEX_NONE)
				{
					return GetValue(KeyIndex);
				}
			}
		}

		return DefaultValue;
	}

	void SetValueWithAlias(TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade>& PropertyFacade, const FString& PropertyName, const TFunctionRef<void(const FString&)>& SetValue)
	{
		check(PropertyFacade);
		if (const TArray<FString>* FoundAliases = Aliases.Find(PropertyName))
		{
			for (const FString& FoundAlias : *FoundAliases)
			{
				SetValue(FoundAlias);
			}
		}
		else
		{
			SetValue(PropertyName);
		}
	}
}

void UChaosClothAssetInteractor::SetProperties(const TArray<TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade>>& InCollectionPropertyFacades)
{
	CollectionPropertyFacades.Reset(InCollectionPropertyFacades.Num());
	for (const TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade>& InFacade : InCollectionPropertyFacades)
	{
		CollectionPropertyFacades.Emplace(InFacade);
	}
}

void UChaosClothAssetInteractor::ResetProperties()
{
	CollectionPropertyFacades.Reset();
}

TArray<FString> UChaosClothAssetInteractor::GetAllProperties(int32 LODIndex) const
{
	using namespace UE::Chaos::ClothAsset::Private;

	TSet<FString> Keys;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				if (Keys.IsEmpty())
				{
					// This is the first non-empty LOD. We can add all keys without worrying about uniqueness.
					Keys.Reserve(PropertyFacade->Num() + Aliases.Num());
					for (int32 KeyIndex = 0; KeyIndex < PropertyFacade->Num(); ++KeyIndex)
					{
						Keys.Add(PropertyFacade->GetKey(KeyIndex));
					}
				}
				else
				{
					for (int32 KeyIndex = 0; KeyIndex < PropertyFacade->Num(); ++KeyIndex)
					{
						Keys.Add(PropertyFacade->GetKey(KeyIndex));
					}
				}
			}
		}
	}
	else
	{
		if (CollectionPropertyFacades.IsValidIndex(LODIndex))
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
			{
				Keys.Reserve(PropertyFacade->Num());
				for (int32 KeyIndex = 0; KeyIndex < PropertyFacade->Num(); ++KeyIndex)
				{
					Keys.Add(PropertyFacade->GetKey(KeyIndex));
				}
			}
		}
	}

	for (TMap<FString, TArray<FString>>::TConstIterator AliasIter = Aliases.CreateConstIterator(); AliasIter; ++AliasIter)
	{
		for (const FString& OtherName : AliasIter.Value())
		{
			if (Keys.Contains(OtherName))
			{
				Keys.Add(AliasIter.Key());
			}
		}
	}
	return Keys.Array();
}

float UChaosClothAssetInteractor::GetFloatValue(const FString& PropertyName, int32 LODIndex, float DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<float>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return PropertyFacade->GetValue<float>(KeyIndex);
				});
		}
	}
	return DefaultValue;
}

float UChaosClothAssetInteractor::GetLowFloatValue(const FString& PropertyName, int32 LODIndex, float DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<float>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return PropertyFacade->GetLowValue<float>(KeyIndex);
				});
		}
	}
	return DefaultValue;
}

float UChaosClothAssetInteractor::GetHighFloatValue(const FString& PropertyName, int32 LODIndex, float DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<float>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return PropertyFacade->GetHighValue<float>(KeyIndex);
				});
		}
	}
	return DefaultValue;
}

FVector2D UChaosClothAssetInteractor::GetWeightedFloatValue(const FString& PropertyName, int32 LODIndex, FVector2D DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<FVector2D>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return FVector2D(PropertyFacade->GetWeightedFloatValue(KeyIndex));
				});
		}
	}
	return DefaultValue;
}

int32 UChaosClothAssetInteractor::GetIntValue(const FString& PropertyName, int32 LODIndex, int32 DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<int32>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return PropertyFacade->GetValue<int32>(KeyIndex);
				});
		}
	}
	return DefaultValue;
}

FVector UChaosClothAssetInteractor::GetVectorValue(const FString& PropertyName, int32 LODIndex, FVector DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<FVector>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return FVector(PropertyFacade->GetValue<FVector3f>(KeyIndex));
				});
		}
	}
	return DefaultValue;
}

FString UChaosClothAssetInteractor::GetStringValue(const FString& PropertyName, int32 LODIndex, const FString& DefaultValue) const
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			return GetValueWithAlias<FString>(PropertyFacade, PropertyName, DefaultValue,
				[&PropertyFacade](int32 KeyIndex)
				{
					return PropertyFacade->GetStringValue(KeyIndex);
				});
		}
	}
	return DefaultValue;
}

void UChaosClothAssetInteractor::SetFloatValue(const FString& PropertyName, int32 LODIndex, float Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, Value](const FString& Name)
					{
						PropertyFacade->SetValue(Name, Value);
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, Value](const FString& Name)
				{
					PropertyFacade->SetValue(Name, Value);
				});
		}
	}
}

void UChaosClothAssetInteractor::SetLowFloatValue(const FString& PropertyName, int32 LODIndex, float Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, Value](const FString& Name)
					{
						PropertyFacade->SetLowValue(Name, Value);
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, Value](const FString& Name)
				{
					PropertyFacade->SetLowValue(Name, Value);
				});
		}
	}
}

void UChaosClothAssetInteractor::SetHighFloatValue(const FString& PropertyName, int32 LODIndex, float Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, Value](const FString& Name)
					{
						PropertyFacade->SetHighValue(Name, Value);
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, Value](const FString& Name)
				{
					PropertyFacade->SetHighValue(Name, Value);
				});
		}
	}
}

void UChaosClothAssetInteractor::SetWeightedFloatValue(const FString& PropertyName, int32 LODIndex, FVector2D Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, &Value](const FString& Name)
					{
						PropertyFacade->SetWeightedFloatValue(Name, FVector2f(Value));
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, &Value](const FString& Name)
				{
					PropertyFacade->SetWeightedFloatValue(Name, FVector2f(Value));
				});
		}
	}
}

void UChaosClothAssetInteractor::SetIntValue(const FString& PropertyName, int32 LODIndex, int32 Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, Value](const FString& Name)
					{
						PropertyFacade->SetValue(Name, Value);
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, Value](const FString& Name)
				{
					PropertyFacade->SetValue(Name, Value);
				});
		}
	}
}

void UChaosClothAssetInteractor::SetVectorValue(const FString& PropertyName, int32 LODIndex, FVector Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, &Value](const FString& Name)
					{
						PropertyFacade->SetValue(Name, FVector3f(Value));
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, &Value](const FString& Name)
				{
					PropertyFacade->SetValue(Name, FVector3f(Value));
				});
		}
	}
}

void UChaosClothAssetInteractor::SetStringValue(const FString& PropertyName, int32 LODIndex, const FString& Value)
{
	using namespace UE::Chaos::ClothAsset::Private;
	if (LODIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < CollectionPropertyFacades.Num(); ++Index)
		{
			if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[Index].Pin())
			{
				SetValueWithAlias(PropertyFacade, PropertyName,
					[&PropertyFacade, &Value](const FString& Name)
					{
						PropertyFacade->SetStringValue(Name, Value);
					});
			}
		}
	}
	else if (CollectionPropertyFacades.IsValidIndex(LODIndex))
	{
		if (TSharedPtr<::Chaos::Softs::FCollectionPropertyFacade> PropertyFacade = CollectionPropertyFacades[LODIndex].Pin())
		{
			SetValueWithAlias(PropertyFacade, PropertyName,
				[&PropertyFacade, &Value](const FString& Name)
				{
					PropertyFacade->SetStringValue(Name, Value);
				});
		}
	}
}
