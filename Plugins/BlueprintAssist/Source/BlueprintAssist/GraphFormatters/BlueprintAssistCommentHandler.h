// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

struct FFormatterInterface;
class UEdGraphNode_Comment;
class FBAGraphHandler;

struct BLUEPRINTASSIST_API FCommentHandler
	: public TSharedFromThis<FCommentHandler>
{
	TSharedPtr<FBAGraphHandler> GraphHandler;
	TSharedPtr<FFormatterInterface> Formatter;
	TSet<UEdGraphNode_Comment*> Comments;
	TMap<UEdGraphNode*, TArray<UEdGraphNode_Comment*>> ParentComments;
	TMap<UEdGraphNode_Comment*, TSet<UEdGraphNode*>> CommentNodesContains;

	FCommentHandler() = default;
	FCommentHandler(TSharedPtr<FBAGraphHandler> InGraphHandler, TSharedPtr<FFormatterInterface> InFormatter);

	void Init(TSharedPtr<FBAGraphHandler> InGraphHandler, TSharedPtr<FFormatterInterface> InFormatter);

	TSet<UEdGraphNode_Comment*> GetComments() const { return Comments; }
	TArray<UEdGraphNode_Comment*> GetParentComments(const UEdGraphNode* Node) const;
	TArray<UEdGraphNode*> GetNodesUnderComments(UEdGraphNode_Comment* Comment) const;

	void Reset();

	FSlateRect GetCommentBounds(UEdGraphNode_Comment* CommentNode, UEdGraphNode* NodeAsking = nullptr);

	bool DoesCommentContainNode(UEdGraphNode_Comment* Comment, UEdGraphNode* Node);

	int GetCommentDepth(const UEdGraphNode_Comment* Comment) const;

	bool ShouldIgnoreComment(UEdGraphNode_Comment* Comment);

	static bool AreCommentsIntersecting(UEdGraphNode_Comment* CommentA, UEdGraphNode_Comment* CommentB);
};
