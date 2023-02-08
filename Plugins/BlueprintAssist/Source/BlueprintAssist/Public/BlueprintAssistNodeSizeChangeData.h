// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBAPinChangeData
{
	bool bPinLinked;
	bool bPinHidden;
	FString PinValue;

	FBAPinChangeData() = default;

	void UpdatePin(UEdGraphPin* Pin);

	bool HasPinChanged(UEdGraphPin* Pin);
};


/**
 * @brief Node size can change by:
 *		- Pin being linked
 *		- Pin value changing
 *		- Pin being added or removed
 *		- Expanding the node (see print string)
 *		- Node title changing
 *		- Comment bubble pinned
 */
class FBANodeSizeChangeData
{
	TMap<FGuid, FBAPinChangeData> PinChangeData;
	bool bCommentBubblePinned;
	FString NodeTitle;
	TEnumAsByte<ENodeAdvancedPins::Type> AdvancedPinDisplay;
	ENodeEnabledState NodeEnabledState;

public:
	FBANodeSizeChangeData(UEdGraphNode* Node);

	void UpdateNode(UEdGraphNode* Node);

	bool HasNodeChanged(UEdGraphNode* Node);
};