// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistCommentHandler.h"

#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistUtils.h"
#include "EdGraphNode_Comment.h"
#include "FormatterInterface.h"

FCommentHandler::FCommentHandler(TSharedPtr<FBAGraphHandler> InGraphHandler, TSharedPtr<FFormatterInterface> InFormatter)
{
	Init(InGraphHandler, InFormatter);
}

void FCommentHandler::Init(TSharedPtr<FBAGraphHandler> InGraphHandler, TSharedPtr<FFormatterInterface> InFormatter)
{
	if (!InGraphHandler.IsValid() || !InFormatter.IsValid())
	{
		return;
	}

	Reset();

	GraphHandler = InGraphHandler;
	Formatter = InFormatter;

	TSet<UEdGraphNode*> FormattedNodes = Formatter->GetFormattedNodes();
	// UE_LOG(LogTemp, Warning, TEXT("Formatted nodes:"));
	// for (UEdGraphNode* FormattedNode : FormattedNodes)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(FormattedNode));
	// }

	TArray<UEdGraphNode_Comment*> CommentNodes = FBAUtils::GetCommentNodesFromGraph(GraphHandler->GetFocusedEdGraph());

	CommentNodes.Sort([&](const UEdGraphNode_Comment& NodeA, const UEdGraphNode_Comment& NodeB)
	{
		return GetCommentDepth(&NodeA) > GetCommentDepth(&NodeB);
	});

	for (UEdGraphNode_Comment* Comment : CommentNodes)
	{
		if (ShouldIgnoreComment(Comment))
		{
			continue;
		}

		// UE_LOG(LogTemp, Warning, TEXT("Added Comment %s (%d)"), *FBAUtils::GetNodeName(Comment), GetCommentDepth(Comment));

		TArray<UEdGraphNode*> NodesUnderComment = FBAUtils::GetNodesUnderComment(Comment);

		Comments.Add(Comment);

		// bool bShouldModify = false;
		for (UEdGraphNode* EdGraphNode : NodesUnderComment)
		{
			// if (Formatter->GetFormattedNodes().Contains(EdGraphNode))
			{
				CommentNodesContains.FindOrAdd(Comment).Add(EdGraphNode);
				ParentComments.FindOrAdd(EdGraphNode).Add(Comment);
				// UE_LOG(LogTemp, Warning, TEXT("Added comment %s"), *FBAUtils::GetNodeName(Comment));
			}

			// bShouldModify = true;
		}

		// TODO: Modify the comment nodes somewhere?

		// if (bShouldModify)
		// {
		// 	Comment->Modify();
		// }
	}
}

TArray<UEdGraphNode_Comment*> FCommentHandler::GetParentComments(const UEdGraphNode* Node) const
{
	TArray<UEdGraphNode_Comment*> Parents;

	if (const TArray<UEdGraphNode_Comment*>* FoundNodes = ParentComments.Find(Node))
	{
		Parents = *FoundNodes;
	}

	return Parents;
}

TArray<UEdGraphNode*> FCommentHandler::GetNodesUnderComments(UEdGraphNode_Comment* Comment) const
{
	TArray<UEdGraphNode*> Nodes;

	if (const TSet<UEdGraphNode*>* FoundNodes = CommentNodesContains.Find(Comment))
	{
		Nodes = FoundNodes->Array();
	}

	return Nodes;
}

void FCommentHandler::Reset()
{
	Comments.Reset();
	ParentComments.Reset();
	CommentNodesContains.Reset();
}

FSlateRect FCommentHandler::GetCommentBounds(UEdGraphNode_Comment* CommentNode, UEdGraphNode* NodeAsking)
{
	auto ObjUnderComment = CommentNode->GetNodesUnderComment();
	TArray<UEdGraphNode*> NodesUnderComment;
	TArray<UEdGraphNode_Comment*> CommentNodesUnderComment;

	for (auto Obj : ObjUnderComment)
	{
		if (UEdGraphNode* EdNode = Cast<UEdGraphNode>(Obj))
		{
			if (auto Comment = Cast<UEdGraphNode_Comment>(EdNode))
			{
				CommentNodesUnderComment.Add(Comment);
			}
			else
			{
				NodesUnderComment.Add(EdNode);
			}
		}
	}

	auto ContainedNodesBounds = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, NodesUnderComment);
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t ContainedNodesBounds %s"), *ContainedNodesBounds.ToString());
	for (UEdGraphNode_Comment* CommentUnderComment : CommentNodesUnderComment)
	{
		if (CommentUnderComment->GetNodesUnderComment().Num() == 0)
		{
			continue;
		}

		if (DoesCommentContainNode(CommentUnderComment, NodeAsking))
		{
			continue;
		}

		ContainedNodesBounds = ContainedNodesBounds.Expand(GetCommentBounds(CommentUnderComment, NodeAsking));
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t ContainedNodesBounds %s"), *ContainedNodesBounds.ToString());
	}

	const FVector2D Padding = GetDefault<UBASettings>()->CommentNodePadding;

	const float TitlebarHeight = FBAUtils::GetCachedNodeBounds(GraphHandler, CommentNode, false).GetSize().Y;

	const FMargin CommentPadding(
		Padding.X,
		Padding.Y + TitlebarHeight,
		Padding.X,
		Padding.Y);

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tContainedNodeBounds %s | Padding %s"), *ContainedNodesBounds.ToString(), *CommentPadding.GetDesiredSize().ToString());

	ContainedNodesBounds = ContainedNodesBounds.ExtendBy(CommentPadding);

	return ContainedNodesBounds;
}

bool FCommentHandler::DoesCommentContainNode(UEdGraphNode_Comment* Comment, UEdGraphNode* Node)
{
	if (TArray<UEdGraphNode_Comment*>* FoundParents = ParentComments.Find(Node))
	{
		return FoundParents->Contains(Comment);
	}

	return false;
}

int FCommentHandler::GetCommentDepth(const UEdGraphNode_Comment* Comment) const
{
	int MaxDepth = 0;
	for (const UEdGraphNode_Comment* ParentComment : GetParentComments(Comment))
	{
		MaxDepth = FMath::Max(MaxDepth, 1 + GetCommentDepth(ParentComment));
	}

	return MaxDepth;
}

bool FCommentHandler::ShouldIgnoreComment(UEdGraphNode_Comment* Comment)
{
	// UE_LOG(LogTemp, Warning, TEXT("Checking Comment %s"), *FBAUtils::GetNodeName(Comment));

	TArray<UEdGraphNode*> NodesUnderComment = FBAUtils::GetNodesUnderCommentAndChildComments(Comment);

	// ignore containing comments
	NodesUnderComment.RemoveAll(FBAUtils::IsCommentNode);
	
	if (NodesUnderComment.Num() == 0)
	{
		// UE_LOG(LogTemp, Warning, TEXT("\tSkipping comment EMPTY"));
		return true;
	}

	TSet<UEdGraphNode*> FormattedNodes = Formatter->GetFormattedNodes();

	// ignore if the comment contains a node which isn't going to be formatted 
	const bool bContainsNonFormattedNode = NodesUnderComment.ContainsByPredicate([FormattedNodes](UEdGraphNode* Node)
	{
		return !FormattedNodes.Contains(Node);
	});

	if (bContainsNonFormattedNode)
	{
		// UE_LOG(LogTemp, Warning, TEXT("\tSkipping comment NON-FORMATTED"));
		return true;
	}

	// ignore if all nodes are not in the same node tree 
	const auto IsUnderComment = [&NodesUnderComment](const FPinLink& PinLink)
	{
		return NodesUnderComment.Contains(PinLink.GetNode());
	};

	// const TSet<UEdGraphNode*> CommentNodeTree = FBAUtils::GetNodeTreeWithFilter(NodesUnderComment[0], IsUnderComment);
	// if (CommentNodeTree.Num() != NodesUnderComment.Num())
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\tSkipping comment NODE TREE"));
	// 	return true;
	// }
	//
	// const bool bContainsUnlinkedNode = NodesUnderComment.ContainsByPredicate([&CommentNodeTree](UEdGraphNode* Node)
	// {
	// 	if (FBAUtils::IsCommentNode(Node)) // don't check comments
	// 	{
	// 		return false;
	// 	}
	//
	// 	return !CommentNodeTree.Contains(Node);
	// });

	// if (bContainsUnlinkedNode)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\tSkipping comment UNLINKED"));
	// 	return true;
	// }

	return false;
}

bool FCommentHandler::AreCommentsIntersecting(UEdGraphNode_Comment* CommentA, UEdGraphNode_Comment* CommentB)
{
	if (!CommentA || !CommentB)
	{
		return false;
	}

	struct FLocal
	{
		static bool IsContainedInOther(UEdGraphNode_Comment* Comment, UEdGraphNode* Node)
		{
			return FBAUtils::GetNodesUnderComment(Comment).Contains(Node);
		}
	};

	if (FLocal::IsContainedInOther(CommentA, CommentB) || FLocal::IsContainedInOther(CommentB, CommentA))
	{
		return false;
	}

	const TArray<UEdGraphNode*> NodesA = FBAUtils::GetNodesUnderComment(CommentA);
	const TArray<UEdGraphNode*> NodesB = FBAUtils::GetNodesUnderComment(CommentB);

	const TArray<UEdGraphNode*> Intersection = NodesA.FilterByPredicate([&NodesB](UEdGraphNode* Node) { return NodesB.Contains(Node); });
	return Intersection.Num() > 0;
}
