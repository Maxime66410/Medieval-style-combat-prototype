// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "BlueprintAssistUtils.h"
#include "SGraphPin.h"

#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION >= 5
#define BA_PROPERTY FProperty
#define BA_FIND_FIELD FindUField
#define BA_FIND_PROPERTY FindFProperty
#define BA_WEAK_FIELD_PTR TWeakFieldPtr
#else
#define BA_PROPERTY UProperty
#define BA_FIND_FIELD FindField
#define BA_FIND_PROPERTY FindField
#define BA_WEAK_FIELD_PTR TWeakObjectPtr
#endif

struct FBAGraphPinHandle
{
	TWeakObjectPtr<UEdGraph> Graph = nullptr;
	FGuid NodeGuid;
	FGuid PinId;

	// for when guid fails
	FEdGraphPinType PinType;
	FName PinName;

	FBAGraphPinHandle(UEdGraphPin* Pin)
	{
		SetPin(Pin);
	}

	void SetPin(UEdGraphPin* Pin)
	{
		if (!FBAUtils::IsValidPin(Pin))
		{
			Graph = nullptr;
			NodeGuid.Invalidate();
			PinId.Invalidate();
			PinType.ResetToDefaults();
			PinName = NAME_None;
			return;
		}

		if (UEdGraphNode* Node = Pin->GetOwningNodeUnchecked())
		{
			Graph = Node->GetGraph();
			NodeGuid = Node->NodeGuid;
			PinId = Pin->PinId;
			PinType = Pin->PinType;
			PinName = Pin->PinName;
		}
	}

	UEdGraphPin* GetPin()
	{
		if (!IsValid())
		{
			return nullptr;
		}

		for (auto Node : Graph->Nodes)
		{
			if (Node->NodeGuid == NodeGuid)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin->PinId == PinId)
					{
						return Pin;
					}
				}

				// guid failed, find using PinType & PinName
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin->PinType == PinType && Pin->PinName == PinName)
					{
						// side effect: also update the latest PinId
						PinId = Pin->PinId;

						return Pin;
					}
				}

				return nullptr;
			}
		}

		return nullptr;
	}

	bool IsValid() const
	{
		return Graph != nullptr && PinId.IsValid() && NodeGuid.IsValid();
	}

	bool operator==(const FBAGraphPinHandle& Other) const
	{
		return PinId == Other.PinId && NodeGuid == Other.NodeGuid;
	}

	friend inline uint32 GetTypeHash(const FBAGraphPinHandle& Handle)
	{
		return HashCombine(GetTypeHash(Handle.PinId), GetTypeHash(Handle.NodeGuid));
	}
};

// Consider using FEdGraphPinReference
struct FBANodePinHandle
{
	TWeakObjectPtr<UEdGraphNode> Node = nullptr;
	FGuid PinId;

	// for when guid fails
	FEdGraphPinType PinType;
	FName PinName;

	FBANodePinHandle(UEdGraphPin* Pin)
	{
		SetPin(Pin);
	}

	void SetPin(UEdGraphPin* Pin)
	{
		if (Pin)
		{
			PinId = Pin->PinId;
			Node = Pin->GetOwningNode();
			PinType = Pin->PinType;
			PinName = Pin->PinName;
		}
		else
		{
			PinId.Invalidate();
			Node = nullptr;
			PinType.ResetToDefaults();
			PinName = NAME_None;
		}
	}

	UEdGraphPin* GetPin()
	{
		if (!IsValid())
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->PinId == PinId)
			{
				return Pin;
			}
		}

		// guid failed, find using PinType & PinName
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->PinType == PinType && Pin->PinName == PinName)
			{
				// side effect: also update the latest PinId
				PinId = Pin->PinId;

				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphNode* GetNode() const
	{
		return Node.IsValid() ? Node.Get() : nullptr;
	}

	bool IsValid()
	{
		return Node != nullptr && PinId.IsValid();
	}

	static TArray<FBANodePinHandle> ConvertArray(const TArray<UEdGraphPin*>& Pins)
	{
		TArray<FBANodePinHandle> Handles;

		for (UEdGraphPin* const Pin : Pins)
		{
			Handles.Add(FBANodePinHandle(Pin));
		}

		return Handles;
	}

	bool operator==(const FBANodePinHandle& Other) const
	{
		return PinId == Other.PinId && Node == Other.Node;
	}

	friend inline uint32 GetTypeHash(const FBANodePinHandle& Handle)
	{
		return HashCombine(GetTypeHash(Handle.PinId), GetTypeHash(Handle.Node->NodeGuid));
	}
};
