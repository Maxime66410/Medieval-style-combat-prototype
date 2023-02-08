// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintAssistTypes.h"

#include "SGraphPin.h"

class UK2Node_Knot;
struct FFormatterInterface;
class FBAGraphHandler;


struct BLUEPRINTASSIST_API FKnotNodeCreation
	: public TSharedFromThis<FKnotNodeCreation>
{
	TSharedPtr<struct FKnotNodeTrack> OwningKnotTrack;

	bool bMakeLinkForPrevious = false;

	FVector2D KnotPos;
	TSharedPtr<FKnotNodeCreation> KnotToConnectTo = nullptr;
	UK2Node_Knot* CreatedKnot = nullptr;

	FBAGraphPinHandle PinToConnectToHandle;
	TSet<FBAGraphPinHandle> PinHandlesToConnectTo;

	FKnotNodeCreation() : PinToConnectToHandle(nullptr) { }

	FKnotNodeCreation(
		TSharedPtr<FKnotNodeTrack> InOwningKnotTrack,
		const FVector2D InKnotPos,
		TSharedPtr<FKnotNodeCreation> InKnotToConnectTo,
		UEdGraphPin* InPinToConnectTo)
		: OwningKnotTrack(InOwningKnotTrack)
		, KnotPos(InKnotPos)
		, KnotToConnectTo(InKnotToConnectTo)
		, PinToConnectToHandle(InPinToConnectTo)
	{
		if (InPinToConnectTo != nullptr)
		{
			PinHandlesToConnectTo.Add(PinToConnectToHandle);
		}
	}

	UEdGraphPin* GetPinToConnectTo();

	UK2Node_Knot* CreateKnotNode(FVector2D InKnotPos, UEdGraphPin* PreviousPin, UK2Node_Knot* KnotNodeToReuse, UEdGraph* Graph);

	bool HasHeightDifference() const;
};

struct BLUEPRINTASSIST_API FKnotNodeTrack
	: public TSharedFromThis<FKnotNodeTrack>
{
	TSharedPtr<FBAGraphHandler> GraphHandler;
	UEdGraphPin* ParentPin;
	TArray<UEdGraphPin*> LinkedTo;

private:
	float TrackHeight;

public:
	float GetTrackHeight();

	FBANodePinHandle PinToAlignTo;
	float PinAlignedX;

	FVector2D ParentPinPos;

	TArray<TSharedPtr<FKnotNodeCreation>> KnotCreations;
	bool bIsLoopingTrack = false;

	FKnotNodeTrack(
		TSharedPtr<FFormatterInterface> Formatter,
		TSharedPtr<FBAGraphHandler> InGraphHandler,
		UEdGraphPin* InParentPin,
		TArray<UEdGraphPin*> InLinkedTo,
		float InTrackY,
		bool bInIsLoopingTrack);

	UEdGraphPin* GetParentPin() const;

	UEdGraphPin* GetLastPin() const;

	UEdGraphPin* GetPinToAlignTo();

	FSlateRect GetTrackBounds();

	void SetTrackHeight(TSharedPtr<FFormatterInterface> Formatter);

	bool IsFloatingTrack() const;

	void UpdateTrackHeight(float NewTrackY);

	TSet<UEdGraphNode*> GetNodes(UEdGraph* Graph);

	bool DoesTrackOverlapNode(UEdGraphNode* Node);

	bool HasPinToAlignTo();

	bool TryAlignTrack(TSharedPtr<FFormatterInterface> Formatter, float TrackStart, float TrackEnd, float TestHeight);

	FString ToString();
};

struct BLUEPRINTASSIST_API FGroupedTracks
{
	UEdGraphNode* ParentNode;
	TArray<TSharedPtr<FKnotNodeTrack>> Tracks;

	float Width = MIN_flt;

	bool bLooping = false;

	void Init()
	{
		for (auto Track : Tracks)
		{
			Width = FMath::Max(Width, Track->GetTrackBounds().GetSize().X);
			bLooping |= Track->bIsLoopingTrack;
		}
	}
};