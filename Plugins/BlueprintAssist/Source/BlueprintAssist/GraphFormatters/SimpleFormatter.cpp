// Copyright 2021 fpwong. All Rights Reserved.

#include "SimpleFormatter.h"

#include "BAFormatterUtils.h"
#include "BlueprintAssistUtils.h"
#include "EdGraphNode_Comment.h"
#include "Containers/Queue.h"

FSimpleFormatter::FSimpleFormatter(TSharedPtr<FBAGraphHandler> InGraphHandler)
	: GraphHandler(InGraphHandler)
	, RootNode(nullptr)
{
	TrackSpacing = GetMutableDefault<UBASettings>()->BlueprintKnotTrackSpacing;
}

void FSimpleFormatter::FormatNode(UEdGraphNode* Node)
{
	RootNode = Node;

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Root node %s"), *FBAUtils::GetNodeName(RootNode));

	FormatterSettings = GetFormatterSettings();

	int32 SavedNodePosX = RootNode->NodePosX;
	int32 SavedNodePosY = RootNode->NodePosY;

	FormatX();

	CommentHandler.Init(GraphHandler, SharedThis(this));

	FormatY();

	// UE_LOG(LogTemp, Warning, TEXT("Same row mapping"));
	// for (auto Kvp : SameRowMapping)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\t%s"), *Kvp.Key.ToString());
	// }

	if (GetDefault<UBASettings>()->bApplyCommentPadding)
	{
		ApplyCommentPaddingX();
	}

	if (GetDefault<UBASettings>()->bApplyCommentPadding)
	{
		ApplyCommentPaddingY();
	}

	// reset root node position
	const float DeltaX = SavedNodePosX - RootNode->NodePosX;
	const float DeltaY = SavedNodePosY - RootNode->NodePosY;

	for (auto FormattedNode : FormattedNodes)
	{
		FormattedNode->NodePosX += DeltaX;
		FormattedNode->NodePosY += DeltaY;
	}
}

void FSimpleFormatter::FormatX()
{
	TSet<UEdGraphNode*> VisitedNodes;
	TSet<UEdGraphNode*> PendingNodes;
	PendingNodes.Add(RootNode);
	TSet<FPinLink> VisitedLinks;
	const FPinLink RootNodeLink(nullptr, nullptr, RootNode);
	TSharedPtr<FFormatXInfo> RootInfo = MakeShareable(new FFormatXInfo(RootNodeLink, nullptr));

	TArray<TSharedPtr<FFormatXInfo>> OutputStack;
	TArray<TSharedPtr<FFormatXInfo>> InputStack;
	OutputStack.Push(RootInfo);
	FormatXInfoMap.Add(RootNode, RootInfo);

	EEdGraphPinDirection LastDirection = FormatterSettings.FormatterDirection;

	NodesToExpand.Reset();

	while (OutputStack.Num() > 0 || InputStack.Num() > 0)
	{
		// try to get the current info from the pending input
		TSharedPtr<FFormatXInfo> CurrentInfo = nullptr;

		TArray<TSharedPtr<FFormatXInfo>>& FirstStack = LastDirection == EGPD_Output ? OutputStack : InputStack;
		TArray<TSharedPtr<FFormatXInfo>>& SecondStack = LastDirection == EGPD_Output ? InputStack : OutputStack;

		if (FirstStack.Num() > 0)
		{
			CurrentInfo = FirstStack.Pop();
		}
		else
		{
			CurrentInfo = SecondStack.Pop();
		}

		LastDirection = CurrentInfo->Link.GetDirection();

		UEdGraphNode* CurrentNode = CurrentInfo->GetNode();
		VisitedNodes.Add(CurrentNode);

		FormattedNodes.Add(CurrentNode);

		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Processing %s | %s"), *FBAUtils::GetNodeName(CurrentNode), *CurrentInfo->Link.ToString());
		const int32 NewX = GetChildX(CurrentInfo->Link);

		if (!FormatXInfoMap.Contains(CurrentNode))
		{
			if (CurrentNode != RootNode)
			{
				CurrentInfo->SetParent(CurrentInfo->Parent);
				CurrentNode->NodePosX = NewX;

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tInitial Set node pos x %d %s"), NewX, *FBAUtils::GetNodeName(CurrentNode));

				Path.Add(CurrentInfo->Link);
			}
			FormatXInfoMap.Add(CurrentNode, CurrentInfo);
		}
		else
		{
			TSharedPtr<FFormatXInfo> OldInfo = FormatXInfoMap[CurrentNode];

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tInfo map contains %s | %s (%s) | Parent %s (%s) | %d"),
			//        *FBAUtils::GetNodeName(CurrentInfo->Link.To->GetOwningNode()),
			//        *FBAUtils::GetNodeName(CurrentInfo->GetNode()),
			//        *FBAUtils::GetPinName(CurrentInfo->Link.To),
			//        *FBAUtils::GetNodeName(CurrentInfo->Link.From->GetOwningNode()),
			//        *FBAUtils::GetPinName(CurrentInfo->Link.From),
			//        NewX);

			const bool bHasNoParent = CurrentInfo->Link.From == nullptr;

			bool bHasCycle = false;
			if (!bHasNoParent) // if we have a parent, check if there is a cycle
			{
				// bHasCycle = OldInfo->GetChildren(EGPD_Output).Contains(CurrentInfo->Parent->GetNode());
				bHasCycle = OldInfo->GetChildren().Contains(CurrentInfo->Parent->GetNode());

				// if (bHasCycle)
				// {
				// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tHas cycle! Skipping"));
				// 	for (UEdGraphNode* Child : OldInfo->GetChildren(EGPD_Output))
				// 	{
				// 		UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tChild: %s"), *FBAUtils::GetNodeName(Child));
				// 	}
				// }

				// for (UEdGraphNode* Child : OldInfo->GetChildren(EGPD_Output))
				// // for (UEdGraphNode* Child : OldInfo->GetChildren())
				// {
				// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tParent %s | Child: %s"), *FBAUtils::GetNodeName(CurrentInfo->Parent->GetNode()), *FBAUtils::GetNodeName(Child));
				// }
			}

			if (bHasNoParent || !bHasCycle)
			{
				if (OldInfo->Parent.IsValid())
				{
					bool bTakeNewParent = bHasNoParent;

					if (!bTakeNewParent)
					{
						const int32 OldX = CurrentInfo->GetNode()->NodePosX;

						const bool bPositionIsBetter
							= CurrentInfo->Link.From->Direction == EGPD_Output
							? NewX > OldX
							: NewX < OldX;

						// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t Comparing parents Old: %s (%d) New: %s (%d)"),
						//        *FBAUtils::GetNodeName(OldInfo->Link.From->GetOwningNode()), OldX,
						//        *FBAUtils::GetNodeName(CurrentInfo->Link.From->GetOwningNode()), NewX);

						const bool bSameDirection = OldInfo->Link.To->Direction == CurrentInfo->Link.To->Direction;
						// if (!bSameDirection) UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tNot same direction"));
						//
						// if (!bPositionIsBetter) UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tPosition is worse?"));

						bTakeNewParent = bPositionIsBetter && bSameDirection;
					}

					// take the new parent by updating the old info
					if (bTakeNewParent)
					{
						// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tTOOK PARENT"));

						OldInfo->Link = CurrentInfo->Link;
						OldInfo->SetParent(CurrentInfo->Parent);

						CurrentInfo = OldInfo;

						CurrentNode->NodePosX = NewX;

						for (TSharedPtr<FFormatXInfo> ChildInfo : CurrentInfo->Children)
						{
							if (ChildInfo->Link.GetDirection() == EGPD_Output)
							{
								OutputStack.Push(ChildInfo);
							}
							else
							{
								InputStack.Push(ChildInfo);
							}
						}

						Path.Add(CurrentInfo->Link);
					}
				}
			}
		}

		TArray<UEdGraphPin*> LinkedPins = FBAUtils::GetLinkedPins(CurrentInfo->GetNode());
		// for (auto Pin : LinkedPins)
		// {
		// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("Pin %s"), *FBAUtils::GetPinName(Pin));
		// }

		for (int i = LinkedPins.Num() - 1; i >= 0; --i)
		{
			UEdGraphPin* ParentPin = LinkedPins[i];

			for (UEdGraphPin* LinkedPin : ParentPin->LinkedTo)
			{
				UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Iterating node %s"), *FBAUtils::GetNodeName(LinkedNode));

				const FPinLink PinLink(ParentPin, LinkedPin, LinkedNode);
				if (VisitedLinks.Contains(PinLink))
				{
					continue;
				}

				VisitedLinks.Add(PinLink);

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tIterating pin link %s"), *PinLink.ToString());

				TSharedPtr<FFormatXInfo> LinkedInfo = MakeShareable(new FFormatXInfo(PinLink, CurrentInfo));

				if (ParentPin->Direction == FormatterSettings.FormatterDirection)
				{
					OutputStack.Push(LinkedInfo);
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\t\t\tAdded to output stack"));
				}
				else
				{
					if (GetMutableDefault<UBASettings>()->FormattingStyle == EBANodeFormattingStyle::Expanded)
					{
						EEdGraphPinDirection OppositeDirection = UEdGraphPin::GetComplementaryDirection(FormatterSettings.FormatterDirection);

						if (CurrentInfo->Link.GetDirection() == FormatterSettings.FormatterDirection)
						{
							const bool bHasCycle = PendingNodes.Contains(LinkedNode) || FBAUtils::GetExecTree(LinkedNode, OppositeDirection).Contains(CurrentInfo->GetNode());

							if (!bHasCycle)
							{
								if (!CurrentInfo->Parent.IsValid() || LinkedNode != CurrentInfo->Parent->GetNode())
								{
									NodesToExpand.Add(CurrentInfo);
									// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\t\t\tExpanding node %s"), *FBAUtils::GetNodeName(LinkedNode));
								}
							}
						}
					}

					InputStack.Push(LinkedInfo);
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\t\t\tAdded to input stack"));
				}

				PendingNodes.Add(LinkedNode);
			}
		}
	}

	if (GetMutableDefault<UBASettings>()->FormattingStyle == EBANodeFormattingStyle::Expanded)
	{
		ExpandPendingNodes();
	}
}

int32 FSimpleFormatter::GetChildX(const FPinLink& Link)
{
	if (Link.From == nullptr)
	{
		return GetNodeBounds(Link.GetNode()).Left;
	}

	EEdGraphPinDirection Direction = Link.GetDirection();
	UEdGraphNode* Parent = Link.From->GetOwningNode();
	UEdGraphNode* Child = Link.To->GetOwningNode();
	FSlateRect ParentBounds = GetNodeBounds(Parent);

	FSlateRect ChildBounds = GetNodeBounds(Child);

	FSlateRect LargerBounds = GetNodeBounds(Child);

	float NewNodePos;
	if (Link.From->Direction == EGPD_Input)
	{
		const float Delta = LargerBounds.Right - ChildBounds.Left;
		NewNodePos = ParentBounds.Left - Delta - FormatterSettings.Padding.X; // -1;
	}
	else
	{
		const float Delta = ChildBounds.Left - LargerBounds.Left;
		NewNodePos = ParentBounds.Right + Delta + FormatterSettings.Padding.X; // +1;
	}

	return FMath::RoundToInt(NewNodePos);
	// return ParentBounds.Left - Padding.X - ChildBounds.GetSize().X;
}

FSlateRect FSimpleFormatter::GetNodeBounds(UEdGraphNode* Node)
{
	if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
	{
		return CommentHandler.GetCommentBounds(Comment);
	}

	return FBAUtils::GetCachedNodeBounds(GraphHandler, Node);
}

FSlateRect FSimpleFormatter::GetNodeArrayBounds(const TArray<UEdGraphNode*>& Nodes)
{
	return FBAUtils::GetCachedNodeArrayBounds(GraphHandler, Nodes);
}

FBAFormatterSettings FSimpleFormatter::GetFormatterSettings()
{
	return FBAUtils::GetFormatterSettings(GraphHandler->GetFocusedEdGraph());
}

void FSimpleFormatter::ExpandPendingNodes()
{
	for (TSharedPtr<FFormatXInfo> Info : NodesToExpand)
	{
		if (!Info->Parent.IsValid())
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Expand X Invalid %s"), *FBAUtils::GetNodeName(Info->GetNode()));
			return;
		}

		UEdGraphNode* Node = Info->GetNode();
		UEdGraphNode* Parent = Info->Parent->GetNode();

		auto OppositeDirection = UEdGraphPin::GetComplementaryDirection(FormatterSettings.FormatterDirection);
		TArray<UEdGraphNode*> Children = Info->GetChildren(OppositeDirection);

		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Expand X %s | %s"), *FBAUtils::GetNodeName(Info->GetNode()), *FBAUtils::GetNodeName(Parent));

		if (Children.Num() > 0)
		{
			FSlateRect ChildrenBounds = GetNodeArrayBounds(Children);

			FSlateRect ParentBounds = GetNodeBounds(Parent);

			bool bShouldExpand = FormatterSettings.FormatterDirection == EGPD_Output
				? ParentBounds.Right > ChildrenBounds.Left
				: ChildrenBounds.Right > ParentBounds.Left;

			if (bShouldExpand)
			{
				const float Delta = FormatterSettings.FormatterDirection == EGPD_Output
					? ParentBounds.Right - ChildrenBounds.Left + FormatterSettings.Padding.X
					: ParentBounds.Left - ChildrenBounds.Right - FormatterSettings.Padding.X;

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Expanding node %s by %f"), *FBAUtils::GetNodeName(Node), Delta);

				Node->NodePosX += Delta;

				TArray<UEdGraphNode*> AllChildren = Info->GetChildren();
				for (UEdGraphNode* Child : AllChildren)
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tChild %s"), *FBAUtils::GetNodeName(Child));
					Child->NodePosX += Delta;
				}
			}
		}
	}
}

void FSimpleFormatter::FormatY()
{
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Format y?!?!?"));

	TSet<UEdGraphNode*> NodesToCollisionCheck;
	TSet<FPinLink> VisitedLinks;
	TSet<UEdGraphNode*> TempChildren;
	FormatY_Recursive(RootNode, nullptr, nullptr, NodesToCollisionCheck, VisitedLinks, true, TempChildren);
}

void FSimpleFormatter::FormatY_Recursive(
	UEdGraphNode* CurrentNode,
	UEdGraphPin* CurrentPin,
	UEdGraphPin* ParentPin,
	TSet<UEdGraphNode*>& NodesToCollisionCheck,
	TSet<FPinLink>& VisitedLinks,
	bool bSameRow,
	TSet<UEdGraphNode*>& Children)
{
	// 	const FString NodeNameA = CurrentNode == nullptr
	// 	? FString("nullptr")
	// 	: FBAUtils::GetNodeName(CurrentNode);
	// const FString PinNameA = CurrentPin == nullptr ? FString("nullptr") : FBAUtils::GetPinName(CurrentPin);
	// const FString NodeNameB = ParentPin == nullptr
	// 	? FString("nullptr")
	// 	: FBAUtils::GetNodeName(ParentPin->GetOwningNode());
	// const FString PinNameB = ParentPin == nullptr ? FString("nullptr") : FBAUtils::GetPinName(ParentPin);
	//
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("FormatY Next : %s | %s || %s | %s"),
	//        *NodeNameA, *PinNameA,
	//        *NodeNameB, *PinNameB);

	uint16 CollisionLimit = 30;
	while (true)
	{
		if (CollisionLimit <= 0)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("BlueprintAssist: FormatY failed to resolve collision!"));
			break;
		}

		CollisionLimit -= 1;

		bool bNoCollision = true;
		for (UEdGraphNode* NodeToCollisionCheck : NodesToCollisionCheck)
		{
			if (NodeToCollisionCheck == CurrentNode)
			{
				continue;
			}

			TSet<UEdGraphNode*> NodesToMove;

			FSlateRect MyBounds = GetNodeBounds(CurrentNode);
			const FMargin CollisionPadding(0, 0, FormatterSettings.Padding.X * 0.75f, FormatterSettings.Padding.Y);

			FSlateRect OtherBounds = GetNodeBounds(NodeToCollisionCheck);

			OtherBounds = OtherBounds.ExtendBy(CollisionPadding);

			if (FSlateRect::DoRectanglesIntersect(MyBounds.ExtendBy(CollisionPadding), OtherBounds))
			{
				bNoCollision = false;
				const int32 Delta = OtherBounds.Bottom - MyBounds.Top;

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Collision between %d | %s (%s) and %s (%s)"),
				// 	Delta + 1,
				// 	*FBAUtils::GetNodeName(CurrentNode), *MyBounds.ToString(),
				// 	*FBAUtils::GetNodeName(NodeToCollisionCheck), *OtherBounds.ToString());

				if (NodesToMove.Num() > 0)
				{
					for (UEdGraphNode* Node : NodesToMove)
					{
						// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tMoved node relative %s %d"), *FBAUtils::GetNodeName(Node), Delta + 1);
						Node->NodePosY += Delta + 1;
					}
				}
				else
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tMoved node single %s"), *FBAUtils::GetNodeName(CurrentNode));
					CurrentNode->NodePosY += Delta + 1;
				}
			}
		}

		if (bNoCollision)
		{
			break;
		}
	}

	NodesToCollisionCheck.Emplace(CurrentNode);

	TArray<TArray<UEdGraphPin*>> OutputInput;

	const EEdGraphPinDirection Direction = ParentPin == nullptr ? EGPD_Input : ParentPin->Direction.GetValue();

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Pin Direction: %d"), Direction);

	OutputInput.Add(FBAUtils::GetLinkedPins(CurrentNode, Direction));
	OutputInput.Add(FBAUtils::GetLinkedPins(CurrentNode, UEdGraphPin::GetComplementaryDirection(Direction)));

	bool bFirstPin = true;

	UEdGraphPin* MainPin = CurrentPin;

	auto& GraphHandlerCapture = GraphHandler;

	for (TArray<UEdGraphPin*>& Pins : OutputInput)
	{
		UEdGraphPin* LastLinked = CurrentPin;
		UEdGraphPin* LastProcessed = nullptr;

		int DeltaY = 0;
		for (UEdGraphPin* MyPin : Pins)
		{
			TArray<UEdGraphPin*> LinkedPins = MyPin->LinkedTo;

			for (int i = 0; i < LinkedPins.Num(); ++i)
			{
				UEdGraphPin* OtherPin = LinkedPins[i];
				UEdGraphNode* OtherNode = OtherPin->GetOwningNode();
				FPinLink Link(MyPin, OtherPin);

				bool bIsSameLink = Path.Contains(Link);

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tIter Child %s"), *FBAUtils::GetNodeName(OtherNode));
				//
				// if (!bIsSameLink)
				// {
				// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tNot same link!"));
				// }

				if (VisitedLinks.Contains(Link)
					// || !NodePool.Contains(OtherNode)
					|| NodesToCollisionCheck.Contains(OtherNode)
					|| !bIsSameLink)
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tSkipping child"));
					continue;
				}
				VisitedLinks.Add(Link);

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tTaking Child %s"), *FBAUtils::GetNodeName(OtherNode));

				FBAUtils::StraightenPin(GraphHandler, MyPin, OtherPin);

				bool bChildIsSameRow = false;

				if (bFirstPin && (ParentPin == nullptr || MyPin->Direction == ParentPin->Direction))
				{
					bChildIsSameRow = true;
					bFirstPin = false;
					// UE_LOG(LogBlueprintAssist, Error, TEXT("\t\tNode %s is same row as %s"),
					//        *FBAUtils::GetNodeName(OtherNode),
					//        *FBAUtils::GetNodeName(CurrentNode));
				}
				else
				{
					if (LastProcessed != nullptr)
					{
						//UE_LOG(LogBlueprintAssist, Warning, TEXT("Moved node %s to %s"), *FBAUtils::GetNodeName(OtherNode), *FBAUtils::GetNodeName(LastPinOther->GetOwningNode()));
						const int32 NewNodePosY = FMath::Max(OtherNode->NodePosY, LastProcessed->GetOwningNode()->NodePosY);
						FBAUtils::SetNodePosY(GraphHandler, OtherNode, NewNodePosY);
					}
				}

				TSet<UEdGraphNode*> LocalChildren;
				FormatY_Recursive(OtherNode, OtherPin, MyPin, NodesToCollisionCheck, VisitedLinks, bChildIsSameRow, LocalChildren);
				Children.Append(LocalChildren);

				//UE_LOG(LogBlueprintAssist, Warning, TEXT("Local children for %s"), *FBAUtils::GetNodeName(CurrentNode));
				//for (UEdGraphNode* Node : LocalChildren)
				//{
				//	UE_LOG(LogBlueprintAssist, Warning, TEXT("\tChild %s"), *FBAUtils::GetNodeName(Node));
				//}

				if (!bChildIsSameRow && LocalChildren.Num() > 0)
				{
					UEdGraphPin* PinToAvoid = LastLinked;
					if (MainPin != nullptr)
					{
						PinToAvoid = MainPin;
						MainPin = nullptr;
					}

					if (PinToAvoid != nullptr && GetDefault<UBASettings>()->bCustomDebug != 27)
					{
						FSlateRect Bounds = GetNodeArrayBounds(LocalChildren.Array());

						//UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tPin to avoid %s (%s)"), *FBAUtils::GetPinName(PinToAvoid), *FBAUtils::GetPinName(OtherPin));
						const float PinPos = GraphHandler->GetPinY(PinToAvoid) + TrackSpacing;
						const float Delta = PinPos - Bounds.Top;

						if (Delta > 0)
						{
							for (UEdGraphNode* Child : LocalChildren)
							{
								Child->NodePosY += Delta;
							}
						}
					}
				}

				LastProcessed = OtherPin;
			}

			LastLinked = MyPin;

			DeltaY += 1;
		}
	}

	Children.Add(CurrentNode);

	if (bSameRow && ParentPin != nullptr)
	{
		//UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\tStraightening pin from %s to %s"),
		//       *FBAUtils::GetPinName(CurrentPin),
		//       *FBAUtils::GetPinName(ParentPin));
		SameRowMapping.Add(FPinLink(CurrentPin, ParentPin));
		SameRowMapping.Add(FPinLink(ParentPin, CurrentPin));
		FBAUtils::StraightenPin(GraphHandler, CurrentPin, ParentPin);
	}
}

TSet<UEdGraphNode*> FSimpleFormatter::GetFormattedNodes()
{
	return FormattedNodes;
}

void FSimpleFormatter::ApplyCommentPaddingX()
{
	// UE_LOG(LogTemp, Warning, TEXT("EXPAND COMMENTS X Comments"));
	TArray<UEdGraphNode*> Contains = GetFormattedNodes().Array();

	TArray<UEdGraphNode_Comment*> Comments = CommentHandler.GetComments().Array();
	// UE_LOG(LogTemp, Warning, TEXT("Initial comments %d"), Comments.Num());
	Comments.RemoveAll([&](UEdGraphNode_Comment* Comment)
	{
		TArray<UEdGraphNode*> NodesUnderComment = FBAUtils::GetNodesUnderComment(Comment);

		const bool bContainsNone = !FBAUtils::GetNodesUnderComment(Comment).ContainsByPredicate([&](UEdGraphNode* Node)
		{
			return Contains.Contains(Node);
		});

		if (bContainsNone)
		{
			return true;
		}

		return false;
	});

	Contains.Append(Comments);

	// UE_LOG(LogTemp, Warning, TEXT("EXPAND COMMENTS X Comments %d"), Comments.Num());

	TSet<UEdGraphNode*> Temp;
	ApplyCommentPaddingX_Recursive(Contains, Temp);
}

void FSimpleFormatter::ApplyCommentPaddingX_Recursive(TArray<UEdGraphNode*> NodeSet, TSet<UEdGraphNode*>& OutHandledNodes)
{
	const auto LeftMost = [&](UEdGraphNode& NodeA, UEdGraphNode& NodeB)
	{
		const float LeftA = GetNodeBounds(&NodeA).Left;
		const float LeftB = GetNodeBounds(&NodeB).Left;
		return FormatterSettings.FormatterDirection == EGPD_Output
			? LeftA < LeftB
			: LeftA > LeftB;
	};

	NodeSet.StableSort(LeftMost);

	TSet<UEdGraphNode*> HandledNodes;
	TMap<UEdGraphNode_Comment*, TSet<UEdGraphNode*>> CommentContains;

	TArray<UEdGraphNode_Comment*> Comments = FBAUtils::GetNodesOfClass<UEdGraphNode_Comment>(NodeSet);
	Comments.StableSort([&](const UEdGraphNode_Comment& CommentA, const UEdGraphNode_Comment& CommentB)
	{
		return CommentHandler.GetCommentDepth(&CommentA) < CommentHandler.GetCommentDepth(&CommentB);
	});

	for (UEdGraphNode_Comment* Comment : Comments)
	{
		if (HandledNodes.Contains(Comment))
		{
			continue;
		}

		TSet<UEdGraphNode*> LocalHandled;
		ApplyCommentPaddingX_Recursive(CommentHandler.GetNodesUnderComments(Comment), LocalHandled);
		HandledNodes.Append(LocalHandled);
		CommentContains.Add(Comment, LocalHandled);
	}

	OutHandledNodes.Append(NodeSet);

	// Remove all handled nodes from subgraphs
	NodeSet.RemoveAll([&HandledNodes](UEdGraphNode* Node) { return HandledNodes.Contains(Node); });
	Comments.RemoveAll([&HandledNodes](UEdGraphNode* Node) { return HandledNodes.Contains(Node); });

	// UE_LOG(LogTemp, Warning, TEXT("Format SubGraph"));
	// for (UEdGraphNode* Node : NodeSet)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Node));
	// }

	for (UEdGraphNode* NodeA : NodeSet)
	{
		// collide only with our children
		TSet<TSharedPtr<FFormatXInfo>> Children;
		if (UEdGraphNode_Comment* CommentA = Cast<UEdGraphNode_Comment>(NodeA))
		{
			for (UEdGraphNode* Node : CommentContains[CommentA])
			{
				if (TSharedPtr<FFormatXInfo> FormatXInfo = FormatXInfoMap.FindRef(Node))
				{
					Children.Append(FormatXInfo->Children);
				}
			}
		}
		else
		{
			if (TSharedPtr<FFormatXInfo> FormatXInfo = FormatXInfoMap.FindRef(NodeA))
			{
				Children.Append(FormatXInfo->Children);
			}
		}

		// UE_LOG(LogTemp, Warning, TEXT("Children for node %s"), *FBAUtils::GetNodeName(NodeA));
		// for (TSharedPtr<FFormatXInfo> Info : Children)
		// {
		// 	UE_LOG(LogTemp, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Info->GetNode()));
		// }

		for (TSharedPtr<FFormatXInfo> Info : Children)
		{
			UEdGraphNode* NodeB = Info->GetNode();

			// UE_LOG(LogTemp, Warning, TEXT("TRY {%s} Checking {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));

			if (!SameRowMapping.Contains(Info->Link))
			{
				// UE_LOG(LogTemp, Warning, TEXT("\tNOt same row skipping"));
				continue;
			}

			if (!NodeSet.Contains(NodeB))
			{
				bool bHasContainingComment = false;
				for (auto Comment : Comments)
				{
					if (CommentHandler.DoesCommentContainNode(Comment, NodeB))
					{
						NodeB = Comment;
						bHasContainingComment = true;
						break;
					}
				}

				if (!bHasContainingComment)
				{
					// UE_LOG(LogTemp, Warning, TEXT("\tSkip not in nodeset"));
					continue;
				}
			}

			if (NodeA == NodeB)
			{
				continue;
			}

			UEdGraphNode_Comment* CommentA = Cast<UEdGraphNode_Comment>(NodeA);
			UEdGraphNode_Comment* CommentB = Cast<UEdGraphNode_Comment>(NodeB);

			// only collision check comment nodes
			if (!CommentA && !CommentB)
			{
				continue;
			}

			if (CommentA && CommentB)
			{
				if (FCommentHandler::AreCommentsIntersecting(CommentA, CommentB))
				{
					// UE_LOG(LogTemp, Warning, TEXT("\tSkip comments intersecting"));
					continue;
				}
			}

			FSlateRect BoundsA = GetNodeBounds(NodeA).ExtendBy(FMargin(FormatterSettings.Padding.X, 0.f));
			FSlateRect BoundsB = GetNodeBounds(NodeB);

			if (CommentA)
			{
				BoundsA = CommentHandler.GetCommentBounds(CommentA).ExtendBy(FMargin(FormatterSettings.Padding.X, 0.f));
			}

			if (CommentB)
			{
				BoundsB = CommentHandler.GetCommentBounds(CommentB);
			}

			// UE_LOG(LogTemp, Warning, TEXT("{%s} Checking {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));

			if (FSlateRect::DoRectanglesIntersect(BoundsA, BoundsB))
			{
				const float Delta = Info->Link.GetDirection() == EGPD_Output ?
					BoundsA.Right + 1.0f - BoundsB.Left :
					BoundsA.Left - BoundsB.Right;

				if (CommentB)
				{
					TSet<UEdGraphNode*> AllChildren;
					for (auto Node : CommentContains[CommentB].Array())
					{
						if (!FormatXInfoMap.Contains(Node))
						{
							continue;
						}

						AllChildren.Add(Node);
						AllChildren.Append(FormatXInfoMap[Node]->GetChildren());
					}

					// UE_LOG(LogTemp, Warning, TEXT("\tNode {%s} Colliding with COMMENT {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));

					for (auto Child : AllChildren)
					{
						Child->NodePosX += Delta;
						// UE_LOG(LogTemp, Warning, TEXT("\tMove child %s"), *FBAUtils::GetNodeName(Child));
					}

					// UE_LOG(LogTemp, Warning, TEXT("CommentBounds %s"), *BoundsB.ToString());
					// UE_LOG(LogTemp, Warning, TEXT("RegularBounds %s"), *FBAUtils::GetCachedNodeArrayBounds(GraphHandler, CommentHandler.CommentNodesContains[CommentB]).ToString());
				}
				else
				{
					if (!FormatXInfoMap.Contains(NodeB))
					{
						continue;
					}

					// UE_LOG(LogTemp, Warning, TEXT("COMMENT {%s} Colliding with Node {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));

					NodeB->NodePosX += Delta;
					for (auto Child : FormatXInfoMap[NodeB]->GetChildren())
					{
						Child->NodePosX += Delta;
						// UE_LOG(LogTemp, Warning, TEXT("\tMove child %s"), *FBAUtils::GetNodeName(Child));
					}
				}
			}
		}
	}
}

void FSimpleFormatter::ApplyCommentPaddingY()
{
	// UE_LOG(LogTemp, Error, TEXT("EXPAND COMMENTS Y"));
	TArray<UEdGraphNode*> Contains = GetFormattedNodes().Array();
	TArray<UEdGraphNode_Comment*> Comments = CommentHandler.GetComments().Array();
	Comments.RemoveAll([&Contains](UEdGraphNode_Comment* Comment)
	{
		return !FBAUtils::GetNodesUnderComment(Comment).ContainsByPredicate([&Contains](UEdGraphNode* Node)
		{
			return Contains.Contains(Node);
		});
	});

	// UE_LOG(LogTemp, Warning, TEXT("ALL Comments:"));
	// for (auto Comment : Comments)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Comment));
	// }
	// UE_LOG(LogTemp, Warning, TEXT("END Comments:"));

	Contains.Append(Comments);

	TSet<UEdGraphNode*> Temp;
	ApplyCommentPaddingY_Recursive(Contains, Temp);
	// UE_LOG(LogTemp, Error, TEXT("END EXPAND COMMENTS Y"));
}

void FSimpleFormatter::ApplyCommentPaddingY_Recursive(TArray<UEdGraphNode*> NodeSet, TSet<UEdGraphNode*>& OutHandledNodes)
{
	NodeSet.StableSort([&](UEdGraphNode& NodeA, UEdGraphNode& NodeB)
	{
		float TopA = GetNodeBounds(&NodeA).Top;
		if (auto Comment = Cast<UEdGraphNode_Comment>(&NodeA))
		{
			auto Nodes = FBAUtils::GetNodesUnderComment(Comment);
			Nodes.RemoveAll(FBAUtils::IsCommentNode);
			TopA = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, Nodes).Top;
		}

		float TopB = GetNodeBounds(&NodeB).Top;
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(&NodeB))
		{
			auto Nodes = FBAUtils::GetNodesUnderComment(Comment);
			Nodes.RemoveAll(FBAUtils::IsCommentNode);
			TopB = FBAUtils::GetCachedNodeArrayBounds(GraphHandler, Nodes).Top;
		}
		return TopA < TopB;
	});

	TSet<UEdGraphNode*> HandledNodes;
	TMap<UEdGraphNode_Comment*, TSet<UEdGraphNode*>> CommentContains;

	TArray<UEdGraphNode_Comment*> Comments = FBAUtils::GetNodesOfClass<UEdGraphNode_Comment>(NodeSet);
	Comments.StableSort([&](const UEdGraphNode_Comment& CommentA, const UEdGraphNode_Comment& CommentB)
	{
		return CommentHandler.GetCommentDepth(&CommentA) < CommentHandler.GetCommentDepth(&CommentB);
	});

	for (UEdGraphNode_Comment* Comment : Comments)
	{
		if (HandledNodes.Contains(Comment))
		{
			continue;
		}

		TSet<UEdGraphNode*> LocalHandled;
		ApplyCommentPaddingY_Recursive(CommentHandler.GetNodesUnderComments(Comment), LocalHandled);
		HandledNodes.Append(LocalHandled);
		CommentContains.Add(Comment, LocalHandled);
	}

	// for (auto kvp: CommentContains)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("%s"), *FBAUtils::GetNodeName(kvp.Key));
	// 	for (UEdGraphNode* Node : kvp.Value)
	// 	{
	// 		UE_LOG(LogTemp, Warning, TEXT("Contains %s"), *FBAUtils::GetNodeName(Node));
	// 	}
	// }

	OutHandledNodes.Append(NodeSet);

	// Remove all handled nodes from subgraphs
	NodeSet.RemoveAll([&HandledNodes](UEdGraphNode* Node) { return HandledNodes.Contains(Node); });
	Comments.RemoveAll([&HandledNodes](UEdGraphNode* Node) { return HandledNodes.Contains(Node); });

	// UE_LOG(LogTemp, Warning, TEXT("Format SubGraph"));
	// for (UEdGraphNode* Node : NodeSet)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("\t%s"), *FBAUtils::GetNodeName(Node));
	// }

	for (UEdGraphNode* NodeA : NodeSet)
	{
		for (UEdGraphNode* NodeB : NodeSet)
		{
			if (NodeA == NodeB)
			{
				continue;
			}

			UEdGraphNode_Comment* CommentA = Cast<UEdGraphNode_Comment>(NodeA);
			UEdGraphNode_Comment* CommentB = Cast<UEdGraphNode_Comment>(NodeB);

			if (CommentA && CommentB)
			{
				if (FCommentHandler::AreCommentsIntersecting(CommentA, CommentB))
				{
					continue;
				}
			}

			FSlateRect BoundsA = GetNodeBounds(NodeA).ExtendBy(FMargin(0, FormatterSettings.Padding.Y));
			FSlateRect BoundsB = GetNodeBounds(NodeB);

			// UE_LOG(LogTemp, Warning, TEXT("{%s} Checking {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));

			if (FSlateRect::DoRectanglesIntersect(BoundsA, BoundsB))
			{
				const float Delta = BoundsA.Bottom + 1.0f - BoundsB.Top;
				if (CommentB)
				{
					// TSet<UEdGraphNode*> NodesToMove;
					// for (UEdGraphNode* Node : CommentContains[CommentB])
					// {
					// 	NodesToMove.Add(Node);
					// 	if (TSharedPtr<FFormatXInfo> Info = FormatXInfoMap.FindRef(Node))
					// 	{
					// 		NodesToMove.Append(Info->GetChildren());
					// 	}
					// }
					//
					// for (UEdGraphNode* Node : NodesToMove)
					// {
					// 	Node->NodePosY += Delta;
					// }

					// for (UEdGraphNode* Node : FBAUtils::GetNodesUnderComment(CommentB))
					for (UEdGraphNode* Node : CommentContains[CommentB])
					{
						Node->NodePosY += Delta;
					}

					// for (UEdGraphNode* Node : FBAUtils::GetNodesUnderComment(CommentB))
					for (UEdGraphNode* Node : CommentContains[CommentB])
					{
						FBAFormatterUtils::StraightenRow(GraphHandler, SameRowMapping, Node);
						// FBAFormatterUtils::StraightenRowWithFilter(GraphHandler, SameRowMapping, Node, [&](const FPinLink& Link) { return NodeSet.Contains(Link.GetNode()); });
					}

					// UE_LOG(LogTemp, Warning, TEXT("\t{%s} Colliding with COMMENT {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));
					// UE_LOG(LogTemp, Warning, TEXT("CommentBounds %s"), *BoundsB.ToString());
					// UE_LOG(LogTemp, Warning, TEXT("RegularBounds %s"), *FBAUtils::GetCachedNodeArrayBounds(GraphHandler, CommentHandler.CommentNodesContains[CommentB]).ToString());
				}
				else
				{
					NodeB->NodePosY += Delta;
					FBAFormatterUtils::StraightenRowWithFilter(GraphHandler, SameRowMapping, NodeB, [&](const FPinLink& Link) { return NodeSet.Contains(Link.GetNode()); });

					// NodeB->NodePosY += Delta;
					// if (TSharedPtr<FFormatXInfo> Info = FormatXInfoMap.FindRef(NodeB))
					// {
					// 	for (UEdGraphNode* Child : Info->GetChildren())
					// 	{
					// 		Child->NodePosY += Delta;
					// 	}
					// }

					// UE_LOG(LogTemp, Warning, TEXT("\t{%s} Colliding with NODE {%s}"), *FBAUtils::GetNodeName(NodeA), *FBAUtils::GetNodeName(NodeB));
				}
			}
		}
	}
}
