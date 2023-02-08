// Copyright 2021 fpwong. All Rights Reserved.

#include "KnotTrackCreator.h"

#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistUtils.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_Knot.h"
#include "BlueprintAssist/GraphFormatters/BlueprintAssistCommentHandler.h"
#include "BlueprintAssist/GraphFormatters/FormatterInterface.h"
#include "Kismet2/BlueprintEditorUtils.h"

void FKnotTrackCreator::Init(TSharedPtr<FFormatterInterface> InFormatter, TSharedPtr<FBAGraphHandler> InGraphHandler)
{
	Formatter = InFormatter;
	GraphHandler = InGraphHandler;

	const UBASettings* BASettings = GetDefault<UBASettings>();
	NodePadding = BASettings->BlueprintFormatterSettings.Padding;
	PinPadding = BASettings->BlueprintParameterPadding;
	TrackSpacing = BASettings->BlueprintKnotTrackSpacing;
}

void FKnotTrackCreator::FormatKnotNodes()
{
	//UE_LOG(LogBlueprintAssist, Warning, TEXT("### Format Knot Nodes"));

	MakeKnotTrack();

	MergeNearbyKnotTracks();

	ExpandKnotTracks();

	RemoveUselessCreationNodes();

	CreateKnotTracks();

	if (GetDefault<UBASettings>()->bAddKnotNodesToComments)
	{
		AddKnotNodesToComments();
	}
}

void FKnotTrackCreator::CreateKnotTracks()
{
	// we sort tracks by
	// 1. exec pin track over parameter track 
	// 2. top-most-track-height 
	// 3. top-most parent pin 
	// 4. left-most
	const auto& TrackSorter = [](const TSharedPtr<FKnotNodeTrack> TrackA, const TSharedPtr<FKnotNodeTrack> TrackB)
	{
		const bool bIsExecPinA = FBAUtils::IsExecPin(TrackA->GetLastPin());
		const bool bIsExecPinB = FBAUtils::IsExecPin(TrackB->GetLastPin());

		if (bIsExecPinA != bIsExecPinB)
		{
			return bIsExecPinA > bIsExecPinB;
		}

		if (TrackA->GetTrackHeight() != TrackB->GetTrackHeight())
		{
			return TrackA->GetTrackHeight() < TrackB->GetTrackHeight();
		}

		if (TrackA->ParentPinPos.Y != TrackB->ParentPinPos.Y)
		{
			return TrackA->ParentPinPos.Y < TrackB->ParentPinPos.Y;
		}

		return TrackA->GetTrackBounds().GetSize().X < TrackB->GetTrackBounds().GetSize().X;
	};
	KnotTracks.Sort(TrackSorter);

	// we need to save the pin height since creating knot nodes for certain nodes (K2Node_LatentAbilityCall) will cause
	// the node to recreate its pins (including their guids) and so we fail to find the cached pin height 
	TMap<TSharedPtr<FKnotNodeTrack>, float> SavedPinHeight;
	for (TSharedPtr<FKnotNodeTrack> KnotTrack : KnotTracks)
	{
		UEdGraphPin* PinToAlignTo = KnotTrack->GetPinToAlignTo();
		if (PinToAlignTo != nullptr)
		{
			SavedPinHeight.Add(KnotTrack, GraphHandler->GetPinY(PinToAlignTo));
		}
	}

	for (TSharedPtr<FKnotNodeTrack> KnotTrack : KnotTracks)
	{
		// sort knot creations
		auto GraphCapture = GraphHandler->GetFocusedEdGraph();
		const auto CreationSorter = [GraphCapture](TSharedPtr<FKnotNodeCreation> CreationA, TSharedPtr<FKnotNodeCreation> CreationB)
		{
			UEdGraphPin* Pin = CreationA->PinToConnectToHandle.GetPin();

			if (FBAUtils::IsExecPin(Pin))
			{
				return CreationA->KnotPos.X > CreationB->KnotPos.X;
			}

			return CreationA->KnotPos.X < CreationB->KnotPos.X;
		};

		if (!KnotTrack->bIsLoopingTrack)
		{
			KnotTrack->KnotCreations.StableSort(CreationSorter);
		}

		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Creating knot track %s"), *FBAUtils::GetPinName(KnotTrack->ParentPin));
		// if (KnotTrack->PinToAlignTo.IsValid())
		// {
		// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\tShould make aligned track!"));
		// }


		TSharedPtr<FKnotNodeCreation> LastCreation = nullptr;
		const int NumCreations = KnotTrack->KnotCreations.Num();
		for (int i = 0; i < NumCreations; i++)
		{
			TSharedPtr<FKnotNodeCreation> Creation = KnotTrack->KnotCreations[i];

			FVector2D KnotPos = Creation->KnotPos;

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Making knot creation at %s %d"), *KnotPos.ToString(), i);
			// for (auto Pin : Creation->PinHandlesToConnectTo)
			// {
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tPin %s"), *FBAUtils::GetPinName(FBAUtils::GetPinFromGraph(Pin, GraphHandler->GetFocusedEdGraph())));
			// }

			UEdGraphPin* PinToAlignTo = KnotTrack->GetPinToAlignTo();
			if (PinToAlignTo != nullptr)
			{
				KnotPos.Y = SavedPinHeight.Contains(KnotTrack) ? SavedPinHeight[KnotTrack] : GraphHandler->GetPinY(PinToAlignTo);
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Created knot aligned to %s %f"), *FBAUtils::GetPinName(PinToAlignTo), KnotPos.Y);
			}

			if (!LastCreation.IsValid()) // create a knot linked to the first pin (the fallback pin)
			{
				UEdGraphPin* ParentPin = FBAGraphPinHandle(KnotTrack->ParentPin).GetPin();
				UK2Node_Knot* KnotNode = CreateKnotNode(Creation.Get(), KnotPos, ParentPin);
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Create initial %s"), *FBAUtils::GetPinName(ParentPin));

				KnotNodesSet.Add(KnotNode);
				LastCreation = Creation;
			}
			else // create a knot that connects to the last knot
			{
				UK2Node_Knot* ParentKnot = LastCreation->CreatedKnot;

				const bool bCreatePinAlignedKnot = LastCreation->PinHandlesToConnectTo.Num() == 1 && PinToAlignTo != nullptr;
				if (bCreatePinAlignedKnot && NumCreations == 1) // move the parent knot to the aligned x position
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("Create pin aligned!"));
					for (FBAGraphPinHandle& PinHandle : Creation->PinHandlesToConnectTo)
					{
						UEdGraphPin* Pin = PinHandle.GetPin();

						UEdGraphPin* ParentPin = Pin->Direction == EGPD_Input
							? ParentKnot->GetOutputPin()
							: ParentKnot->GetInputPin();
						FBAUtils::TryCreateConnection(ParentPin, Pin);
					}
				}
				else
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("Create normal"));
					UEdGraphPin* LastPin = KnotTrack->GetLastPin();

					UEdGraphPin* PinOnLastKnot = LastPin->Direction == EGPD_Output
						? ParentKnot->GetInputPin()
						: ParentKnot->GetOutputPin();

					UK2Node_Knot* NewKnot = CreateKnotNode(Creation.Get(), KnotPos, PinOnLastKnot);
					KnotNodesSet.Add(NewKnot);

					LastCreation = Creation;
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(GraphHandler->GetBlueprint());

	// Cleanup knot node pool
	for (auto KnotNode : KnotNodePool)
	{
		if (FBAUtils::GetLinkedNodes(KnotNode).Num() == 0)
		{
			FBAUtils::DeleteNode(KnotNode);
		}
	}
	KnotNodePool.Empty();
}

void FKnotTrackCreator::ExpandKnotTracks()
{
	// UE_LOG(LogBlueprintAssist, Error, TEXT("### Expanding Knot Tracks"));
	// for (auto Elem : KnotTracks)
	// {
	// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("KnotTrack %s"), *FBAUtils::GetPinName(Elem->GetParentPin()));
	// }

	// sort tracks by:
	// 1. exec over parameter
	// 2. Highest track Y
	// 3. Smallest track width
	// 4. Parent pin height
	TSharedPtr<FBAGraphHandler> GraphHandlerCapture = GraphHandler;
	const auto& ExpandTrackSorter = [GraphHandlerCapture](const TSharedPtr<FKnotNodeTrack>& TrackA, const TSharedPtr<FKnotNodeTrack>& TrackB)
	{
		const bool bIsExecPinA = FBAUtils::IsExecPin(TrackA->GetLastPin());
		const bool bIsExecPinB = FBAUtils::IsExecPin(TrackB->GetLastPin());
		if (bIsExecPinA != bIsExecPinB)
		{
			return bIsExecPinA > bIsExecPinB;
		}

		if (bIsExecPinA && TrackA->bIsLoopingTrack != TrackB->bIsLoopingTrack)
		{
			return TrackA->bIsLoopingTrack < TrackB->bIsLoopingTrack;
		}

		if (TrackA->GetTrackHeight() != TrackB->GetTrackHeight())
		{
			return TrackA->bIsLoopingTrack
				? TrackA->GetTrackHeight() > TrackB->GetTrackHeight()
				: TrackA->GetTrackHeight() < TrackB->GetTrackHeight();
		}

		const float WidthA = TrackA->GetTrackBounds().GetSize().X;
		const float WidthB = TrackB->GetTrackBounds().GetSize().X;
		if (WidthA != WidthB)
		{
			return TrackA->bIsLoopingTrack
				? WidthA > WidthB
				: WidthA < WidthB;
		}

		return GraphHandlerCapture->GetPinY(TrackA->GetLastPin()) < GraphHandlerCapture->GetPinY(TrackB->GetLastPin());
	};

	const auto& OverlappingTrackSorter = [GraphHandlerCapture](const TSharedPtr<FKnotNodeTrack>& TrackA, const TSharedPtr<FKnotNodeTrack>& TrackB)
	{
		if (TrackA->bIsLoopingTrack != TrackB->bIsLoopingTrack)
		{
			return TrackA->bIsLoopingTrack < TrackB->bIsLoopingTrack;
		}

		const bool bIsExecPinA = FBAUtils::IsExecPin(TrackA->GetLastPin());
		const bool bIsExecPinB = FBAUtils::IsExecPin(TrackB->GetLastPin());
		if (bIsExecPinA != bIsExecPinB)
		{
			return bIsExecPinA > bIsExecPinB;
		}

		const float WidthA = TrackA->GetTrackBounds().GetSize().X;
		const float WidthB = TrackB->GetTrackBounds().GetSize().X;
		if (WidthA != WidthB)
		{
			return TrackA->bIsLoopingTrack
				? WidthA > WidthB
				: WidthA < WidthB;
		}

		return GraphHandlerCapture->GetPinY(TrackA->GetLastPin()) < GraphHandlerCapture->GetPinY(TrackB->GetLastPin());
	};

	TArray<TSharedPtr<FKnotNodeTrack>> SortedTracks = KnotTracks;
	SortedTracks.StableSort(ExpandTrackSorter);

	TArray<TSharedPtr<FKnotNodeTrack>> PendingTracks = SortedTracks;

	// for (auto Track : SortedTracks)
	// {
	// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("Expanding tracks %s"), *Track->ToString());
	// }

	TSet<TSharedPtr<FKnotNodeTrack>> PlacedTracks;
	while (PendingTracks.Num() > 0)
	{
		TSharedPtr<FKnotNodeTrack> CurrentTrack = PendingTracks[0];
		PlacedTracks.Add(CurrentTrack);

		const float TrackY = CurrentTrack->GetTrackHeight();

		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Process pending Track %s (%s)"), *FBAUtils::GetPinName(CurrentTrack->ParentPin), *FBAUtils::GetNodeName(CurrentTrack->ParentPin->GetOwningNode()));

		// check against all other tracks, and find ones which overlap with the current track
		TArray<TSharedPtr<FKnotNodeTrack>> OverlappingTracks;
		OverlappingTracks.Add(CurrentTrack);

		float CurrentLowestTrackHeight = CurrentTrack->GetTrackHeight();
		FSlateRect OverlappingBounds = CurrentTrack->GetTrackBounds();
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Current Track bounds %s"), *CurrentTrack->GetTrackBounds().ToString());
		bool bFoundCollision = true;
		do
		{
			bFoundCollision = false;
			for (TSharedPtr<FKnotNodeTrack> Track : SortedTracks)
			{
				if (OverlappingTracks.Contains(Track))
				{
					continue;
				}

				FSlateRect TrackBounds = Track->GetTrackBounds();

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tOverlapping Bounds %s | %s"), *OverlappingBounds.ToString(), *TrackBounds.ToString());
				if (FSlateRect::DoRectanglesIntersect(OverlappingBounds, TrackBounds))
				{
					OverlappingTracks.Add(Track);
					PlacedTracks.Add(Track);
					bFoundCollision = true;

					OverlappingBounds.Top = FMath::Min(Track->GetTrackHeight(), OverlappingBounds.Top);
					OverlappingBounds.Left = FMath::Min(TrackBounds.Left, OverlappingBounds.Left);
					OverlappingBounds.Right = FMath::Max(TrackBounds.Right, OverlappingBounds.Right);
					OverlappingBounds.Bottom = OverlappingBounds.Top + (OverlappingTracks.Num() * TrackSpacing);

					if (CurrentTrack->HasPinToAlignTo())
					{
						CurrentTrack->PinToAlignTo.SetPin(nullptr);
					}

					if (Track->HasPinToAlignTo())
					{
						// UE_LOG(LogBlueprintAssist, Warning, TEXT("Removed pin to align to for %s"), *Track->ToString());
						Track->PinToAlignTo.SetPin(nullptr);
					}

					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tTrack %s colliding %s"), *FBAUtils::GetPinName(Track->ParentPin), *FBAUtils::GetPinName(CurrentTrack->ParentPin));
				}
			}
		}
		while (bFoundCollision);

		if (OverlappingTracks.Num() == 1)
		{
			PendingTracks.Remove(CurrentTrack);
			continue;
		}

		bool bOverlappingLoopingTrack = false;
		TArray<TSharedPtr<FKnotNodeTrack>> ExecTracks;

		// Group overlapping tracks by node (expect for exec tracks)
		TArray<FGroupedTracks> OverlappingGroupedTracks;
		for (TSharedPtr<FKnotNodeTrack> Track : OverlappingTracks)
		{
			if (FBAUtils::IsExecPin(Track->ParentPin) && !Track->bIsLoopingTrack)
			{
				ExecTracks.Add(Track);
				continue;
			}

			if (!Track->bIsLoopingTrack)
			{
				bOverlappingLoopingTrack = true;
			}

			const auto MatchesNode = [&Track](const FGroupedTracks& OtherTrack)
			{
				return Track->ParentPin->GetOwningNode() == OtherTrack.ParentNode;
			};

			FGroupedTracks* Group = OverlappingGroupedTracks.FindByPredicate(MatchesNode);
			if (Group)
			{
				Group->Tracks.Add(Track);
			}
			else
			{
				FGroupedTracks NewGroup;
				NewGroup.ParentNode = Track->ParentPin->GetOwningNode();
				NewGroup.Tracks.Add(Track);
				OverlappingGroupedTracks.Add(NewGroup);
			}
		}

		ExecTracks.StableSort(OverlappingTrackSorter);

		for (auto& Group : OverlappingGroupedTracks)
		{
			Group.Init();
			Group.Tracks.StableSort(OverlappingTrackSorter);
		}

		const auto& GroupSorter = [](const FGroupedTracks& GroupA, const FGroupedTracks& GroupB)
		{
			if (GroupA.bLooping != GroupB.bLooping)
			{
				return GroupA.bLooping < GroupB.bLooping;
			}

			return GroupA.Width < GroupB.Width;
		};
		OverlappingGroupedTracks.StableSort(GroupSorter);

		int TrackCount = 0;
		for (auto Track : ExecTracks)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tTrack %s"), *Track->ToString());
			Track->UpdateTrackHeight(CurrentLowestTrackHeight + (TrackCount * TrackSpacing));
			TrackCount += 1;
		}

		for (FGroupedTracks& Group : OverlappingGroupedTracks)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Group %s"), *FBAUtils::GetNodeName(Group.ParentNode));
			for (TSharedPtr<FKnotNodeTrack> Track : Group.Tracks)
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tTrack %s"), *Track->ToString());
				Track->UpdateTrackHeight(CurrentLowestTrackHeight + (TrackCount * TrackSpacing));
				TrackCount += 1;
			}
		}

		for (TSharedPtr<FKnotNodeTrack> Track : PlacedTracks)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tPlaced track %s"), *Track->ToString());
			PendingTracks.Remove(Track);
		}

		TSet<UEdGraphNode*> TrackNodes = CurrentTrack->GetNodes(GraphHandler->GetFocusedEdGraph());

		FSlateRect ExpandedBounds = OverlappingBounds;
		const float Padding = bOverlappingLoopingTrack ? TrackSpacing * 2 : TrackSpacing;
		ExpandedBounds.Bottom += Padding;

		// find the top of the tallest node the track block is colliding with
		bool bAnyCollision = false;
		float CollisionTop = MAX_flt;

		// collide against nodes
		for (UEdGraphNode* Node : Formatter->GetFormattedNodes())
		{
			// if (Node == CurrentTrack->LinkedTo[0]->GetOwningNode() || Node == CurrentTrack->GetLastPin()->GetOwningNode())
			// 	continue;

			bool bSkipNode = false;
			for (TSharedPtr<FKnotNodeTrack> Track : PlacedTracks)
			{
				if (Node == Track->ParentPin->GetOwningNode() || Node == Track->GetLastPin()->GetOwningNode())
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("Skipping node %s"), *FBAUtils::GetNodeName(Node));
					bSkipNode = true;
					break;
				}

				if (auto AlignedPin = Track->GetPinToAlignTo())
				{
					if (Node == AlignedPin->GetOwningNode())
					{
						// UE_LOG(LogBlueprintAssist, Warning, TEXT("Skipping node aligned %s"), *FBAUtils::GetNodeName(Node));
						bSkipNode = true;
						break;
					}
				}
			}

			if (bSkipNode)
			{
				continue;
			}

			const FSlateRect NodeBounds = GraphHandler->GetCachedNodeBounds(Node);
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Checking collision for %s | %s | %s"), *FBAUtils::GetNodeName(Node), *NodeBounds.ToString(), *ExpandedBounds.ToString());

			if (FSlateRect::DoRectanglesIntersect(NodeBounds, ExpandedBounds))
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Collision with %s"), *FBAUtils::GetNodeName(Node));
				bAnyCollision = true;
				CollisionTop = FMath::Min(NodeBounds.Top, CollisionTop);
			}
		}

		if (!bAnyCollision)
		{
			continue;
		}

		if (GetDefault<UBASettings>()->bCustomDebug == 200)
		{
			continue;
		}

		float Delta = ExpandedBounds.Bottom - CollisionTop;
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("### Moving nodes down | Delta %f"), Delta);

		// move all nodes below the track block
		TSet<UEdGraphNode*> MovedNodes;
		for (UEdGraphNode* Node : Formatter->GetFormattedNodes())
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tChecking Node for collision %s | My %d | Track %f"), *FBAUtils::GetNodeName(Node), Node->NodePosY, TrackY);

			if (Node->NodePosY > TrackY)
			{
				Node->NodePosY += Delta;
				MovedNodes.Add(Node);
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t Moved node %s by delta %f"), *FBAUtils::GetNodeName(Node), Delta);
			}
		}

		// Update other tracks since their nodes may have moved
		for (TSharedPtr<FKnotNodeTrack> Track : SortedTracks)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("CHECKING track %s"), *Track->ToString());
			if (PlacedTracks.Contains(Track))
			{
				continue;
			}

			if (Track->HasPinToAlignTo()) // if we are aligned to a pin, update our track y when a node moves
			{
				if (MovedNodes.Contains(Track->GetLastPin()->GetOwningNode()) || MovedNodes.Contains(Track->ParentPin->GetOwningNode()))
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tMoved Aligned Track for node delta %s"), *FBAUtils::GetNodeName(Track->GetLastPin()->GetOwningNode()));
					Track->UpdateTrackHeight(Track->GetTrackHeight() + Delta);
				}
			}
			else if (Track->GetTrackHeight() > TrackY)
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tMoved BELOW Track for node delta %s"), *FBAUtils::GetNodeName(Track->GetLastPin()->GetOwningNode()));
				Track->UpdateTrackHeight(Track->GetTrackHeight() + Delta);
			}
		}
	}
}

void FKnotTrackCreator::RemoveUselessCreationNodes()
{
	for (TSharedPtr<FKnotNodeTrack> Track : KnotTracks)
	{
		TSharedPtr<FKnotNodeCreation> LastCreation;
		TArray<TSharedPtr<FKnotNodeCreation>> CreationsCopy = Track->KnotCreations;
		for (TSharedPtr<FKnotNodeCreation> Creation : CreationsCopy)
		{
			if (Track->KnotCreations.Num() == 1)
			{
				break;
			}

			const bool bHasOneConnection = Creation->PinHandlesToConnectTo.Num() == 1;
			if (bHasOneConnection)
			{
				const float PinHeight = GraphHandler->GetPinY(Creation->GetPinToConnectTo());
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Pin %s %f | %f"), *FBAUtils::GetPinName(MainPin), PinHeight, Track->GetTrackHeight());

				if (PinHeight == Track->GetTrackHeight())
				{
					if (LastCreation)
					{
						LastCreation->PinHandlesToConnectTo.Add(Creation->PinToConnectToHandle);
					}

					Track->KnotCreations.Remove(Creation);
				}
			}

			LastCreation = Creation;
		}
	}
}

void FKnotTrackCreator::RemoveKnotNodes(const TArray<UEdGraphNode*>& NodeTree)
{
	TArray<UEdGraphNode_Comment*> CommentNodes = FBAUtils::GetCommentNodesFromGraph(GraphHandler->GetFocusedEdGraph());
	for (UEdGraphNode* Node : NodeTree)
	{
		/** Delete all connections for each knot node */
		if (UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(Node))
		{
			FBAUtils::DisconnectKnotNode(KnotNode);

			for (auto Comment : CommentNodes)
			{
				if (Comment->GetNodesUnderComment().Contains(KnotNode))
				{
					FBAUtils::RemoveNodeFromComment(Comment, KnotNode);
				}
			}

			if (GetDefault<UBASettings>()->bUseKnotNodePool)
			{
				KnotNodePool.Add(KnotNode);
			}
			else
			{
				FBAUtils::DeleteNode(KnotNode);
			}
		}
	}
}

UK2Node_Knot* FKnotTrackCreator::CreateKnotNode(FKnotNodeCreation* Creation, const FVector2D& Position, UEdGraphPin* ParentPin)
{
	if (!Creation)
	{
		return nullptr;
	}

	UK2Node_Knot* OptionalNodeToReuse = nullptr;
	if (GetDefault<UBASettings>()->bUseKnotNodePool && KnotNodePool.Num() > 0)
	{
		OptionalNodeToReuse = KnotNodePool.Pop();
	}
	else
	{
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Failed to find?"));
	}

	auto Graph = GraphHandler->GetFocusedEdGraph();
	UK2Node_Knot* CreatedNode = Creation->CreateKnotNode(Position, ParentPin, OptionalNodeToReuse, Graph);

	UEdGraphPin* MainPinToConnectTo = Creation->PinToConnectToHandle.GetPin();

	KnotNodeOwners.Add(CreatedNode, MainPinToConnectTo->GetOwningNode());
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Created node %d for %s"), CreatedNode, *FBAUtils::GetNodeName(ParentPin->GetOwningNode()));

	return CreatedNode; //Creation->CreateKnotNode(Position, ParentPin, OptionalNodeToReuse, GraphHandler->GetFocusedEdGraph());
}

bool FKnotTrackCreator::TryAlignTrackToEndPins(TSharedPtr<FKnotNodeTrack> Track, const TArray<UEdGraphNode*>& AllNodes)
{
	const float ParentPinY = GraphHandler->GetPinY(Track->ParentPin);
	const float LastPinY = GraphHandler->GetPinY(Track->GetLastPin());
	bool bPreferParentPin = ParentPinY > LastPinY;

	if (FBAUtils::IsExecPin(Track->ParentPin))
	{
		bPreferParentPin = true;
	}

	for (int i = 0; i < 2; ++i)
	{
		//FString PreferPinStr = bPreferParentPin ? "true" : "false";
		//UE_LOG(LogBlueprintAssist, Warning, TEXT("AlignTrack ParentY %f (%s) | LastPinY %f (%s) | PreferParent %s"), ParentPinY, *FBAUtils::GetPinName(Track->ParentPin), LastPinY,
		//       *FBAUtils::GetPinName(Track->GetLastPin()), *PreferPinStr);

		if (i == 1)
		{
			bPreferParentPin = !bPreferParentPin;
		}

		UEdGraphPin* SourcePin = bPreferParentPin ? Track->ParentPin : Track->GetLastPin();
		UEdGraphPin* OtherPin = bPreferParentPin ? Track->GetLastPin() : Track->ParentPin;

		const FVector2D SourcePinPos = FBAUtils::GetPinPos(GraphHandler, SourcePin);
		const FVector2D OtherPinPos = FBAUtils::GetPinPos(GraphHandler, OtherPin);

		const FVector2D Padding = FBAUtils::IsParameterPin(OtherPin)
			? PinPadding
			: NodePadding;

		const FVector2D Point
			= SourcePin->Direction == EGPD_Output
			? FVector2D(OtherPinPos.X - Padding.X, SourcePinPos.Y)
			: FVector2D(OtherPinPos.X + Padding.X, SourcePinPos.Y);

		// UE_LOG(LogBlueprintAssist, Error, TEXT("Checking Point %s | %s"), *Point.ToString(), *FBAUtils::GetNodeName(SourcePin->GetOwningNode()));

		bool bAnyCollision = false;

		for (UEdGraphNode* NodeToCollisionCheck : AllNodes)
		{
			FSlateRect CollisionBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToCollisionCheck).ExtendBy(FMargin(0, TrackSpacing - 1));

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Collision check against %s | %s | %s"), *FBAUtils::GetNodeName(NodeToCollisionCheck), *CollisionBounds.ToString(), *Point.ToString());

			if (NodeToCollisionCheck == SourcePin->GetOwningNode() || NodeToCollisionCheck == OtherPin->GetOwningNode())
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tSkipping node"));
				continue;
			}

			if (FBAUtils::LineRectIntersection(CollisionBounds, SourcePinPos, Point))
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tFound collision"));
				bAnyCollision = true;
				break;
			}
		}

		for (TSharedPtr<FKnotNodeTrack> OtherTrack : KnotTracks)
		{
			if (OtherTrack == Track)
			{
				continue;
			}

			// Possibly revert back to rect collision check
			if (FBAUtils::LineRectIntersection(OtherTrack->GetTrackBounds().ExtendBy(FMargin(0, TrackSpacing * 0.25f)), SourcePinPos, Point))
			{
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Track %s colliding with %s"), *Track->ToString(), *OtherTrack->ToString());
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tStart %s End %s"), *SourcePinPos.ToString(), *Point.ToString());
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tRect %s"), *MyTrackBounds.ToString());
				bAnyCollision = true;
				break;
			}
		}

		if (!bAnyCollision)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("sucessfully found easy solution!"));
			Track->PinAlignedX = Point.X;
			Track->UpdateTrackHeight(SourcePinPos.Y);
			Track->PinToAlignTo.SetPin(SourcePin);
			return true;
		}
	}

	return false;
}

bool FKnotTrackCreator::DoesPinNeedTrack(UEdGraphPin* Pin, const TArray<UEdGraphPin*>& LinkedTo)
{
	if (LinkedTo.Num() == 0)
	{
		return false;
	}

	// if the pin is linked to multiple linked nodes, we need a knot track
	if (LinkedTo.Num() > 1)
	{
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Multiple linked to?"));
		return true;
	}

	// otherwise the pin is linked to exactly 1 node, run a collision check
	UEdGraphPin* OtherPin = LinkedTo[0];

	// need pin if there are any collisions
	return AnyCollisionBetweenPins(Pin, OtherPin);
}

bool FKnotTrackCreator::AnyCollisionBetweenPins(UEdGraphPin* Pin, UEdGraphPin* OtherPin)
{
	TSet<UEdGraphNode*> FormattedNodes = Formatter->GetFormattedNodes();

	const FVector2D PinPos = FBAUtils::GetPinPos(GraphHandler, Pin);
	const FVector2D OtherPinPos = FBAUtils::GetPinPos(GraphHandler, OtherPin);

	return NodeCollisionBetweenLocation(PinPos, OtherPinPos, { Pin->GetOwningNode(), OtherPin->GetOwningNode() });
}

bool FKnotTrackCreator::NodeCollisionBetweenLocation(FVector2D Start, FVector2D End, TSet<UEdGraphNode*> IgnoredNodes)
{
	TSet<UEdGraphNode*> FormattedNodes = Formatter->GetFormattedNodes();

	for (UEdGraphNode* NodeToCollisionCheck : FormattedNodes)
	{
		if (IgnoredNodes.Contains(NodeToCollisionCheck))
		{
			continue;
		}

		FSlateRect NodeBounds = FBAUtils::GetCachedNodeBounds(GraphHandler, NodeToCollisionCheck).ExtendBy(FMargin(0, TrackSpacing - 1));
		if (FBAUtils::LineRectIntersection(NodeBounds, Start, End))
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tNode collision!"));
			return true;
		}
	}

	return false;
}

void FKnotTrackCreator::Reset()
{
	KnotNodesSet.Reset();
	KnotTracks.Reset();
	KnotNodeOwners.Reset();
}

void FKnotTrackCreator::MakeKnotTrack()
{
	const TSet<UEdGraphNode*> FormattedNodes = Formatter->GetFormattedNodes();

	const auto& NotFormatted = [FormattedNodes](UEdGraphPin* Pin)
	{
		return !FormattedNodes.Contains(Pin->GetOwningNode());
	};

	// iterate across the pins of all nodes and determine if they require a knot track
	for (UEdGraphNode* MyNode : FormattedNodes)
	{
		// make tracks for input exec pins
		TArray<TSharedPtr<FKnotNodeTrack>> PreviousTracks;
		for (UEdGraphPin* MyPin : FBAUtils::GetExecPins(MyNode, EGPD_Input))
		{
			TArray<UEdGraphPin*> LinkedTo = MyPin->LinkedTo;
			LinkedTo.RemoveAll(NotFormatted);
			if (LinkedTo.Num() == 0)
			{
				continue;
			}

			if (GetDefault<UBASettings>()->ExecutionWiringStyle == EBAWiringStyle::AlwaysMerge)
			{
				MakeKnotTracksForLinkedExecPins(MyPin, LinkedTo, PreviousTracks);
			}
			else
			{
				for (UEdGraphPin* Pin : LinkedTo)
				{
					MakeKnotTracksForLinkedExecPins(MyPin, { Pin }, PreviousTracks);
				}
			}
		}
	}

	for (UEdGraphNode* MyNode : FormattedNodes)
	{
		// make tracks for output parameter pins
		TArray<TSharedPtr<FKnotNodeTrack>> PreviousTracks;
		for (UEdGraphPin* MyPin : FBAUtils::GetParameterPins(MyNode, EGPD_Output))
		{
			TArray<UEdGraphPin*> LinkedTo = MyPin->LinkedTo;
			LinkedTo.RemoveAll(NotFormatted);
			if (LinkedTo.Num() == 0)
			{
				continue;
			}

			if (GetDefault<UBASettings>()->ParameterWiringStyle == EBAWiringStyle::AlwaysMerge)
			{
				MakeKnotTracksForParameterPins(MyPin, LinkedTo, PreviousTracks);
			}
			else
			{
				for (UEdGraphPin* Pin : LinkedTo)
				{
					MakeKnotTracksForParameterPins(MyPin, { Pin }, PreviousTracks);
				}
			}
		}
	}
}

TSharedPtr<FKnotNodeTrack> FKnotTrackCreator::MakeKnotTracksForLinkedExecPins(UEdGraphPin* ParentPin, TArray<UEdGraphPin*> LinkedPins, TArray<TSharedPtr<FKnotNodeTrack>>& PreviousTracks)
{
	FVector2D ParentPinPos = FBAUtils::GetPinPos(GraphHandler, ParentPin);
	UEdGraphNode* ParentNode = ParentPin->GetOwningNode();

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Processing knot track for parent pin %s"), *FBAUtils::GetPinName(ParentPin));
	// for (auto Pin : LinkedPins)
	// {
	// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("\t%s"), *FBAUtils::GetPinName(Pin));
	// }

	// check for looping pins, these are pins where 
	// the x position of the pin is less than the x value of the parent pin
	TArray<UEdGraphPin*> LoopingPins;
	for (UEdGraphPin* LinkedPin : LinkedPins)
	{
		const FVector2D LinkedPinPos = FBAUtils::GetPinPos(GraphHandler, LinkedPin);
		if (LinkedPinPos.X > ParentPinPos.X)
		{
			LoopingPins.Add(LinkedPin);
		}
	}

	// create looping tracks
	for (UEdGraphPin* OtherPin : LoopingPins)
	{
		const float OtherNodeTop = FBAUtils::GetNodeBounds(OtherPin->GetOwningNode()).Top;
		const float MyNodeTop = FBAUtils::GetNodeBounds(ParentNode).Top;
		const float AboveNodeWithPadding = FMath::Min(OtherNodeTop, MyNodeTop) - TrackSpacing * 2;

		TArray<UEdGraphPin*> TrackPins = { OtherPin };
		TSharedPtr<FKnotNodeTrack> KnotTrack = MakeShared<FKnotNodeTrack>(Formatter, GraphHandler, ParentPin, TrackPins, AboveNodeWithPadding, true);
		KnotTracks.Add(KnotTrack);

		const FVector2D OtherPinPos = FBAUtils::GetPinPos(GraphHandler, OtherPin);

		const FVector2D FirstKnotPos(ParentPinPos.X + 20, KnotTrack->GetTrackHeight());
		TSharedPtr<FKnotNodeCreation> FirstLoopingKnot = MakeShared<FKnotNodeCreation>(KnotTrack, FirstKnotPos, nullptr, OtherPin);
		KnotTrack->KnotCreations.Add(FirstLoopingKnot);

		const FVector2D SecondKnotPos(OtherPinPos.X - 20, KnotTrack->GetTrackHeight());
		TSharedPtr<FKnotNodeCreation> SecondLoopingKnot = MakeShared<FKnotNodeCreation>(KnotTrack, SecondKnotPos, FirstLoopingKnot, OtherPin);
		KnotTrack->KnotCreations.Add(SecondLoopingKnot);
	}

	LinkedPins.RemoveAll([&LoopingPins](UEdGraphPin* Pin) { return LoopingPins.Contains(Pin); });

	// remove pins which are left or too close to my pin
	const float Threshold = ParentPinPos.X - NodePadding.X * 1.5f;
	TSharedPtr<FBAGraphHandler> GraphHandlerRef = GraphHandler;
	const auto& IsTooCloseToParent = [GraphHandlerRef, Threshold](UEdGraphPin* Pin)
	{
		const FVector2D PinPos = FBAUtils::GetPinPos(GraphHandlerRef, Pin);
		return PinPos.X > Threshold;
	};
	LinkedPins.RemoveAll(IsTooCloseToParent);

	// remove any linked pins which has the same height and no collision
	UEdGraphPin* PinRemoved = nullptr;
	for (UEdGraphPin* LinkedPin : LinkedPins)
	{
		//UE_LOG(LogBlueprintAssist, Warning, TEXT("Checking %s"), *FBAUtils::GetPinName(LinkedPin));
		const FVector2D LinkedPinPos = FBAUtils::GetPinPos(GraphHandler, LinkedPin);
		const bool bSameHeight = FMath::Abs(LinkedPinPos.Y - ParentPinPos.Y) < 5.f;
		if (bSameHeight && !AnyCollisionBetweenPins(ParentPin, LinkedPin))
		{
			PinRemoved = LinkedPin;
			break;
		}
	}
	LinkedPins.Remove(PinRemoved);

	if (LinkedPins.Num() == 0)
	{
		return nullptr;
	}

	// sort pins by node's highest x position first then highest y position
	const auto RightTop = [](const UEdGraphPin& PinA, const UEdGraphPin& PinB)
	{
		if (PinA.GetOwningNode()->NodePosX == PinB.GetOwningNode()->NodePosX)
		{
			return PinA.GetOwningNode()->NodePosY > PinB.GetOwningNode()->NodePosY;
		}

		return PinA.GetOwningNode()->NodePosX > PinB.GetOwningNode()->NodePosX;
	};

	LinkedPins.Sort(RightTop);

	const FVector2D LastPinPos = FBAUtils::GetPinPos(GraphHandler, LinkedPins.Last());

	const float Dist = FMath::Abs(ParentPinPos.X - LastPinPos.X);

	// skip the pin distance check if we are expanding by height
	const bool bPinReallyFar = Dist > GetDefault<UBASettings>()->KnotNodeDistanceThreshold && !GetDefault<UBASettings>()->bExpandNodesByHeight;

	const bool bPinNeedsTrack = DoesPinNeedTrack(ParentPin, LinkedPins);

	const bool bPreviousHasTrack = PreviousTracks.Num() > 0;

	const FVector2D ToLast = LastPinPos - ParentPinPos;
	const bool bTooSteep = FMath::Abs(ToLast.Y) / FMath::Abs(ToLast.X) >= 2.75f;
	if (bTooSteep)
	{
		return nullptr;
	}

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Need reroute: %d %d %d"), bPinReallyFar, bPreviousHasTrack, bPinNeedsTrack);

	const bool bNeedsReroute = bPinReallyFar || bPreviousHasTrack || bPinNeedsTrack;
	if (!bNeedsReroute)
	{
		return nullptr;
	}

	TSharedPtr<FKnotNodeTrack> KnotTrack = MakeShared<FKnotNodeTrack>(Formatter, GraphHandler, ParentPin, LinkedPins, ParentPinPos.Y, false);
	KnotTracks.Add(KnotTrack);

	TryAlignTrackToEndPins(KnotTrack, Formatter->GetFormattedNodes().Array());

	// if the track is not at the same height as the pin, then we need an
	// initial knot right of the inital pin, at the track height
	const FVector2D MyKnotPos = FVector2D(ParentPinPos.X - NodePadding.X, KnotTrack->GetTrackHeight());
	TSharedPtr<FKnotNodeCreation> PreviousKnot = MakeShared<FKnotNodeCreation>(KnotTrack, MyKnotPos, nullptr, KnotTrack->ParentPin);
	KnotTrack->KnotCreations.Add(PreviousKnot);

	// create a knot node for each of the pins remaining in linked to
	for (UEdGraphPin* OtherPin : KnotTrack->LinkedTo)
	{
		ParentPin->BreakLinkTo(OtherPin);

		const FVector2D OtherPinPos = FBAUtils::GetPinPos(GraphHandler, OtherPin);
		const float KnotX = FMath::Min(OtherPinPos.X + NodePadding.X, ParentPinPos.X - NodePadding.X);
		const FVector2D KnotPos(KnotX, KnotTrack->GetTrackHeight());

		// if the x position is very close to the previous knot's x position, 
		// we should not need to create a new knot instead we merge the locations
		if (PreviousKnot.IsValid() && FMath::Abs(KnotX - PreviousKnot->KnotPos.X) < 50)
		{
			PreviousKnot->KnotPos.X = KnotX;
			PreviousKnot->PinHandlesToConnectTo.Add(OtherPin);
			continue;
		}

		PreviousKnot = MakeShared<FKnotNodeCreation>(KnotTrack, KnotPos, PreviousKnot, OtherPin);
		KnotTrack->KnotCreations.Add(PreviousKnot);
	}

	PreviousTracks.Add(KnotTrack);

	return KnotTrack;
}

TSharedPtr<FKnotNodeTrack> FKnotTrackCreator::MakeKnotTracksForParameterPins(UEdGraphPin* ParentPin, TArray<UEdGraphPin*> LinkedPins, TArray<TSharedPtr<FKnotNodeTrack>>& PreviousTracks)
{
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Make knot tracks for parameter pin %s"), *FBAUtils::GetPinName(ParentPin));

	FVector2D ParentPinPos = FBAUtils::GetPinPos(GraphHandler, ParentPin);

	// remove pins which are left or too close to my pin
	const float Threshold = ParentPinPos.X + NodePadding.X * 2.0f;
	TSharedPtr<FBAGraphHandler> GraphHandlerRef = GraphHandler;

	const auto& IsTooCloseToParent = [GraphHandlerRef, Threshold](UEdGraphPin* Pin)
	{
		const FVector2D PinPos = FBAUtils::GetPinPos(GraphHandlerRef, Pin);
		return PinPos.X < Threshold;
	};

	LinkedPins.RemoveAll(IsTooCloseToParent);

	if (LinkedPins.Num() == 0)
	{
		return nullptr;
	}

	const auto LeftTop = [](const UEdGraphPin& PinA, const UEdGraphPin& PinB)
	{
		if (PinA.GetOwningNode()->NodePosX == PinB.GetOwningNode()->NodePosX)
		{
			return PinA.GetOwningNode()->NodePosY > PinB.GetOwningNode()->NodePosY;
		}

		return PinA.GetOwningNode()->NodePosX < PinB.GetOwningNode()->NodePosX;
	};
	LinkedPins.Sort(LeftTop);

	const FVector2D LastPinPos = FBAUtils::GetPinPos(GraphHandler, LinkedPins.Last());

	const float Dist = FMath::Abs(ParentPinPos.X - LastPinPos.X);

	const bool bLastPinFarAway = Dist > GetDefault<UBASettings>()->KnotNodeDistanceThreshold && !GetDefault<UBASettings>()->bExpandNodesByHeight;

	const bool bPinNeedsTrack = DoesPinNeedTrack(ParentPin, LinkedPins);

	const bool bPreviousHasTrack = PreviousTracks.Num() > 0;

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Needs track: %d %d %d"), bLastPinFarAway, bPreviousHasTrack, bPinNeedsTrack);

	const FVector2D ToLast = LastPinPos - ParentPinPos;
	const bool bTooSteep = FMath::Abs(ToLast.Y) / FMath::Abs(ToLast.X) >= 2.75f;
	if (bTooSteep)
	{
		return nullptr;
	}

	const bool bNeedsReroute = bPinNeedsTrack || bPreviousHasTrack || bLastPinFarAway;
	if (!bNeedsReroute)
	{
		return nullptr;
	}

	// init the knot track
	TSharedPtr<FKnotNodeTrack> KnotTrack = MakeShared<FKnotNodeTrack>(Formatter, GraphHandler, ParentPin, LinkedPins, ParentPinPos.Y, false);
	KnotTracks.Add(KnotTrack);

	// check if the track height can simply be set to one of it's pin's height
	if (TryAlignTrackToEndPins(KnotTrack, Formatter->GetFormattedNodes().Array()))
	{
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Found a pin to align to for %s"), *FBAUtils::GetPinName(KnotTrack->ParentPin));
	}
	else
	{
		// UE_LOG(LogBlueprintAssist, Warning, TEXT("Failed to find pin to align to"));
	}

	// Add a knot creation which links to the parent pin
	const FVector2D InitialKnotPos = FVector2D(ParentPinPos.X + PinPadding.X, KnotTrack->GetTrackHeight());
	TSharedPtr<FKnotNodeCreation> PreviousKnot = MakeShared<FKnotNodeCreation>(KnotTrack, InitialKnotPos, nullptr, KnotTrack->ParentPin);
	ParentPin->BreakLinkTo(KnotTrack->GetLastPin());
	KnotTrack->KnotCreations.Add(PreviousKnot);

	for (UEdGraphPin* OtherPin : KnotTrack->LinkedTo)
	{
		// break link to parent pin
		ParentPin->BreakLinkTo(OtherPin);

		const FVector2D OtherPinPos = FBAUtils::GetPinPos(GraphHandler, OtherPin);
		const float KnotX = FMath::Max(OtherPinPos.X - PinPadding.X, ParentPinPos.X + PinPadding.X);

		const FVector2D KnotPos = FVector2D(KnotX, KnotTrack->GetTrackHeight());

		// if the x position is very close to the previous knot's x position, 
		// we should not need to create a new knot instead we merge the locations
		if (PreviousKnot.IsValid() && FMath::Abs(KnotX - PreviousKnot->KnotPos.X) < 50)
		{
			PreviousKnot->KnotPos.X = KnotX;
			PreviousKnot->PinHandlesToConnectTo.Add(OtherPin);
			continue;
		}

		// Add a knot creation for each linked pin
		PreviousKnot = MakeShared<FKnotNodeCreation>(KnotTrack, KnotPos, PreviousKnot, OtherPin);
		KnotTrack->KnotCreations.Add(PreviousKnot);
	}

	PreviousTracks.Add(KnotTrack);

	return KnotTrack;
}

void FKnotTrackCreator::MergeNearbyKnotTracks()
{
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("Merging knot track"));

	TArray<TSharedPtr<FKnotNodeTrack>> PendingTracks = KnotTracks;

	if (GetDefault<UBASettings>()->ExecutionWiringStyle != EBAWiringStyle::MergeWhenNear)
	{
		PendingTracks.RemoveAll([](TSharedPtr<FKnotNodeTrack> Track)
		{
			return FBAUtils::IsExecPin(Track->ParentPin);
		});
	}

	if (GetDefault<UBASettings>()->ParameterWiringStyle != EBAWiringStyle::MergeWhenNear)
	{
		PendingTracks.RemoveAll([](TSharedPtr<FKnotNodeTrack> Track)
		{
			return FBAUtils::IsParameterPin(Track->ParentPin);
		});
	}

	// TODO: Handle merging of looping tracks
	PendingTracks.RemoveAll([](TSharedPtr<FKnotNodeTrack> Track)
	{
		return Track->bIsLoopingTrack;
	});

	while (PendingTracks.Num() > 0)
	{
		auto CurrentTrack = PendingTracks.Pop();
		auto Tracks = PendingTracks;

		for (TSharedPtr<FKnotNodeTrack> Track : Tracks)
		{
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Merging track %s"), *FBAUtils::GetPinName(Track->ParentPin));

			// merge if they have the same parent and same height
			if (Track->ParentPin == CurrentTrack->ParentPin &&
				Track->GetTrackHeight() == CurrentTrack->GetTrackHeight())
			{
				for (TSharedPtr<FKnotNodeCreation> Creation : Track->KnotCreations)
				{
					bool bShouldAddCreation = true;
					for (TSharedPtr<FKnotNodeCreation> CurrentCreation : CurrentTrack->KnotCreations)
					{
						if (FMath::Abs(CurrentCreation->KnotPos.X - Creation->KnotPos.X) < 50)
						{
							bShouldAddCreation = false;
							CurrentCreation->PinHandlesToConnectTo.Append(Creation->PinHandlesToConnectTo);
						}
					}

					if (bShouldAddCreation)
					{
						CurrentTrack->KnotCreations.Add(Creation);
						CurrentTrack->PinToAlignTo.SetPin(nullptr);

						// UE_LOG(LogBlueprintAssist, Warning, TEXT("Cancelled pin to align to for track %s"), *FBAUtils::GetPinName(CurrentTrack->ParentPin));
					}
				}

				KnotTracks.Remove(Track);
				PendingTracks.Remove(Track);
			}
		}
	}
}

void FKnotTrackCreator::AddKnotNodesToComments()
{
	FCommentHandler* CommentHandler = Formatter->GetCommentHandler();
	if (!CommentHandler)
	{
		return;
	}

	if (CommentHandler->CommentNodesContains.Num() == 0)
	{
		return;
	}

	for (TSharedPtr<FKnotNodeTrack> Track : KnotTracks)
	{
		TArray<UEdGraphNode*> TrackNodes = Track->GetNodes(GraphHandler->GetFocusedEdGraph()).Array();

		int NumKnots = 0;
		UK2Node_Knot* SingleKnot = nullptr;
		for (auto Creation : Track->KnotCreations)
		{
			if (Creation->CreatedKnot != nullptr)
			{
				NumKnots += 1;
				SingleKnot = Creation->CreatedKnot;
			}
		}

		for (const auto& Elem : CommentHandler->CommentNodesContains)
		{
			UEdGraphNode_Comment* Comment = Elem.Key;
			TArray<UEdGraphNode*> Containing = Elem.Value.Array();
			FSlateRect CommentBounds = CommentHandler->GetCommentBounds(Comment, nullptr); // .ExtendBy(30);

			bool bContainsSingleKnot = NumKnots == 1 && CommentBounds.ContainsPoint(FVector2D(SingleKnot->NodePosX, SingleKnot->NodePosY));
			const bool bContainsAllNodes = FBAUtils::DoesArrayContainsAllItems(Containing, TrackNodes);

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\tAllKnots %d | AllNodes %d"), bContainsAllKnots, bContainsAllNodes);

			if (bContainsAllNodes)
			{
				if (!(NumKnots == 1 && bContainsSingleKnot))
				{
					auto NodesUnderComment = Comment->GetNodesUnderComment();
					for (auto Creation : Track->KnotCreations)
					{
						if (!NodesUnderComment.Contains(Creation->CreatedKnot))
						{
							Comment->AddNodeUnderComment(Creation->CreatedKnot);
						}
					}
				}
			}
		}
	}
}

void FKnotTrackCreator::PrintKnotTracks()
{
	UE_LOG(LogBlueprintAssist, Warning, TEXT("### All Knot Tracks"));
	for (TSharedPtr<FKnotNodeTrack> Track : KnotTracks)
	{
		FString Aligned = Track->GetPinToAlignTo() != nullptr ? FString("True") : FString("False");
		FString Looping = Track->bIsLoopingTrack ? FString("True") : FString("False");
		UE_LOG(LogBlueprintAssist, Warning, TEXT("\tKnot Tracks (%d) %s | %s | %s | %s | Aligned %s (%s) | Looping %s"),
			Track->KnotCreations.Num(),
			*FBAUtils::GetPinName(Track->ParentPin),
			*FBAUtils::GetNodeName(Track->ParentPin->GetOwningNodeUnchecked()),
			*FBAUtils::GetPinName(Track->GetLastPin()),
			*FBAUtils::GetNodeName(Track->GetLastPin()->GetOwningNodeUnchecked()),
			*Aligned, *FBAUtils::GetPinName(Track->GetPinToAlignTo()),
			*Looping);

		for (TSharedPtr<FKnotNodeCreation> Elem : Track->KnotCreations)
		{
			if (auto MyPin = Elem->PinToConnectToHandle.GetPin())
			{
				UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t%s %s"), *FBAUtils::GetPinName(MyPin), *Elem->KnotPos.ToString());
			}

			for (auto PinHandle : Elem->PinHandlesToConnectTo)
			{
				if (auto MyPin = PinHandle.GetPin())
				{
					UE_LOG(LogBlueprintAssist, Warning, TEXT("\t\t\t%s"), *FBAUtils::GetPinName(MyPin));
				}
			}
		}
	}
}
