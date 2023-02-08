// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraphNode.h"
#include "Framework/Application/IInputProcessor.h"

class SDockTab;
class FBAGraphHandler;
class FBATabHandler;
class FUICommandList;

class BLUEPRINTASSIST_API FBAInputProcessor
	: public TSharedFromThis<FBAInputProcessor>
	, public IInputProcessor
{
public:
	virtual ~FBAInputProcessor() override;

	static void Create();

	static FBAInputProcessor& Get();

	void Cleanup();

	//~ Begin IInputProcessor Interface

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	void OnMouseDrag(FSlateApplication& SlateApp, const FVector2D& MousePos, const FVector2D& Delta);

	bool bIsDragging = false;
	FVector2D LastMousePos;
	FVector2D NodeGrabOffset; 

	//~ End IInputProcessor Interface

	bool CanExecuteCommand(TSharedRef<const FUICommandInfo> Command) const;
	bool TryExecuteCommand(TSharedRef<const FUICommandInfo> Command);

	bool IsGameRunningOrCompiling() const;

private:
	TSharedPtr<FBAGraphHandler> GraphHandler;

	TSharedPtr<FUICommandList> GlobalCommands;
	TSharedPtr<FUICommandList> TabCommands;
	TSharedPtr<FUICommandList> GraphCommands;
	TSharedPtr<FUICommandList> GraphReadOnlyCommands;
	TSharedPtr<FUICommandList> SingleNodeCommands;
	TSharedPtr<FUICommandList> MultipleNodeCommands;
	TSharedPtr<FUICommandList> MultipleNodeCommandsIncludingComments;
	TSharedPtr<FUICommandList> PinCommands;
	TSharedPtr<FUICommandList> ActionMenuCommands;
	TSharedPtr<FUICommandList> PinEditCommands;
	TSharedPtr<FUICommandList> BlueprintEditorCommands;
	TArray<TSharedPtr<FUICommandList>> CommandLists;

	FBAInputProcessor();

	void CreateGraphEditorCommands();

	// command list
	void OnOpenContextMenu();

	void FormatNodes() const;

	void FormatNodesSelectively();

	void FormatNodesWithHelixing() const;

	void FormatNodesWithLHS() const;

	void OnSmartWireSelectedNode() const;

	void OnFormatAllEvents() const;

	void SelectNodeInDirection(
		const TArray<UEdGraphNode*>& Nodes,
		int X,
		int Y,
		float DistLimit) const;

	void SelectAnyNodeInDirection(int X, int Y) const;

	void SelectCustomEventNodeInDirection(int X, int Y) const;

	void SelectPinInDirection(int X, int Y) const;

	bool CanSelectPinInDirection();

	void ShiftCameraInDirection(int X, int Y) const;

	void SwapNodeInDirection(EEdGraphPinDirection Direction) const;

	void OpenContextMenu(const FVector2D& MenuLocation, const FVector2D& NodeSpawnPosition) const;

	void SmartWireNode(UEdGraphNode* Node) const;

	void LinkPinToNearest(UEdGraphPin* Pin, bool bOverrideLink, bool bFilterByDirection, float DistLimit = 0.f) const;

	void LinkToHoveredPin();

	void LinkNodesBetweenWires();

	void CreateContextMenuFromPin(UEdGraphPin* Pin, const FVector2D& MenuLocation, const FVector2D& NodeLocation) const;

	void OpenPinLinkMenu();

	void OpenGoToSymbolMenu();

	void OpenWindowMenu();

	void OpenFocusSearchBoxMenu();

	void OpenVariableSelectorMenu();

	void OpenCreateSymbolMenu();

	void OpenEditDetailsMenu();

	void OpenTabSwitcherMenu();

	void DuplicateNodeForEachLink() const;

	void RefreshNodeSizes() const;

	void OnEditSelectedPinValue() const;

	void DeleteAndLink();

	void ZoomToNodeTree() const;

	void OnGetContextMenuActions(bool bUsePin = true) const;

	void OnToggleActionMenuContextSensitive() const;

	void DisconnectPinOrWire();

	void DisconnectExecutionOfSelectedNode();

	void DisconnectExecutionOfNodes(TArray<UEdGraphNode*> Nodes);

	void DisconnectAllNodeLinks();

	void ReplaceNodeWith();

	void OnReplaceNodeMenuClosed(const TSharedRef<class SWindow>& Window);

	void DebugPrintGraphUnderCursor() const;

	void ToggleNodes();

	void SplitPin();

	void RecombinePin();

	void CreateRerouteNode();

	void OpenBlueprintAssistHotkeyMenu();

	void RenameSelectedNode();

	void ToggleFullscreen();

	void SwitchWorkflowMode();

	void OpenAssetCreationMenu();

	void FocusGraphPanel();

	bool CanExecuteActions() const;

	bool HasEditablePin() const;

	bool HasOpenActionMenu() const;

	bool HasOpenBlueprintEditor() const;

	bool HasOpenTab() const;

	bool HasGraph() const;

	bool HasGraphNonReadOnly() const;

	bool HasSelectedPin() const;

	bool HasSingleNodeSelected() const;

	bool HasMultipleNodesSelected() const;

	bool HasMultipleNodesSelectedInclComments() const;

	bool CanRenameSelectedNode() const;

	bool CanToggleNodes() const;

	bool HasWorkflowModes() const;

	bool CanOpenEditDetailsMenu() const;

#if ENGINE_MINOR_VERSION >= 26 || ENGINE_MAJOR_VERSION >= 5
	virtual const TCHAR* GetDebugName() const override { return TEXT("BlueprintAssistInputProcessor"); }
#endif
};
