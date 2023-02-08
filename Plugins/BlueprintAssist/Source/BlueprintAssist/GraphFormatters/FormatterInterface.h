// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintAssistSettings.h"

struct FCommentHandler;
class UEdGraphNode;

struct BLUEPRINTASSIST_API FFormatterInterface
	: public TSharedFromThis<FFormatterInterface>
{
	virtual ~FFormatterInterface() = default;
	virtual void FormatNode(UEdGraphNode* Node) = 0;
	virtual TSet<UEdGraphNode*> GetFormattedNodes() = 0;
	virtual UEdGraphNode* GetRootNode() = 0;
	virtual FBAFormatterSettings GetFormatterSettings() { return FBAFormatterSettings(); }
	virtual FCommentHandler* GetCommentHandler() { return nullptr; }
};
