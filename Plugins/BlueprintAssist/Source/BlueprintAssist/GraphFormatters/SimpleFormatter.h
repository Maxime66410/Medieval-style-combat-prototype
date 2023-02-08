// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintAssistCommentHandler.h"
#include "BlueprintAssistGraphHandler.h"
#include "FormatterInterface.h"

class BLUEPRINTASSIST_API FSimpleFormatter
	: public FFormatterInterface
{
public:
	TSharedPtr<FBAGraphHandler> GraphHandler;
	FBAFormatterSettings FormatterSettings;
	float TrackSpacing;
	UEdGraphNode* RootNode;
	virtual UEdGraphNode* GetRootNode() override { return RootNode; }
	TSet<UEdGraphNode*> FormattedNodes;
	TMap<UEdGraphNode*, TSharedPtr<FFormatXInfo>> FormatXInfoMap;
	TMap<FPinLink, bool> SameRowMapping;

	TSet<TSharedPtr<FFormatXInfo>> NodesToExpand;

	TArray<FPinLink> Path;

	FCommentHandler CommentHandler;

	FSimpleFormatter(TSharedPtr<FBAGraphHandler> InGraphHandler);

	virtual ~FSimpleFormatter() override { }

	virtual void FormatNode(UEdGraphNode* Node) override;

	void FormatX();
	int32 GetChildX(const FPinLink& Link);
	void ExpandPendingNodes();

	void FormatY();

	void FormatY_Recursive(
		UEdGraphNode* CurrentNode,
		UEdGraphPin* CurrentPin,
		UEdGraphPin* ParentPin,
		TSet<UEdGraphNode*>& NodesToCollisionCheck,
		TSet<FPinLink>& VisitedLinks,
		bool bSameRow,
		TSet<UEdGraphNode*>& Children);

	virtual TSet<UEdGraphNode*> GetFormattedNodes() override;

	virtual FCommentHandler* GetCommentHandler() override { return &CommentHandler; }

	virtual FBAFormatterSettings GetFormatterSettings() override;

	FSlateRect GetNodeBounds(UEdGraphNode* Node);
	FSlateRect GetNodeArrayBounds(const TArray<UEdGraphNode*>& Nodes);

	void ApplyCommentPaddingX();
	void ApplyCommentPaddingX_Recursive(TArray<UEdGraphNode*> NodeSet, TSet<UEdGraphNode*>& OutHandledNodes);

	void ApplyCommentPaddingY();
	void ApplyCommentPaddingY_Recursive(TArray<UEdGraphNode*> NodeSet, TSet<UEdGraphNode*>& OutHandledNodes);
};
