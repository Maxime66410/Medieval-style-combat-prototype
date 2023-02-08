// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintAssistCommentHandler.h"
#include "FormatterInterface.h"
#include "GraphFormatterTypes.h"
#include "EdGraph/EdGraphNode.h"

struct ChildBranch;
class FEdGraphFormatter;
class FBAGraphHandler;
struct FNodeEdge;
struct FNodeArray;
class FSlateRect;
class UEdGraphNode;

class BLUEPRINTASSIST_API FEdGraphParameterFormatter final
	: public FFormatterInterface
{
	TSharedPtr<FBAGraphHandler> GraphHandler;
	UEdGraphNode* RootNode;
	TSharedPtr<FEdGraphFormatter> GraphFormatter;

public:
	TArray<UEdGraphNode*> IgnoredNodes;
	TSet<UEdGraphNode*> FormattedInputNodes;
	TSet<UEdGraphNode*> FormattedOutputNodes;
	TSet<UEdGraphNode*> AllFormattedNodes;

	bool bInitialized = false;

	bool bCenterBranches;
	int NumRequiredBranches;

	FCommentHandler CommentHandler;

	virtual FCommentHandler* GetCommentHandler() override { return &CommentHandler; }

	virtual UEdGraphNode* GetRootNode() override { return RootNode; }

	FEdGraphParameterFormatter(
		TSharedPtr<FBAGraphHandler> InGraphHandler,
		UEdGraphNode* InRootNode,
		TSharedPtr<FEdGraphFormatter> InGraphFormatter = nullptr,
		UEdGraphNode* InNodeToKeepStill = nullptr,
		TArray<UEdGraphNode*> InIgnoredNodes = TArray<UEdGraphNode*>());

	virtual ~FEdGraphParameterFormatter() override { }

	virtual void FormatNode(UEdGraphNode* Node) override;

	virtual TSet<UEdGraphNode*> GetFormattedNodes() override;

	void SetIgnoredNodes(TArray<UEdGraphNode*> InIgnoredNodes) { IgnoredNodes = InIgnoredNodes; }

	FSlateRect GetBounds();

	FSlateRect GetParameterBounds();

	void ExpandByHeight();

	void SaveRelativePositions();

	bool IsUsingHelixing() const { return bFormatWithHelixing; }

private:
	bool bFormatWithHelixing;

	TMap<UEdGraphNode*, TSharedPtr<FNodeInfo>> NodeInfoMap;

	bool DoesHelixingApply();

	UEdGraphNode* NodeToKeepStill;

	FVector2D Padding;

	TMap<FPinLink, bool> SameRowMapping;

	TMap<UEdGraphNode*, FVector2D> NodeOffsets;

	void ProcessSameRowMapping(UEdGraphNode* CurrentNode,
								UEdGraphPin* CurrentPin,
								UEdGraphPin* ParentPin,
								TSet<UEdGraphNode*>& VisitedNodes);

	void FormatX();

	void FormatY(
		UEdGraphNode* CurrentNode,
		UEdGraphPin* CurrentPin,
		UEdGraphPin* ParentPin,
		TSet<UEdGraphNode*>& VisitedNodes,
		bool bSameRow,
		TSet<UEdGraphNode*>& Children);

	void CenterBranches(UEdGraphNode* CurrentNode, const TArray<ChildBranch>& ChildBranches, const TSet<UEdGraphNode*>& NodesToCollisionCheck);

	int32 GetChildX(const FPinLink& Link,
					const EEdGraphPinDirection Direction) const;

	bool AnyLinkedImpureNodes() const;

	void MoveBelowBaseline(TSet<UEdGraphNode*> Nodes, float Baseline);

	void DebugPrintFormatted();

	void SimpleRelativeFormatting();

	FSlateRect GetNodeBounds(UEdGraphNode* Node);

	void ApplyCommentPaddingX();
	void ApplyCommentPaddingX_Recursive(TArray<UEdGraphNode*> NodeSet, TSet<UEdGraphNode*>& OutHandledNodes, TArray<FPinLink>& OutLeafLinks);

	void ApplyCommentPaddingY();
	void ApplyCommentPaddingY_Recursive(TArray<UEdGraphNode*> NodeSet, TSet<UEdGraphNode*>& OutHandledNodes);
};
