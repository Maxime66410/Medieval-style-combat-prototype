// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistInputProcessor.h"

#include "BlueprintAssistCommands.h"
#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistModule.h"
#include "BlueprintAssistSettings.h"
#include "BlueprintAssistTabHandler.h"
#include "BlueprintAssistToolbar.h"
#include "BlueprintAssistUtils.h"
#include "BlueprintEditor.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallFunctionOnMember.h"
#include "K2Node_Literal.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "ScopedTransaction.h"
#include "SGraphActionMenu.h"
#include "SGraphPanel.h"
#include "SNodePanel.h"
#include "BlueprintAssist/BlueprintAssistObjects/BARootObject.h"
#include "BlueprintAssist/BlueprintAssistWidgets/AddSymbolMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/BlueprintAssistCreateAssetMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/BlueprintAssistHotkeyMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/BlueprintAssistTabSwitcher.h"
#include "BlueprintAssist/BlueprintAssistWidgets/BlueprintAssistWorkflowModeMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/EditDetailsMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/FocusSearchBoxMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/GoToSymbolMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/LinkPinMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/OpenWindowMenu.h"
#include "BlueprintAssist/BlueprintAssistWidgets/VariableSelectorMenu.h"
#include "BlueprintAssist/GraphFormatters/GraphFormatterTypes.h"
#include "EdGraph/EdGraph.h"
#include "Editor/BlueprintGraph/Classes/K2Node_Knot.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Templates/SharedPointer.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"

//~ Begin COPY OF BlueprintEditor.cpp FUpdatePastedNodes struct
struct FUpdatePastedNodes_Copy
{
	TSet<UK2Node_VariableGet*> AddedTargets;
	TSet<UK2Node_CallFunction*> AddedFunctions;
	TSet<UK2Node_Literal*> ReplacedTargets;
	TSet<UK2Node_CallFunctionOnMember*> ReplacedFunctions;

	UClass* CurrentClass;
	UEdGraph* Graph;
	TSet<UEdGraphNode*>& PastedNodes;
	const UEdGraphSchema_K2* K2Schema;

	FUpdatePastedNodes_Copy(
		UClass* InCurrentClass,
		TSet<UEdGraphNode*>& InPastedNodes,
		UEdGraph* InDestinationGraph)
		: CurrentClass(InCurrentClass)
		, Graph(InDestinationGraph)
		, PastedNodes(InPastedNodes)
		, K2Schema(GetDefault<UEdGraphSchema_K2>())
	{
		check(InCurrentClass && InDestinationGraph && K2Schema);
	}

	/**
	 *	Replace UK2Node_CallFunctionOnMember called on actor with a UK2Node_CallFunction.
	 *	When the blueprint has the member.
	 */
	void ReplaceAll()
	{
		for (UEdGraphNode* PastedNode : PastedNodes)
		{
			if (UK2Node_CallFunctionOnMember* CallOnMember = Cast<UK2Node_CallFunctionOnMember>(
				PastedNode))
			{
				if (UEdGraphPin* TargetInPin = CallOnMember->FindPin(UEdGraphSchema_K2::PN_Self))
				{
					const UClass* TargetClass = Cast<const UClass>(
						TargetInPin->PinType.PinSubCategoryObject.Get());

					const bool bTargetIsNullOrSingleLinked =
						TargetInPin->LinkedTo.Num() == 1 ||
						(!TargetInPin->LinkedTo.Num() && !TargetInPin->DefaultObject);

					const bool bCanCurrentBlueprintReplace = TargetClass
						&& CurrentClass->IsChildOf(TargetClass)
						// If current class if of the same type, it has the called member
						&& (!CallOnMember->MemberVariableToCallOn.IsSelfContext() && (TargetClass !=
							CurrentClass))
						// Make sure the class isn't self, using a explicit check in case the class hasn't been compiled since the member was added
						&& bTargetIsNullOrSingleLinked;

					if (bCanCurrentBlueprintReplace)
					{
						UEdGraphNode* TargetNode = TargetInPin->LinkedTo.Num()
							? TargetInPin->LinkedTo[0]->GetOwningNode()
							: nullptr;
						UK2Node_Literal* TargetLiteralNode = Cast<UK2Node_Literal>(TargetNode);

						const bool bPastedNodeShouldBeReplacedWithTarget = TargetLiteralNode
							&& !TargetLiteralNode->GetObjectRef()
							//The node delivering target actor is invalid
							&& PastedNodes.Contains(TargetLiteralNode);

						const bool bPastedNodeShouldBeReplacedWithoutTarget = !TargetNode || !
							PastedNodes.Contains(
								TargetNode);

						if (bPastedNodeShouldBeReplacedWithTarget ||
							bPastedNodeShouldBeReplacedWithoutTarget)
						{
							Replace(TargetLiteralNode, CallOnMember);
						}
					}
				}
			}
		}

		UpdatePastedCollection();
	}

private:
	void UpdatePastedCollection()
	{
		for (UK2Node_Literal* ReplacedTarget : ReplacedTargets)
		{
			if (ReplacedTarget && ReplacedTarget->GetValuePin() && !ReplacedTarget
				->GetValuePin()->LinkedTo.Num())
			{
				PastedNodes.Remove(ReplacedTarget);
				Graph->RemoveNode(ReplacedTarget);
			}
		}
		for (UK2Node_CallFunctionOnMember* ReplacedFunction : ReplacedFunctions)
		{
			PastedNodes.Remove(ReplacedFunction);
			Graph->RemoveNode(ReplacedFunction);
		}
		for (UK2Node_VariableGet* AddedTarget : AddedTargets)
		{
			PastedNodes.Add(AddedTarget);
		}
		for (UK2Node_CallFunction* AddedFunction : AddedFunctions)
		{
			PastedNodes.Add(AddedFunction);
		}
	}

	bool MoveAllLinksExeptSelf(UK2Node* NewNode, UK2Node* OldNode)
	{
		bool bResult = true;
		for (UEdGraphPin* OldPin : OldNode->Pins)
		{
			if (OldPin && (OldPin->PinName != UEdGraphSchema_K2::PN_Self))
			{
				UEdGraphPin* NewPin = NewNode->FindPin(OldPin->PinName);
				if (NewPin)
				{
					if (!K2Schema->MovePinLinks(*OldPin, *NewPin).CanSafeConnect())
					{
						UE_LOG(
							LogBlueprint,
							Error,
							TEXT("FUpdatePastedNodes: Cannot connect pin '%s' node '%s'"),
							*OldPin->PinName.ToString(),
							*OldNode->GetName());
						bResult = false;
					}
				}
				else
				{
					UE_LOG(
						LogBlueprint,
						Error,
						TEXT("FUpdatePastedNodes: Cannot find pin '%s'"),
						*OldPin->PinName.ToString());
					bResult = false;
				}
			}
		}
		return bResult;
	}

	void InitializeNewNode(
		UK2Node* NewNode,
		UK2Node* OldNode,
		float NodePosX = 0.0f,
		float NodePosY = 0.0f)
	{
		NewNode->NodePosX = OldNode ? OldNode->NodePosX : NodePosX;
		NewNode->NodePosY = OldNode ? OldNode->NodePosY : NodePosY;
		NewNode->SetFlags(RF_Transactional);
		Graph->AddNode(NewNode, false, false);
		NewNode->PostPlacedNewNode();
		NewNode->AllocateDefaultPins();
	}

	bool Replace(UK2Node_Literal* OldTarget, UK2Node_CallFunctionOnMember* OldCall)
	{
		bool bResult = true;
		check(OldCall);

		UK2Node_VariableGet* NewTarget = nullptr;
#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION >= 5
		const FProperty* Property = OldCall->MemberVariableToCallOn.ResolveMember<FProperty>();
#else
		const UProperty* Property = OldCall->MemberVariableToCallOn.ResolveMember<UProperty>();
#endif

		for (UK2Node_VariableGet* AddedTarget : AddedTargets)
		{
#if ENGINE_MINOR_VERSION >= 25 || ENGINE_MAJOR_VERSION >= 5
			if (AddedTarget && (Property == AddedTarget->VariableReference.ResolveMember<FProperty>(CurrentClass)))
#else
			if (AddedTarget && (Property == AddedTarget->VariableReference.ResolveMember<UProperty>(CurrentClass)))
#endif
			{
				NewTarget = AddedTarget;
				break;
			}
		}

		if (!NewTarget)
		{
			NewTarget = NewObject<UK2Node_VariableGet>(Graph);
			check(NewTarget);
#if ENGINE_MINOR_VERSION < 24 && ENGINE_MAJOR_VERSION == 4
			NewTarget->SetFromProperty(Property, true);
#else
			NewTarget->SetFromProperty(Property, true, Property->GetOwnerClass());
#endif
			AddedTargets.Add(NewTarget);
			const float AutoNodeOffsetX = 160.0f;
			InitializeNewNode(
				NewTarget,
				OldTarget,
				OldCall->NodePosX - AutoNodeOffsetX,
				OldCall->NodePosY);
		}

		if (OldTarget)
		{
			ReplacedTargets.Add(OldTarget);
		}

		UK2Node_CallFunction* NewCall = NewObject<UK2Node_CallFunction>(Graph);
		check(NewCall);
		NewCall->SetFromFunction(OldCall->GetTargetFunction());
		InitializeNewNode(NewCall, OldCall);
		AddedFunctions.Add(NewCall);

		if (!MoveAllLinksExeptSelf(NewCall, OldCall))
		{
			bResult = false;
		}

		if (NewTarget)
		{
			UEdGraphPin* SelfPin = NewCall->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			if (!K2Schema->TryCreateConnection(SelfPin, NewTarget->GetValuePin()))
			{
				UE_LOG(LogBlueprint, Error, TEXT("FUpdatePastedNodes: Cannot connect new self."));
				bResult = false;
			}
		}

		OldCall->BreakAllNodeLinks();

		ReplacedFunctions.Add(OldCall);
		return bResult;
	}
};

//~ End COPY OF BlueprintEditor.cpp FUpdatePastedNodes struct

static TSharedPtr<FBAInputProcessor> BAInputProcessorInstance;

void FBAInputProcessor::Create()
{
	BAInputProcessorInstance = MakeShareable(new FBAInputProcessor());
	FSlateApplication::Get().RegisterInputPreProcessor(BAInputProcessorInstance);
}

FBAInputProcessor& FBAInputProcessor::Get()
{
	return *BAInputProcessorInstance;
}

FBAInputProcessor::FBAInputProcessor()
{
	GlobalCommands = MakeShareable(new FUICommandList());
	GraphCommands = MakeShareable(new FUICommandList());
	GraphReadOnlyCommands = MakeShareable(new FUICommandList());
	SingleNodeCommands = MakeShareable(new FUICommandList());
	MultipleNodeCommands = MakeShareable(new FUICommandList());
	MultipleNodeCommandsIncludingComments = MakeShareable(new FUICommandList());
	PinCommands = MakeShareable(new FUICommandList());
	TabCommands = MakeShareable(new FUICommandList());
	ActionMenuCommands = MakeShareable(new FUICommandList());
	PinEditCommands = MakeShareable(new FUICommandList());
	BlueprintEditorCommands = MakeShareable(new FUICommandList());
	CreateGraphEditorCommands();

	CommandLists = {
		GraphCommands,
		GlobalCommands,
		TabCommands,
		GraphCommands,
		GraphReadOnlyCommands,
		SingleNodeCommands,
		MultipleNodeCommands,
		MultipleNodeCommandsIncludingComments,
		PinCommands,
		ActionMenuCommands,
		PinEditCommands,
		BlueprintEditorCommands
	};
}

FBAInputProcessor::~FBAInputProcessor() {}

void FBAInputProcessor::Cleanup()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(BAInputProcessorInstance);
	}

	BAInputProcessorInstance.Reset();
}

void FBAInputProcessor::Tick(
	const float DeltaTime,
	FSlateApplication& SlateApp,
	TSharedRef<ICursor> Cursor)
{
	if (IsGameRunningOrCompiling())
	{
		return;
	}

	FBATabHandler::Get().Tick(DeltaTime);

	if (UBARootObject* RootObject = FBlueprintAssistModule::Get().GetRootObject())
	{
		RootObject->Tick();
	}
}

bool FBAInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	// TODO: Perhaps implement a NavigationConfig, so users can't change focus on widgets
	// See FSlateApplication::SetNavigationConfig

	if (IsGameRunningOrCompiling())
	{
		return false;
	}

	if (SlateApp.IsInitialized())
	{
		GraphHandler = FBATabHandler::Get().GetActiveGraphHandler();

		// process toolbar commands
		if (FBAToolbar::Get().BlueprintAssistToolbarActions->ProcessCommandBindings(
			InKeyEvent.GetKey(),
			SlateApp.GetModifierKeys(),
			InKeyEvent.IsRepeat()))
		{
			return true;
		}

		if (GlobalCommands->ProcessCommandBindings(
			InKeyEvent.GetKey(),
			FSlateApplication::Get().GetModifierKeys(),
			InKeyEvent.IsRepeat()))
		{
			return true;
		}

		if (HasOpenBlueprintEditor())
		{
			if (BlueprintEditorCommands->ProcessCommandBindings(InKeyEvent.GetKey(), FSlateApplication::Get().GetModifierKeys(), InKeyEvent.IsRepeat()))
			{
				return true;
			}
		}

		if (!GraphHandler.IsValid())
		{
			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Invalid graph handler"));
			return false;
		}

		// cancel graph handler ongoing processes
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			GraphHandler->CancelSizeTimeoutNotification();
			GraphHandler->CancelCachingNotification();
			GraphHandler->CancelFormattingNodes();
			GraphHandler->ResetTransactions();
		}

		TSharedPtr<SDockTab> Tab = GraphHandler->GetTab();
		if (!Tab.IsValid() || !Tab->IsForeground())
		{
			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Tab invalid or not foreground"));
			return false;
		}

		TSharedPtr<SWidget> KeyboardFocusedWidget = SlateApp.GetKeyboardFocusedWidget();
		// if (KeyboardFocusedWidget.IsValid())
		// {
		// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("%s | %s"), *KeyboardFocusedWidget->GetTypeAsString(), *KeyboardFocusedWidget->ToString());
		// }
		// else
		// {
		// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("No keyboard focused widget!"));
		// }

		// try process graph action menu hotkeys
		TSharedPtr<SWindow> Menu = SlateApp.GetActiveTopLevelWindow();
		if (Menu.IsValid())
		{
			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Top Level window %s | %s"), *Menu->GetTitle().ToString(), *Menu->ToString());

			if (Menu->GetContent()->GetTypeAsString().Contains("SMenuContentWrapper"))
			{
				TSharedPtr<SWidget> ActionMenu = FBAUtils::GetChildWidget(Menu, "SGraphActionMenu");
				if (ActionMenu.IsValid())
				{
					//UE_LOG(LogBlueprintAssist, Warning, TEXT("Processing commands for action menu"));

					if (ActionMenuCommands->ProcessCommandBindings(
						InKeyEvent.GetKey(),
						FSlateApplication::Get().GetModifierKeys(),
						InKeyEvent.IsRepeat()))
					{
						return true;
					}
				}
			}
		}

		// get the keyboard focused widget
		if (!Menu.IsValid() || !KeyboardFocusedWidget.IsValid())
		{
			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Focus graph panel"));

			TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
			SlateApp.SetKeyboardFocus(GraphPanel);
			KeyboardFocusedWidget = GraphPanel;
		}

		// process commands for when you are editing a user input widget
		if (FBAUtils::IsUserInputWidget(KeyboardFocusedWidget))
		{
			if (FBAUtils::GetParentWidgetOfType(KeyboardFocusedWidget, "SGraphPin").IsValid())
			{
				if (PinEditCommands->ProcessCommandBindings(
					InKeyEvent.GetKey(),
					SlateApp.GetModifierKeys(),
					InKeyEvent.IsRepeat()))
				{
					return true;
				}
			}

			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				SlateApp.SetKeyboardFocus(GraphHandler->GetGraphPanel());
			}

			return false;
		}

		// process commands for when the tab is open
		if (TabCommands->ProcessCommandBindings(
			InKeyEvent.GetKey(),
			SlateApp.GetModifierKeys(),
			InKeyEvent.IsRepeat()))
		{
			return true;
		}

		//UE_LOG(LogBlueprintAssist, Warning, TEXT("Process tab commands"));

		if (!GraphHandler->IsWindowActive())
		{
			//TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
			//const FString CurrentWindowStr = CurrentWindow.IsValid()
			//	? CurrentWindow->GetTitle().ToString()
			//	: "nullptr";

			//TSharedPtr<SWindow> GHWindow = GraphHandler->GetOrFindWindow();
			//FString GHWindowStr = GHWindow.IsValid() ? GHWindow->GetTitle().ToString() : "Nullptr";
			//UE_LOG(
			//	LogBlueprintAssist,
			//	Warning,
			//	TEXT("Graph Handler window is not active %s current window | GH Window %s"),
			//	*CurrentWindowStr,
			//	*GHWindowStr);
			return false;
		}

		// process commands for when the graph exists but is read only
		if (GraphReadOnlyCommands->ProcessCommandBindings(
			InKeyEvent.GetKey(),
			SlateApp.GetModifierKeys(),
			InKeyEvent.IsRepeat()))
		{
			return true;
		}

		// skip all other graph commands if read only
		if (GraphHandler->IsGraphReadOnly())
		{
			return false;
		}

		// process general graph commands
		if (GraphCommands->ProcessCommandBindings(
			InKeyEvent.GetKey(),
			SlateApp.GetModifierKeys(),
			InKeyEvent.IsRepeat()))
		{
			return true;
		}

		// process commands for which require a node to be selected
		if (GraphHandler->GetSelectedPin() != nullptr)
		{
			if (PinCommands->ProcessCommandBindings(
				InKeyEvent.GetKey(),
				SlateApp.GetModifierKeys(),
				InKeyEvent.IsRepeat()))
			{
				return true;
			}
		}

		// process commands for which require a single node to be selected
		if (GraphHandler->GetSelectedNode() != nullptr)
		{
			//UE_LOG(LogBlueprintAssist, Warning, TEXT("Process node commands"));
			if (SingleNodeCommands->ProcessCommandBindings(
				InKeyEvent.GetKey(),
				SlateApp.GetModifierKeys(),
				InKeyEvent.IsRepeat()))
			{
				return true;
			}

			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				GraphHandler->GetGraphPanel()->SelectionManager.ClearSelectionSet();
			}
		}

		// process commands for which require multiple nodes to be selected
		if (GraphHandler->GetSelectedNodes().Num() > 0)
		{
			if (MultipleNodeCommands->ProcessCommandBindings(
				InKeyEvent.GetKey(),
				SlateApp.GetModifierKeys(),
				InKeyEvent.IsRepeat()))
			{
				return true;
			}

			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				GraphHandler->GetGraphPanel()->SelectionManager.ClearSelectionSet();
			}
		}

		// process commands for which require multiple nodes (incl comments) to be selected
		if (GraphHandler->GetSelectedNodes(true).Num() > 0)
		{
			if (MultipleNodeCommandsIncludingComments->ProcessCommandBindings(
				InKeyEvent.GetKey(),
				SlateApp.GetModifierKeys(),
				InKeyEvent.IsRepeat()))
			{
				return true;
			}

			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				GraphHandler->GetGraphPanel()->SelectionManager.ClearSelectionSet();
			}
		}
	}
	else
	{
		UE_LOG(LogBlueprintAssist, Error, TEXT("HandleKeyDown: Slate App not initialized"));
	}
	return false;
}

bool FBAInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (IsGameRunningOrCompiling())
	{
		return false;
	}

	TSharedPtr<FBAGraphHandler> MyGraphHandler = FBATabHandler::Get().GetActiveGraphHandler();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (MyGraphHandler.IsValid())
		{
			TSharedPtr<SGraphPanel> GraphPanel = MyGraphHandler->GetGraphPanel();
			if (MyGraphHandler->GetGraphPanel().IsValid())
			{
				if (GetDefault<UBASettings>()->bEnableShiftDraggingNodes)
				{
					TSharedPtr<SGraphNode> HoveredNode = FBAUtils::GetHoveredGraphNode(GraphPanel);
					if (HoveredNode)
					{
						bIsDragging = true;
						LastMousePos = FBAUtils::SnapToGrid(FBAUtils::ScreenSpaceToPanelCoord(MyGraphHandler->GetGraphPanel(), MouseEvent.GetScreenSpacePosition()) - NodeGrabOffset);
						NodeGrabOffset = HoveredNode->GetCachedGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
					}
				}
				
				TSharedPtr<SGraphPin> GraphPin = FBAUtils::GetHoveredGraphPin(GraphPanel);
				if (GraphPin.IsValid())
				{
					UEdGraphPin* Pin = GraphPin->GetPinObj();

					MyGraphHandler->SetSelectedPin(Pin);

					GraphPanel->SelectionManager.SelectSingleNode(Pin->GetOwningNode());
				}
			}
		}

		if (GetMutableDefault<UBASettings>()->bCustomDebug == 100)
		{
			auto ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
			TSharedPtr<SGraphPanel> GraphPanel = StaticCastSharedPtr<SGraphPanel>(FBAUtils::GetChildWidget(ActiveWindow, "SGraphPanel"));
			if (GraphPanel.IsValid())
			{
				TSharedPtr<SGraphNode> GraphNode = FBAUtils::GetHoveredGraphNode(GraphPanel);
				if (GraphNode.IsValid())
				{
					UEdGraphNode* Node = GraphNode->GetNodeObj();
					FBAUtils::PrintNodeInfo(Node);
				}
			}
		}
	}

	// Fix ongoing transactions being canceled via spawn node event on the graph. See FBlueprintEditor::OnSpawnGraphNodeByShortcut.
	if (MyGraphHandler.IsValid() && MyGraphHandler->HasActiveTransaction())
	{
		if (TSharedPtr<SGraphPanel> GraphPanel = MyGraphHandler->GetGraphPanel())
		{
			if (GraphPanel->IsHovered())
			{
				return true;
			}
		}
	}

	return false;
}

bool FBAInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = false;
	}

	return false;
}

bool FBAInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (IsGameRunningOrCompiling())
	{
		return false;
	}

	TSharedPtr<FBAGraphHandler> MyGraphHandler = FBATabHandler::Get().GetActiveGraphHandler();

	if (GetDefault<UBASettings>()->bEnableShiftDraggingNodes && bIsDragging && MyGraphHandler.IsValid() && MyGraphHandler->GetGraphPanel().IsValid())
	{
		const FVector2D NewMousePos = FBAUtils::SnapToGrid(FBAUtils::ScreenSpaceToPanelCoord(MyGraphHandler->GetGraphPanel(), MouseEvent.GetScreenSpacePosition()) - NodeGrabOffset);
		const FVector2D Delta = NewMousePos - LastMousePos;

		OnMouseDrag(SlateApp, NewMousePos, Delta);
		LastMousePos = NewMousePos;
	}

	return false;
}

void FBAInputProcessor::OnMouseDrag(FSlateApplication& SlateApp, const FVector2D& MousePos, const FVector2D& Delta)
{
	if (SlateApp.GetModifierKeys().IsShiftDown())
	{
		TSet<UEdGraphNode*> NodesToMove;

		// grab all linked nodes to move from the selected nodes
		TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes();
		for (UEdGraphNode* SelectedNode : SelectedNodes)
		{
			NodesToMove.Append(FBAUtils::GetNodeTree(SelectedNode));
		}

		for (UEdGraphNode* Node : NodesToMove)
		{
			if (!SelectedNodes.Contains(Node))
			{
				Node->Modify();
				Node->NodePosX += Delta.X; 
				Node->NodePosY += Delta.Y;
			}
		}
	}
}

void FBAInputProcessor::CreateGraphEditorCommands()
{
	////////////////////////////////////////////////////////////
	// Single Node Commands
	////////////////////////////////////////////////////////////

	SingleNodeCommands->MapAction(
		FBACommands::Get().ConnectUnlinkedPins,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnSmartWireSelectedNode),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSingleNodeSelected)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().ZoomToNodeTree,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ZoomToNodeTree),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSingleNodeSelected)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().DisconnectAllNodeLinks,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::DisconnectAllNodeLinks),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSingleNodeSelected)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().SelectPinUp,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectPinInDirection, 0, -1),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanSelectPinInDirection),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSingleNodeSelected)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().SelectPinDown,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectPinInDirection, 0, 1),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanSelectPinInDirection)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().SelectPinLeft,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectPinInDirection, -1, 0),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanSelectPinInDirection)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().SelectPinRight,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectPinInDirection, 1, 0),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanSelectPinInDirection)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().GetContextMenuForNode,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnGetContextMenuActions, false),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSingleNodeSelected)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().ReplaceNodeWith,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ReplaceNodeWith),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSingleNodeSelected)
	);

	SingleNodeCommands->MapAction(
		FBACommands::Get().RenameSelectedNode,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::RenameSelectedNode),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanRenameSelectedNode)
	);

	////////////////////////////////////////////////////////////
	// Multiple Node Commands
	////////////////////////////////////////////////////////////

	MultipleNodeCommands->MapAction(
		FBACommands::Get().FormatNodes,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::FormatNodes),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().FormatNodes_Selectively,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::FormatNodesSelectively),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().FormatNodes_Helixing,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::FormatNodesWithHelixing),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().FormatNodes_LHS,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::FormatNodesWithLHS),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().LinkNodesBetweenWires,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::LinkNodesBetweenWires),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().DisconnectNodeExecution,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::DisconnectExecutionOfSelectedNode),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().SwapNodeLeft,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SwapNodeInDirection, EGPD_Input),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().SwapNodeRight,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SwapNodeInDirection, EGPD_Output),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().DeleteAndLink,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::DeleteAndLink),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelected)
	);

	MultipleNodeCommands->MapAction(
		FBACommands::Get().ToggleNode,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ToggleNodes),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanToggleNodes)
	);

	////////////////////////////////////////////////////////////
	// Multiple Node Including Comments Commands
	////////////////////////////////////////////////////////////

	MultipleNodeCommandsIncludingComments->MapAction(
		FBACommands::Get().RefreshNodeSizes,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::RefreshNodeSizes),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasMultipleNodesSelectedInclComments)
	);

	////////////////////////////////////////////////////////////
	// Pin Commands
	////////////////////////////////////////////////////////////

	PinCommands->MapAction(
		FBACommands::Get().GetContextMenuForPin,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnGetContextMenuActions, true),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSelectedPin)
	);

	PinCommands->MapAction(
		FBACommands::Get().LinkToHoveredPin,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::LinkToHoveredPin),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSelectedPin)
	);

	PinCommands->MapAction(
		FBACommands::Get().LinkPinMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenPinLinkMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSelectedPin)
	);

	PinCommands->MapAction(
		FBACommands::Get().DuplicateNodeForEachLink,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::DuplicateNodeForEachLink),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSelectedPin)
	);

	PinCommands->MapAction(
		FBACommands::Get().EditSelectedPinValue,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnEditSelectedPinValue),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasSelectedPin)
	);

	////////////////////////////////////////////////////////////
	// Tab Commands
	////////////////////////////////////////////////////////////

	TabCommands->MapAction(
		FBACommands::Get().SelectNodeUp,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectAnyNodeInDirection, 0, -1),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().SelectNodeDown,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectAnyNodeInDirection, 0, 1),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().SelectNodeLeft,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectAnyNodeInDirection, -1, 0),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().SelectNodeRight,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SelectAnyNodeInDirection, 1, 0),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().ShiftCameraUp,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ShiftCameraInDirection, 0, -1),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().ShiftCameraDown,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ShiftCameraInDirection, 0, 1),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().ShiftCameraLeft,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ShiftCameraInDirection, -1, 0),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	TabCommands->MapAction(
		FBACommands::Get().ShiftCameraRight,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ShiftCameraInDirection, 1, 0),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenTab)
	);

	////////////////////////////////////////////////////////////
	// Graph Commands
	////////////////////////////////////////////////////////////

	GraphCommands->MapAction(
		FBACommands::Get().FormatAllEvents,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnFormatAllEvents),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraphNonReadOnly)
	);

	GraphCommands->MapAction(
		FBACommands::Get().OpenContextMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnOpenContextMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraphNonReadOnly)
	);

	GraphCommands->MapAction(
		FBACommands::Get().DisconnectPinLink,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::DisconnectPinOrWire),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraphNonReadOnly)
	);

	GraphCommands->MapAction(
		FBACommands::Get().SplitPin,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SplitPin),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraphNonReadOnly)
	);

	GraphCommands->MapAction(
		FBACommands::Get().RecombinePin,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::RecombinePin),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraphNonReadOnly)
	);

	GraphCommands->MapAction(
		FBACommands::Get().CreateRerouteNode,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::CreateRerouteNode),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraphNonReadOnly)
	);

	////////////////////////////////////////////////////////////
	// Graph Read Only Commands
	////////////////////////////////////////////////////////////

	GraphReadOnlyCommands->MapAction(
		FBACommands::Get().FocusGraphPanel,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::FocusGraphPanel),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasGraph)
	);

	////////////////////////////////////////////////////////////
	// Global Commands
	////////////////////////////////////////////////////////////

	GlobalCommands->MapAction(
		FBACommands::Get().DebugPrintGraphInfo,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::DebugPrintGraphUnderCursor)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().FocusSearchBox,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenFocusSearchBoxMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanExecuteActions)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().EditDetailsMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenEditDetailsMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanOpenEditDetailsMenu)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().OpenWindow,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenWindowMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanExecuteActions)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().OpenBlueprintAssistHotkeySheet,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenBlueprintAssistHotkeyMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanExecuteActions)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().TabSwitcherMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenTabSwitcherMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanExecuteActions)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().ToggleFullscreen,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::ToggleFullscreen),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanExecuteActions)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().SwitchWorkflowMode,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::SwitchWorkflowMode),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasWorkflowModes)
	);

	GlobalCommands->MapAction(
		FBACommands::Get().OpenAssetCreationMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenAssetCreationMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::CanExecuteActions)
	);

	////////////////////////////////////////////////////////////
	// Action Menu Commands
	////////////////////////////////////////////////////////////

	ActionMenuCommands->MapAction(
		FBACommands::Get().ToggleContextSensitive,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnToggleActionMenuContextSensitive),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenActionMenu)
	);

	////////////////////////////////////////////////////////////
	// Pin Edit Commands
	////////////////////////////////////////////////////////////

	PinEditCommands->MapAction(
		FBACommands::Get().EditSelectedPinValue,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OnEditSelectedPinValue),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasEditablePin)
	);

	////////////////////////////////////////////////////////////
	// Blueprint Editor Commands
	////////////////////////////////////////////////////////////

	BlueprintEditorCommands->MapAction(
		FBACommands::Get().VariableSelectorMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenVariableSelectorMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenBlueprintEditor)
	);

	BlueprintEditorCommands->MapAction(
		FBACommands::Get().AddSymbolMenu,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenCreateSymbolMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenBlueprintEditor)
	);

	BlueprintEditorCommands->MapAction(
		FBACommands::Get().GoToInGraph,
		FExecuteAction::CreateRaw(this, &FBAInputProcessor::OpenGoToSymbolMenu),
		FCanExecuteAction::CreateRaw(this, &FBAInputProcessor::HasOpenBlueprintEditor)
	);
}

void FBAInputProcessor::LinkToHoveredPin()
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	if (!GraphPanel.IsValid())
	{
		return;
	}

	UEdGraphPin* SelectedPin = GraphHandler->GetSelectedPin();
	if (SelectedPin != nullptr)
	{
		TSharedPtr<SGraphPin> HoveredPin = FBAUtils::GetHoveredGraphPin(GraphPanel);
		if (HoveredPin.IsValid())
		{
			const FScopedTransaction Transaction(
				NSLOCTEXT("UnrealEd", "LinkToHoveredPin", "Link To Hovered Pin"));

			if (FBAUtils::CanConnectPins(SelectedPin, HoveredPin->GetPinObj(), true, false))
			{
				FBAUtils::TryLinkPins(SelectedPin, HoveredPin->GetPinObj());
			}
		}
	}
}

void FBAInputProcessor::LinkNodesBetweenWires()
{
	if (!GraphHandler.IsValid())
	{
		return;
	}

	UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
	if (!Graph)
	{
		return;
	}

	TSharedPtr<SGraphPin> GraphPinForHoveredWire = FBAUtils::GetHoveredGraphPin(
		GraphHandler->GetGraphPanel());
	if (!GraphPinForHoveredWire.IsValid())
	{
		return;
	}

	UEdGraphPin* PinForHoveredWire = GraphPinForHoveredWire->GetPinObj();
	if (PinForHoveredWire == nullptr)
	{
		return;
	}

	TArray<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes().Array();

	if (SelectedNodes.Num() == 0)
	{
		return;
	}

	const auto LeftMostSort = [](const UEdGraphNode& NodeA, const UEdGraphNode& NodeB)
	{
		return NodeA.NodePosX < NodeB.NodePosX;
	};
	SelectedNodes.Sort(LeftMostSort);

	const auto IsSelected = [&SelectedNodes](UEdGraphNode* Node)
	{
		return SelectedNodes.Contains(Node);
	};

	UEdGraphNode* LeftMostNode =
		FBAUtils::GetTopMostWithFilter(SelectedNodes[0], EGPD_Input, IsSelected);

	UEdGraphNode* RightMostNode =
		FBAUtils::GetTopMostWithFilter(SelectedNodes[0], EGPD_Output, IsSelected);

	TSharedPtr<FScopedTransaction> Transaction =
		MakeShareable(
			new FScopedTransaction(
				NSLOCTEXT("UnrealEd", "LinkNodesBetweenWires", "Link Nodes Between Wires")));

	UEdGraphNode* First = PinForHoveredWire->Direction == EGPD_Output
		? LeftMostNode
		: RightMostNode;

	bool bCancelTransaction = true;

	TArray<FPinLink> PendingLinks;
	PendingLinks.Reserve(2);

	for (UEdGraphPin* Pin : First->Pins)
	{
		if (FBAUtils::CanConnectPins(PinForHoveredWire, Pin, true, false, false))
		{
			PendingLinks.Add(FPinLink(Pin, PinForHoveredWire));
			break;
		}
	}

	UEdGraphPin* ConnectedPin
		= PinForHoveredWire->LinkedTo.Num() > 0
		? PinForHoveredWire->LinkedTo[0]
		: nullptr;

	if (ConnectedPin != nullptr)
	{
		UEdGraphNode* ConnectedNode =
			PinForHoveredWire->Direction == EGPD_Output ? RightMostNode : LeftMostNode;

		for (UEdGraphPin* Pin : ConnectedNode->Pins)
		{
			if (FBAUtils::CanConnectPins(ConnectedPin, Pin, true, false, false))
			{
				PendingLinks.Add(FPinLink(Pin, ConnectedPin));
				break;
			}
		}
	}

	FEdGraphFormatterParameters FormatterParams;
	if (FBAUtils::GetFormatterSettings(Graph).GetAutoFormatting() == EBAAutoFormatting::FormatSingleConnected)
	{
		FormatterParams.NodesToFormat.Append(SelectedNodes);
		FormatterParams.NodesToFormat.Add(PinForHoveredWire->GetOwningNode());
	}

	for (FPinLink& Link : PendingLinks)
	{
		Link.From->BreakAllPinLinks();

		const bool bMadeLink = FBAUtils::TryCreateConnection(Link.From, Link.To);
		if (bMadeLink)
		{
			if (FBAUtils::GetFormatterSettings(Graph).GetAutoFormatting() != EBAAutoFormatting::Never)
			{
				GraphHandler->AddPendingFormatNodes(Link.GetFromNode(), Transaction, FormatterParams);
				GraphHandler->AddPendingFormatNodes(Link.GetToNode(), Transaction, FormatterParams);
			}

			bCancelTransaction = false;
		}
	}

	if (bCancelTransaction)
	{
		Transaction->Cancel();
	}
}

void FBAInputProcessor::CreateContextMenuFromPin(
	UEdGraphPin* Pin,
	const FVector2D& MenuLocation,
	const FVector2D& NodeLocation) const
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	if (!GraphPanel.IsValid())
	{
		return;
	}

	TArray<UEdGraphPin*> DragFromPins;
	DragFromPins.Add(Pin);
	TSharedPtr<SWidget> Widget = GraphPanel->SummonContextMenu(
		MenuLocation,
		NodeLocation,
		nullptr,
		nullptr,
		DragFromPins);

	FSlateApplication::Get().SetKeyboardFocus(Widget);
}

void FBAInputProcessor::OpenPinLinkMenu()
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	if (!GraphPanel.IsValid())
	{
		return;
	}

	UEdGraphPin* Pin = GraphHandler->GetSelectedPin();
	check(Pin != nullptr)

	TSharedRef<SLinkPinMenu> Widget =
		SNew(SLinkPinMenu)
		.SourcePin(Pin)
		.GraphHandler(GraphHandler);

	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenGoToSymbolMenu()
{
	TSharedRef<SGoToSymbolMenu> Widget = SNew(SGoToSymbolMenu);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenWindowMenu()
{
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();

	TSharedRef<SOpenWindowMenu> Widget = SNew(SOpenWindowMenu);

	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenFocusSearchBoxMenu()
{
	TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!Window.IsValid())
	{
		return;
	}

	TSharedRef<SFocusSearchBoxMenu> Widget = SNew(SFocusSearchBoxMenu);

	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenVariableSelectorMenu()
{
	TSharedRef<SVariableSelectorMenu> Widget = SNew(SVariableSelectorMenu);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenCreateSymbolMenu()
{
	TSharedRef<SAddSymbolMenu> Widget = SNew(SAddSymbolMenu);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenEditDetailsMenu()
{
	TSharedRef<SEditDetailsMenu> Widget = SNew(SEditDetailsMenu);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenTabSwitcherMenu()
{
	TSharedRef<SBATabSwitcher> Widget = SNew(SBATabSwitcher);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::DuplicateNodeForEachLink() const
{
	// Find the graph editor with focus
	UEdGraph* DestinationGraph = GraphHandler->GetFocusedEdGraph();
	if (DestinationGraph == nullptr)
	{
		return;
	}

	FBANodePinHandle SelectedPin(GraphHandler->GetSelectedPin());
	if (!SelectedPin.IsValid())
	{
		return;
	}

	// TODO: Make this work with multiple nodes
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	if (SelectedNode == nullptr)
	{
		return;
	}

	if (!FBAUtils::IsBlueprintGraph(DestinationGraph))
	{
		FNotificationInfo Notification(FText::FromString("Duplicate Node For Each Link only supports Blueprint graphs"));
		Notification.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(Notification);
		return;
	}

	if (!FBAUtils::IsNodePure(SelectedNode))
	{
		FNotificationInfo Notification(FText::FromString("Duplicate Node For Each Link currently only supports pure nodes"));

		Notification.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(Notification);
		return;
	}

	const UEdGraphSchema* Schema = DestinationGraph->GetSchema();
	if (!Schema)
	{
		return;
	}

	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "DuplicateNodesForEachLink", "Duplicate Node For Each Link")));

	DestinationGraph->Modify();

	// logic from FBlueprintEditor::PasteNodesHere
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(DestinationGraph);

	FGraphPanelSelectionSet SelectedNodes;

	const auto OwningNodeIsPure = [](UEdGraphPin* Pin)
	{
		return FBAUtils::IsNodePure(Pin->GetOwningNode());
	};

	const TSet<UEdGraphNode*> NodeTree = FBAUtils::GetNodeTreeWithFilter(SelectedNode, OwningNodeIsPure, EGPD_Input);

	for (UEdGraphNode* Node : NodeTree)
	{
		SelectedNodes.Emplace(Node);
	}

	SelectedNode->PrepareForCopying();
	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(SelectedNodes, ExportedText);

	struct FLocal
	{
		static void DeleteKnotsAndGetLinkedPins(
			UEdGraphPin* InPin,
			TArray<UEdGraphPin*>& LinkedPins)
		{
			/** Iterate across all linked pins */
			TArray<UEdGraphPin*> LinkedCopy = InPin->LinkedTo;
			for (UEdGraphPin* LinkedPin : LinkedCopy)
			{
				UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();

				if (FBAUtils::IsKnotNode(LinkedNode))
				{
					for (UEdGraphPin* Pin : FBAUtils::GetPinsByDirection(LinkedNode, InPin->Direction))
					{
						DeleteKnotsAndGetLinkedPins(Pin, LinkedPins);
					}
				}
				else
				{
					LinkedPins.Emplace(LinkedPin);
				}
			}

			/** Delete all connections for each knot node */
			if (UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(InPin->GetOwningNode()))
			{
				FBAUtils::DisconnectKnotNode(KnotNode);
				FBAUtils::DeleteNode(KnotNode);
			}
		}
	};

	TArray<UEdGraphPin*> LinkedPins;
	FLocal::DeleteKnotsAndGetLinkedPins(SelectedPin.GetPin(), LinkedPins);
	TArray<FBANodePinHandle> LinkedPinHandles = FBANodePinHandle::ConvertArray(LinkedPins);
	if (LinkedPinHandles.Num() <= 1)
	{
		return;
	}

	bool bNeedToModifyStructurally = false;

	SelectedPin.GetPin()->Modify();

	for (FBANodePinHandle& PinHandle : LinkedPinHandles)
	{
		PinHandle.GetPin()->Modify();

		// duplicate the node for each linked to pin
		Schema->BreakSinglePinLink(SelectedPin.GetPin(), PinHandle.GetPin());

		// import the nodes
		TSet<UEdGraphNode*> PastedNodes;
		FEdGraphUtilities::ImportNodesFromText(DestinationGraph, ExportedText, /*out*/ PastedNodes);

		for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
		{
			UEdGraphNode* Node = *It;

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("Node %s %d | Selected node %s %d"),
			//        *FBAUtils::GetNodeName(Node), Node->GetUniqueID(),
			//        *FBAUtils::GetNodeName(SelectedNode), SelectedNode->GetUniqueID()
			// );

			auto OldGuid = Node->NodeGuid;
			Node->CreateNewGuid();

			Node->NodePosX = FBAUtils::GetPinPos(GraphHandler, PinHandle.GetPin()).X;

			if (OldGuid != SelectedNode->NodeGuid)
			{
				continue;
			}

			// Update the selected node
			UK2Node* K2Node = Cast<UK2Node>(Node);
			if (K2Node != nullptr && K2Node->NodeCausesStructuralBlueprintChange())
			{
				bNeedToModifyStructurally = true;
			}

			UEdGraphPin* ValuePin = FBAUtils::GetPinsByDirection(Node, EGPD_Output)[0];
			ValuePin->MakeLinkTo(PinHandle.GetPin());
		}
	}

	for (UEdGraphNode* Node : NodeTree)
	{
		Node->Modify();
		FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	}

	if (bNeedToModifyStructurally)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	// Update UI
	DestinationGraph->NotifyGraphChanged();

	auto AutoFormatting = FBAUtils::GetFormatterSettings(DestinationGraph).GetAutoFormatting();

	if (AutoFormatting != EBAAutoFormatting::Never)
	{
		for (FBANodePinHandle& PinHandle : LinkedPinHandles)
		{
			GraphHandler->AddPendingFormatNodes(PinHandle.GetNode(), Transaction);
		}
	}
}

void FBAInputProcessor::RefreshNodeSizes() const
{
	TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes(true);

	auto Graph = GraphHandler->GetFocusedEdGraph();

	auto AutoFormatting = FBAUtils::GetFormatterSettings(Graph).GetAutoFormatting();

	if (SelectedNodes.Num() > 0)
	{
		TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "RefreshNodeSize", "Refresh Node Size")));

		FEdGraphFormatterParameters FormatterParams;

		if (AutoFormatting == EBAAutoFormatting::FormatSingleConnected)
		{
			TSet<UEdGraphNode*> NodeSet;
			for (UEdGraphNode* Node : SelectedNodes)
			{
				if (FBAUtils::IsGraphNode(Node))
				{
					NodeSet.Add(Node);
					if (UEdGraphNode* Linked = FBAUtils::GetFirstLinkedNodePreferringInput(Node))
					{
						NodeSet.Add(Linked);
					}
				}
			}

			FormatterParams.NodesToFormat = NodeSet.Array();
		}

		for (UEdGraphNode* Node : SelectedNodes)
		{
			GraphHandler->RefreshNodeSize(Node);

			if (AutoFormatting != EBAAutoFormatting::Never)
			{
				GraphHandler->AddPendingFormatNodes(Node, Transaction, FormatterParams);
			}
			else
			{
				Transaction->Cancel();
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Commands Event
//////////////////////////////////////////////////////////////////////////

void FBAInputProcessor::OnOpenContextMenu()
{
	TSharedPtr<SGraphEditor> GraphEditor = GraphHandler->GetGraphEditor();
	if (!GraphEditor.IsValid())
	{
		return;
	}

	const FVector2D MenuLocation = FSlateApplication::Get().GetCursorPos();
	const FVector2D SpawnLocation = GraphEditor->GetPasteLocation();

	GraphHandler->NodeToReplace = nullptr;

	UEdGraphPin* SelectedPin = GraphHandler->GetSelectedPin();

	if (SelectedPin != nullptr)
	{
		CreateContextMenuFromPin(SelectedPin, MenuLocation, SpawnLocation);
	}
	else
	{
		OpenContextMenu(MenuLocation, SpawnLocation);
	}
}

void FBAInputProcessor::FormatNodes() const
{
	TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes();
	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "FormatNode", "Format Node")));
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (FBAUtils::IsGraphNode(Node))
		{
			GraphHandler->AddPendingFormatNodes(Node, Transaction);
		}
	}
}

void FBAInputProcessor::FormatNodesSelectively()
{
	// TODO: Make selective formatting work with formatters other than EdGraph
	TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes();
	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "FormatOnlySelectedNodes", "Format Only Selected Nodes")));

	if (SelectedNodes.Num() == 1)
	{
		UEdGraphNode* SelectedNode = SelectedNodes.Array()[0];

		EEdGraphPinDirection Direction = FBAUtils::IsNodeImpure(SelectedNode) ? EGPD_Output : EGPD_Input;

		SelectedNodes = FBAUtils::GetNodeTree(SelectedNode, Direction, true);
	}

	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (FBAUtils::IsGraphNode(Node))
		{
			FEdGraphFormatterParameters FormatterParameters;
			FormatterParameters.NodesToFormat = SelectedNodes.Array();
			GraphHandler->AddPendingFormatNodes(Node, Transaction, FormatterParameters);
		}
	}
}

void FBAInputProcessor::FormatNodesWithHelixing() const
{
	TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes();
	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "FormatNodeHelixing", "Format Node with Helixing")));
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (FBAUtils::IsGraphNode(Node))
		{
			FEdGraphFormatterParameters FormatterParameters;
			FormatterParameters.OverrideFormattingStyle = MakeShareable(new EBAParameterFormattingStyle(EBAParameterFormattingStyle::Helixing));
			GraphHandler->AddPendingFormatNodes(Node, Transaction, FormatterParameters);
		}
	}
}

void FBAInputProcessor::FormatNodesWithLHS() const
{
	TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes();
	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "FormatNodeLHS", "Format Node with LHS")));
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (FBAUtils::IsGraphNode(Node))
		{
			FEdGraphFormatterParameters FormatterParameters;
			FormatterParameters.OverrideFormattingStyle = MakeShareable(new EBAParameterFormattingStyle(EBAParameterFormattingStyle::LeftSide));
			GraphHandler->AddPendingFormatNodes(Node, Transaction, FormatterParameters);
		}
	}
}

void FBAInputProcessor::SmartWireNode(UEdGraphNode* Node) const
{
	if (!FBAUtils::IsGraphNode(Node))
	{
		return;
	}

	UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
	if (!Graph)
	{
		return;
	}

	TSet<UEdGraphNode*> LHSNodes, RHSNodes;
	TSet<UEdGraphPin*> LHSPins, RHSPins;
	FBAUtils::SortNodesOnGraphByDistance(Node, Graph, LHSNodes, RHSNodes, LHSPins, RHSPins);

	TArray<TArray<UEdGraphPin*>> PinsByType;
	TArray<UEdGraphPin*> ExecPins = FBAUtils::GetExecPins(Node);
	TArray<UEdGraphPin*> ParamPins = FBAUtils::GetParameterPins(Node);
	PinsByType.Add(ExecPins);
	PinsByType.Add(ParamPins);
	for (const TArray<UEdGraphPin*>& Pins : PinsByType)
	{
		for (UEdGraphPin* PinA : Pins)
		{
			// skip if pin is hidden or if the pin already is connected
			if (PinA->bHidden || PinA->LinkedTo.Num() > 0 || PinA->Direction == EGPD_MAX)
			{
				continue;
			}

			// check all pins to the left if we are an input pin
			// check all pins to the right if we are an output pin
			bool IsInputPin = PinA->Direction == EGPD_Input;
			for (UEdGraphPin* PinB : IsInputPin ? LHSPins : RHSPins)
			{
				// skip if has connection
				if (PinB->LinkedTo.Num() > 0)
				{
					continue;
				}

				// UE_LOG(LogBlueprintAssist, Warning, TEXT("Checking pins %s %s"), *FBAUtils::GetPinName(PinA), *FBAUtils::GetPinName(PinB));

				//bool bShouldOverrideLink = FBlueprintAssistUtils::IsExecPin(PinA);
				if (!FBAUtils::CanConnectPins(PinA, PinB, false, false, false))
				{
					// UE_LOG(LogBlueprintAssist, Warning, TEXT("\tSkipping"));
					continue;
				}

				TSharedPtr<FScopedTransaction> Transaction = MakeShareable(
					new FScopedTransaction(
						NSLOCTEXT("UnrealEd", "ConnectUnlinkedPins", "Connect Unlinked Pins")
					));

				FBAUtils::TryLinkPins(PinA, PinB);

				if (FBAUtils::GetFormatterSettings(Graph).GetAutoFormatting() != EBAAutoFormatting::Never)
				{
					FEdGraphFormatterParameters FormatterParams;
					if (FBAUtils::GetFormatterSettings(Graph).GetAutoFormatting() == EBAAutoFormatting::FormatSingleConnected)
					{
						FormatterParams.NodesToFormat.Add(PinA->GetOwningNode());
						FormatterParams.NodesToFormat.Add(PinB->GetOwningNode());
					}

					GraphHandler->AddPendingFormatNodes(PinA->GetOwningNode(), Transaction, FormatterParams);
				}
				else
				{
					Transaction.Reset();
				}

				return;
			}
		}
	}
}

// TODO: unused function maybe merge this with smart wire
void FBAInputProcessor::LinkPinToNearest(
	UEdGraphPin* InPin,
	bool bOverrideLink,
	bool bFilterByDirection,
	float DistLimit) const
{
	if (InPin != nullptr)
	{
		FVector2D InPinPos = FBAUtils::GetPinPos(GraphHandler, InPin);

		// gather all the valid exec pins which we can connect to
		TArray<UEdGraphPin*> ValidPins;
		for (UEdGraphNode* Node : GraphHandler->GetFocusedEdGraph()->Nodes)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				// don't link to the same node
				if (Node == InPin->GetOwningNode())
				{
					continue;
				}

				FVector2D OtherPinPos = FBAUtils::GetPinPos(GraphHandler, Pin);

				// skip all pins further than the distance limit
				if (DistLimit > 0 && DistLimit <= FVector2D::Distance(InPinPos, OtherPinPos))
				{
					continue;
				}

				if (bFilterByDirection)
				{
					if (InPin->Direction == EGPD_Input)
					{
						if (OtherPinPos.X > InPinPos.X)
						{
							continue;
						}
					}
					else if (InPin->Direction == EGPD_Output)
					{
						if (OtherPinPos.X < InPinPos.X)
						{
							continue;
						}
					}
				}

				if (FBAUtils::CanConnectPins(InPin, Pin, bOverrideLink, false))
				{
					ValidPins.Add(Pin);
				}
			}
		}

		if (ValidPins.Num() > 0)
		{
			const auto DistanceSorter = [&](UEdGraphPin& PinA, UEdGraphPin& PinB)
			{
				const float DistA = FVector2D::Distance(InPinPos, FBAUtils::GetPinPos(GraphHandler, &PinA));
				const float DistB = FVector2D::Distance(InPinPos, FBAUtils::GetPinPos(GraphHandler, &PinB));
				return DistA < DistB;
			};
			ValidPins.Sort(DistanceSorter);
			UEdGraphPin* ClosestPin = ValidPins[0];
			FBAUtils::TryLinkPins(InPin, ClosestPin);
		}
	}
}

void FBAInputProcessor::OpenContextMenu(
	const FVector2D& MenuLocation,
	const FVector2D& NodeSpawnPosition) const
{
	UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
	if (!Graph)
	{
		return;
	}

	TSharedPtr<SGraphEditor> GraphEditor = GraphHandler->GetGraphEditor();
	if (!GraphEditor.IsValid())
	{
		return;
	}

	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	if (!GraphPanel.IsValid())
	{
		return;
	}

	const TArray<UEdGraphPin*> DummyPins;
	TSharedPtr<SWidget> WidgetToFocus = GraphPanel->SummonContextMenu(
		FSlateApplication::Get().GetCursorPos(),
		NodeSpawnPosition,
		nullptr,
		nullptr,
		DummyPins);

	// Focus the newly created context menu
	if (WidgetToFocus.IsValid())
	{
		FSlateApplication& SlateApp = FSlateApplication::Get();
		SlateApp.SetKeyboardFocus(WidgetToFocus);
	}
}

void FBAInputProcessor::OnSmartWireSelectedNode() const
{
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	if (SelectedNode == nullptr)
	{
		return;
	}

	//const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SmartWire", "Smart Wire Node"));
	SmartWireNode(SelectedNode);
}

void FBAInputProcessor::OnFormatAllEvents() const
{
	GraphHandler->FormatAllEvents();
}

void FBAInputProcessor::SelectNodeInDirection(
	const TArray<UEdGraphNode*>& Nodes,
	const int32 X,
	const int32 Y,
	const float DistLimit) const
{
	if (Nodes.Num() == 0)
	{
		return;
	}

	TSharedPtr<SGraphPanel> Panel = GraphHandler->GetGraphPanel();
	if (!Panel.IsValid())
	{
		return;
	}

	UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
	if (!Graph)
	{
		return;
	}

	// if selected node is null, then use the cursor location
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	const FVector2D StartPosition
		= SelectedNode != nullptr
		? FVector2D(SelectedNode->NodePosX, SelectedNode->NodePosY)
		: Panel->GetPastePosition();

	// filter all nodes on the graph towards our direction
	TArray<UEdGraphNode*> FilteredNodes;
	bool bIsXDirection = X != 0;
	for (UEdGraphNode* Other : Nodes)
	{
		// skip the currently selected
		if (Other == SelectedNode)
		{
			continue;
		}

		// skip comment nodes and knot nodes
		if (!FBAUtils::IsGraphNode(Other) || FBAUtils::IsCommentNode(Other) || FBAUtils::IsKnotNode(Other))
		{
			continue;
		}

		const float DeltaX = Other->NodePosX - StartPosition.X;
		const float DeltaY = Other->NodePosY - StartPosition.Y;

		if (bIsXDirection)
		{
			if (FMath::Sign(DeltaX) == X)
			{
				if (DistLimit <= 0 || (FMath::Abs(DeltaX) < DistLimit && FMath::Abs(DeltaY) < DistLimit * 0.5f))
				{
					FilteredNodes.Add(Other);
				}
			}
		}
		else // y direction
		{
			if (FMath::Sign(DeltaY) == Y)
			{
				if (DistLimit <= 0 || (FMath::Abs(DeltaY) < DistLimit && FMath::Abs(DeltaX) < DistLimit * 0.5f))
				{
					FilteredNodes.Add(Other);
				}
			}
		}
	}

	// no nodes found stop
	if (FilteredNodes.Num() == 0)
	{
		return;
	}

	// sort nodes by distance
	const auto& Sorter = [StartPosition, bIsXDirection](UEdGraphNode& A, UEdGraphNode& B)-> bool
	{
		const float XWeight = bIsXDirection ? 1 : 5;
		const float YWeight = bIsXDirection ? 5 : 1;

		float DeltaX = A.NodePosX - StartPosition.X;
		float DeltaY = A.NodePosY - StartPosition.Y;
		const float DistA = XWeight * DeltaX * DeltaX + YWeight * DeltaY * DeltaY;

		DeltaX = B.NodePosX - StartPosition.X;
		DeltaY = B.NodePosY - StartPosition.Y;
		const float DistB = XWeight * DeltaX * DeltaX + YWeight * DeltaY * DeltaY;

		return DistA < DistB;
	};
	FilteredNodes.Sort(Sorter);

	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();

	// now that we have sorted the nodes we get the closest node and select it
	UEdGraphNode* NodeToSelect = FilteredNodes[0];
	GraphHandler->SelectNode(NodeToSelect);
}

void FBAInputProcessor::SelectAnyNodeInDirection(const int X, const int Y) const
{
	UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
	if (Graph == nullptr)
	{
		return;
	}

	SelectNodeInDirection(Graph->Nodes, X, Y, 5000);
}

void FBAInputProcessor::ShiftCameraInDirection(const int X, const int Y) const
{
	/** get the current view location */
	FVector2D ViewLocation;
	float Zoom;
	GraphHandler->GetGraphEditor()->GetViewLocation(ViewLocation, Zoom);

	/** Shift the current view location */
	const FVector2D Offset = FVector2D(X, Y) * GetDefault<UBASettings>()->ShiftCameraDistance / Zoom;
	GraphHandler->BeginLerpViewport(ViewLocation + Offset, false);
}

void FBAInputProcessor::SelectCustomEventNodeInDirection(const int X, const int Y) const
{
	UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
	if (Graph == nullptr)
	{
		return;
	}

	auto FilterEvents = [](UEdGraphNode* Node)
	{
		return FBAUtils::IsEventNode(Node, EGPD_Output);
	};

	const TArray<UEdGraphNode*> EventNodes = Graph->Nodes.FilterByPredicate(FilterEvents);
	SelectNodeInDirection(EventNodes, X, Y, 0);
}

void FBAInputProcessor::SelectPinInDirection(const int X, const int Y) const
{
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	if (SelectedNode == nullptr)
	{
		GraphHandler->SetSelectedPin(nullptr);
		return;
	}

	if (FBAUtils::IsCommentNode(SelectedNode) || FBAUtils::IsKnotNode(SelectedNode))
	{
		GraphHandler->SetSelectedPin(nullptr);
		return;
	}

	const TArray<UEdGraphPin*> PinsOnSelectedNode = FBAUtils::GetPinsByDirection(SelectedNode);
	if (PinsOnSelectedNode.Num() == 0)
	{
		GraphHandler->SetSelectedPin(nullptr);
		return;
	}

	UEdGraphPin* SelectedPin = GraphHandler->GetSelectedPin();

	if (SelectedPin == nullptr)
	{
		GraphHandler->SetSelectedPin(FBAUtils::GetPinsByDirection(SelectedNode)[0]);
	}
	else
	{
		if (SelectedPin->GetOwningNode() != SelectedNode)
		{
			GraphHandler->SetSelectedPin(FBAUtils::GetPinsByDirection(SelectedNode)[0]);
		}
		else
		{
			const auto& IsPinVisibleAsAdvanced = [&](UEdGraphPin* Pin)
			{
				TSharedPtr<SGraphPin> GraphPin = FBAUtils::GetGraphPin(GraphHandler->GetGraphPanel(), Pin);
				return GraphPin.IsValid() &&
					GraphPin->IsPinVisibleAsAdvanced() == EVisibility::Visible;
			};

			if (X != 0) // x direction - switch to the opposite pins on the current node
			{
				// if we try to move the same direction as the selected pin, move to linked node instead
				if (X < 0 && SelectedPin->Direction == EGPD_Input ||
					X > 0 && SelectedPin->Direction == EGPD_Output)
				{
					const TArray<UEdGraphPin*> LinkedToIgnoringKnots = FBAUtils::GetPinLinkedToIgnoringKnots(SelectedPin);
					if (LinkedToIgnoringKnots.Num() > 0)
					{
						GraphHandler->SetSelectedPin(LinkedToIgnoringKnots[0], true);
					}
					return;
				}

				auto Direction = UEdGraphPin::GetComplementaryDirection(SelectedPin->Direction);

				TArray<UEdGraphPin*> Pins = FBAUtils::GetPinsByDirection(SelectedNode, Direction).FilterByPredicate(IsPinVisibleAsAdvanced);

				if (Pins.Num() > 0)
				{
					const int32 PinIndex = FBAUtils::GetPinIndex(SelectedPin);

					if (PinIndex != -1)
					{
						const int32 NextPinIndex = FMath::Min(Pins.Num() - 1, PinIndex);
						if (Pins.Num() > 0)
						{
							GraphHandler->SetSelectedPin(Pins[NextPinIndex]);
						}
					}
				}
			}
			else if (Y != 0) // y direction - move the selected pin up / down
			{
				TArray<UEdGraphPin*> Pins =
					FBAUtils::GetPinsByDirection(SelectedNode, SelectedPin->Direction)
					.FilterByPredicate(IsPinVisibleAsAdvanced);

				if (Pins.Num() > 1)
				{
					int32 PinIndex;
					Pins.Find(SelectedPin, PinIndex);
					if (PinIndex != -1) // we couldn't find the pin index
					{
						int32 NextPinIndex = PinIndex + Y;

						if (NextPinIndex < 0)
						{
							NextPinIndex = Pins.Num() + NextPinIndex;
						}
						else
						{
							NextPinIndex = NextPinIndex % Pins.Num();
						}

						GraphHandler->SetSelectedPin(Pins[NextPinIndex]);
					}
				}
			}
		}
	}
}

bool FBAInputProcessor::CanSelectPinInDirection()
{
	return HasSingleNodeSelected() && !FBAUtils::IsKnotNode(GraphHandler->GetSelectedNode());
}

void FBAInputProcessor::SwapNodeInDirection(const EEdGraphPinDirection Direction) const
{
	// PinA: Linked to pin in direction
	// PinB: Linked to pin opposite
	// PinC: Linked to PinA's Node in direction

	auto GraphHandlerCapture = GraphHandler;
	const auto TopMostPinSort = [GraphHandlerCapture](UEdGraphPin& PinA, UEdGraphPin& PinB)
	{
		return GraphHandlerCapture->GetPinY(&PinA) < GraphHandlerCapture->GetPinY(&PinB);
	};

	TArray<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes().Array();

	if (SelectedNodes.Num() == 0)
	{
		return;
	}

	const UEdGraphSchema* Schema = GraphHandler->GetFocusedEdGraph()->GetSchema();
	if (!Schema)
	{
		return;
	}

	const auto IsSelectedAndPure = [&SelectedNodes](UEdGraphNode* Node)
	{
		return FBAUtils::IsNodeImpure(Node) && SelectedNodes.Contains(Node) && FBAUtils::HasExecInOut(Node);
	};

	UEdGraphNode* LeftMostNode = FBAUtils::GetTopMostWithFilter(SelectedNodes[0], EGPD_Input, IsSelectedAndPure);

	UEdGraphNode* RightMostNode = FBAUtils::GetTopMostWithFilter(SelectedNodes[0], EGPD_Output, IsSelectedAndPure);

	UEdGraphNode* NodeInDirection = Direction == EGPD_Input ? LeftMostNode : RightMostNode;
	UEdGraphNode* NodeOpposite = Direction == EGPD_Input ? RightMostNode : LeftMostNode;

	// Process NodeInDirection
	TArray<UEdGraphPin*> LinkedPins =
		FBAUtils::GetLinkedPins(NodeInDirection, Direction).FilterByPredicate(FBAUtils::IsExecPin);

	if (LinkedPins.Num() == 0)
	{
		return;
	}

	FBANodePinHandle PinInDirection(LinkedPins[0]);
	if (PinInDirection.GetPin() ->LinkedTo.Num() == 0)
	{
		return;
	}

	// Process NodeOpposite
	const auto OppositeDirection = UEdGraphPin::GetComplementaryDirection(Direction);
	TArray<UEdGraphPin*> PinsOpposite = FBAUtils::GetPinsByDirection(NodeOpposite, OppositeDirection).FilterByPredicate(FBAUtils::IsExecPin);
	if (PinsOpposite.Num() == 0)
	{
		return;
	}

	FBANodePinHandle PinOpposite = PinsOpposite[0];

	// UE_LOG(LogBlueprintAssist, Warning, TEXT("PinInDirection %s (%s)"), *FBAUtils::GetPinName(PinInDirection), *FBAUtils::GetNodeName(PinInDirection->GetOwningNode()));
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("PinOpposite %s (%s)"), *FBAUtils::GetPinName(PinOpposite), *FBAUtils::GetNodeName(PinOpposite->GetOwningNode()));

	// Process NodeA
	auto PinInDLinkedTo = PinInDirection.GetPin()->LinkedTo;
	PinInDLinkedTo.StableSort(TopMostPinSort);
	FBANodePinHandle PinA(PinInDLinkedTo[0]);
	// UE_LOG(LogBlueprintAssist, Warning, TEXT("PinA %s (%s)"), *FBAUtils::GetPinName(PinA), *FBAUtils::GetNodeName(PinA->GetOwningNode()));
	UEdGraphNode* NodeA = PinA.GetNode();

	if (!FBAUtils::HasExecInOut(NodeA))
	{
		return;
	}

	TArray<FPinLink> PendingConnections;
	PendingConnections.Reserve(3);

	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "SwapNodes", "Swap Nodes")));

	{
		TArray<UEdGraphPin*> PinsAInDirection = FBAUtils::GetPinsByDirection(NodeA, Direction).FilterByPredicate(FBAUtils::IsExecPin);
		if (PinsAInDirection.Num() > 0)
		{
			UEdGraphPin* PinAInDirection = PinsAInDirection[0];
			PendingConnections.Add(FPinLink(PinAInDirection, PinOpposite.GetPin()));

			// Optional PinB
			if (PinAInDirection->LinkedTo.Num() > 0)
			{
				PinAInDirection->LinkedTo.StableSort(TopMostPinSort);
				UEdGraphPin* PinB = PinAInDirection->LinkedTo[0];
				// UE_LOG(LogBlueprintAssist, Warning, TEXT("PinB %s (%s)"), *FBAUtils::GetPinName(PinB), *FBAUtils::GetNodeName(PinB->GetOwningNode()));
				PendingConnections.Add(FPinLink(PinB, PinInDirection.GetPin()));
				Schema->BreakSinglePinLink(PinB, PinAInDirection);
			}
		}
	}

	{
		// Optional PinC
		TArray<UEdGraphPin*> LinkedToPinOpposite = PinOpposite.GetPin()->LinkedTo;
		if (LinkedToPinOpposite.Num() > 0)
		{
			LinkedToPinOpposite.StableSort(TopMostPinSort);
			UEdGraphPin* PinC = PinOpposite.GetPin()->LinkedTo[0];
			// UE_LOG(LogBlueprintAssist, Warning, TEXT("PinC %s (%s)"), *FBAUtils::GetPinName(PinC), *FBAUtils::GetNodeName(PinC->GetOwningNode()));
			PendingConnections.Add(FPinLink(PinC, PinA.GetPin()));
			Schema->BreakSinglePinLink(PinC, PinOpposite.GetPin());
		}
	}

	if (PendingConnections.Num() == 0)
	{
		Transaction->Cancel();
	}

	Schema->BreakSinglePinLink(PinInDirection.GetPin(), PinA.GetPin());

	for (FPinLink& Link : PendingConnections)
	{
		Schema->TryCreateConnection(Link.GetFromPin(), Link.GetToPin());
	}

	auto AutoFormatting = FBAUtils::GetFormatterSettings(GraphHandler->GetFocusedEdGraph()).GetAutoFormatting();

	if (AutoFormatting != EBAAutoFormatting::Never)
	{
		FEdGraphFormatterParameters FormatterParams;
		if (AutoFormatting == EBAAutoFormatting::FormatSingleConnected)
		{
			FormatterParams.NodesToFormat.Append(SelectedNodes);
			FormatterParams.NodesToFormat.Add(PinInDirection.GetNode());
		}

		GraphHandler->AddPendingFormatNodes(NodeInDirection, Transaction, FormatterParams);
	}
	else
	{
		UEdGraphNode* SelectedNodeToUse = Direction == EGPD_Output ? NodeOpposite : NodeInDirection;

		int32 DeltaX_Selected = NodeA->NodePosX - SelectedNodeToUse->NodePosX;
		int32 DeltaY_Selected = NodeA->NodePosY - SelectedNodeToUse->NodePosY;

		int32 DeltaX_A = SelectedNodeToUse->NodePosX - NodeA->NodePosX;
		int32 DeltaY_A = SelectedNodeToUse->NodePosY - NodeA->NodePosY;

		// Selected nodes: move node and parameters
		for (UEdGraphNode* SelectedNode : SelectedNodes)
		{
			TArray<UEdGraphNode*> NodeAndParams = FBAUtils::GetNodeAndParameters(SelectedNode);
			for (UEdGraphNode* Node : NodeAndParams)
			{
				Node->NodePosX += DeltaX_Selected;
				Node->NodePosY += DeltaY_Selected;
			}
		}

		// NodeA: move node and parameters
		for (UEdGraphNode* Node : FBAUtils::GetNodeAndParameters(NodeA))
		{
			Node->NodePosX += DeltaX_A;
			Node->NodePosY += DeltaY_A;
		}

		Transaction.Reset();
	}
}

void FBAInputProcessor::OnEditSelectedPinValue() const
{
	UEdGraphPin* SelectedPin = GraphHandler->GetSelectedPin();
	if (SelectedPin == nullptr)
	{
		return;
	}

	TSharedPtr<SGraphPin> GraphPin = FBAUtils::GetGraphPin(GraphHandler->GetGraphPanel(), SelectedPin);
	if (!GraphPin.IsValid())
	{
		return;
	}

	struct FLocal
	{
		static void GetEditableWidgets(TSharedPtr<SWidget> Widget, TArray<TSharedPtr<SWidget>>& EditableWidgets, TArray<TSharedPtr<SWidget>>& ClickableWidgets)
		{
			if (Widget.IsValid())
			{
				if (FBAUtils::IsUserInputWidget(Widget))
				{
					EditableWidgets.Add(Widget);
				}
				else if (FBAUtils::IsClickableWidget(Widget))
				{
					ClickableWidgets.Add(Widget);
				}

				// iterate through children
				if (FChildren* Children = Widget->GetChildren())
				{
					for (int i = 0; i < Children->Num(); i++)
					{
						GetEditableWidgets(Children->GetChildAt(i), EditableWidgets, ClickableWidgets);
					}
				}
			}
		}
	};

	TArray<TSharedPtr<SWidget>> EditableWidgets;
	TArray<TSharedPtr<SWidget>> ClickableWidgets;
	FLocal::GetEditableWidgets(GraphPin, EditableWidgets, ClickableWidgets);

	if (EditableWidgets.Num() > 0)
	{
		TSharedPtr<SWidget> CurrentlyFocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		const int32 CurrentIndex = EditableWidgets.IndexOfByKey(CurrentlyFocusedWidget);

		if (CurrentIndex == -1)
		{
			FSlateApplication::Get().SetKeyboardFocus(EditableWidgets[0], EFocusCause::Navigation);
		}
		else
		{
			const int32 NextIndex = (CurrentIndex + 1) % (EditableWidgets.Num());
			FSlateApplication::Get().SetKeyboardFocus(EditableWidgets[NextIndex], EFocusCause::Navigation);
		}
	}
	else if (ClickableWidgets.Num() > 0)
	{
		FBAUtils::InteractWithWidget(ClickableWidgets[0]);
	}
}

void FBAInputProcessor::DeleteAndLink()
{
	const auto& ShouldDeleteNode = [](UEdGraphNode* Node)
	{
		return Node->CanUserDeleteNode();
	};

	TArray<UEdGraphNode*> NodesToDelete = GraphHandler->GetSelectedNodes().Array().FilterByPredicate(ShouldDeleteNode);
	if (NodesToDelete.Num() > 0)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DeleteAndLink", "Delete and link"));

		DisconnectExecutionOfNodes(NodesToDelete);
		for (UEdGraphNode* Node : NodesToDelete)
		{
			FBAUtils::SafeDelete(GraphHandler, Node);
		}
	}
}

void FBAInputProcessor::ZoomToNodeTree() const
{
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	if (SelectedNode == nullptr)
	{
		return;
	}

	TSet<UEdGraphNode*> NodeTree = FBAUtils::GetNodeTree(SelectedNode);

	// selecting a set of nodes requires the ptrs to be const
	TSet<const UEdGraphNode*> ConstNodeTree;
	for (UEdGraphNode* Node : NodeTree)
	{
		ConstNodeTree.Add(Node);
	}

	TSharedPtr<SGraphEditor> GraphEditor = GraphHandler->GetGraphEditor();
	GraphHandler->GetFocusedEdGraph()->SelectNodeSet(ConstNodeTree);
	GraphHandler->GetGraphEditor()->ZoomToFit(true);
}

void FBAInputProcessor::OnGetContextMenuActions(const bool bUsePin) const
{
	UEdGraph* EdGraph = GraphHandler->GetFocusedEdGraph();
	if (EdGraph == nullptr)
	{
		return;
	}

	const UEdGraphSchema* Schema = EdGraph->GetSchema();
	if (Schema == nullptr)
	{
		return;
	}

	TSharedPtr<SGraphEditor> GraphEditor = GraphHandler->GetGraphEditor();
	const FVector2D MenuLocation = FSlateApplication::Get().GetCursorPos();
	const FVector2D SpawnLocation = GraphEditor->GetPasteLocation();

	UEdGraphNode* Node = GraphHandler->GetSelectedNode();

	UEdGraphPin* Pin = bUsePin
		? GraphHandler->GetSelectedPin()
		: nullptr;

	const TArray<UEdGraphPin*> DummyPins;
	GraphHandler->GetGraphPanel()->SummonContextMenu(MenuLocation, SpawnLocation, Node, Pin, DummyPins);
}

void FBAInputProcessor::OnToggleActionMenuContextSensitive() const
{
	TSharedPtr<SWindow> Menu = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!Menu.IsValid())
	{
		return;
	}

	if (!Menu->GetContent()->GetTypeAsString().Contains("SMenuContentWrapper"))
	{
		return;
	}

	TSharedPtr<SWidget> BlueprintActionMenu = FBAUtils::GetChildWidget(Menu, "SGraphActionMenu");
	if (!BlueprintActionMenu.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> ToggleSensitiveWidget = FBAUtils::GetChildWidget(Menu, "SCheckBox");
	if (!ToggleSensitiveWidget.IsValid())
	{
		return;
	}

	TSharedPtr<SCheckBox> ToggleSensitiveCheckBox = StaticCastSharedPtr<SCheckBox>(ToggleSensitiveWidget);
	ToggleSensitiveCheckBox->ToggleCheckedState();
}

void FBAInputProcessor::DisconnectPinOrWire()
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();

	if (GraphPanel.IsValid())
	{
		TSharedPtr<SGraphPin> HoveredPin = FBAUtils::GetHoveredGraphPin(GraphPanel);
		if (HoveredPin.IsValid())
		{
			if (UEdGraphPin* Pin = HoveredPin->GetPinObj())
			{
				const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DisconnectPinLink", "Disconnect Pin Link"));
				GraphPanel->GetGraphObj()->GetSchema()->BreakPinLinks(*Pin, true);
				return;
			}
		}
	}

	UEdGraphPin* SelectedPin = GraphHandler->GetSelectedPin();
	if (SelectedPin != nullptr)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DisconnectPinLink", "DisconectPinLink"));
		GraphPanel->GetGraphObj()->GetSchema()->BreakPinLinks(*SelectedPin, true);
	}
}

void FBAInputProcessor::DisconnectExecutionOfSelectedNode()
{
	TArray<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes().Array();
	DisconnectExecutionOfNodes(SelectedNodes);
}

void FBAInputProcessor::DisconnectExecutionOfNodes(TArray<UEdGraphNode*> Nodes)
{
	// TODO: Make this work for pure nodes
	FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DisconnectExecutionForNodes", "Disconnect Execution for Nodes"));

	if (Nodes.Num() == 0)
	{
		Transaction.Cancel();
		return;
	}

	int CountError = 0;

	const UEdGraphSchema* Schema = GraphHandler->GetFocusedEdGraph()->GetSchema();

	while (Nodes.Num() > 0)
	{
		CountError += 1;
		if (CountError > 1000)
		{
			UE_LOG(LogBlueprintAssist, Error, TEXT("DisconnectExecutionOfNodes caused infinite loop: Please report this on the wiki"));
			return;
		}

		UEdGraphNode* NextNode = Nodes[0];

		const auto Filter = [&Nodes](UEdGraphNode* Node)
		{
			return Nodes.Contains(Node);
		};

		TArray<UEdGraphNode*> NodeTree = FBAUtils::GetExecutionTreeWithFilter(NextNode, Filter).Array();
		if (NodeTree.Num() > 0)
		{
			const auto InNodeTree = [&NodeTree](UEdGraphNode* Node)
			{
				return NodeTree.Contains(Node);
			};

			UEdGraphNode* LeftMostNode = FBAUtils::GetTopMostWithFilter(NodeTree[0], EGPD_Input, InNodeTree);
			UEdGraphNode* RightMostNode = FBAUtils::GetTopMostWithFilter(NodeTree[0], EGPD_Output, InNodeTree);

			TArray<UEdGraphPin*> LinkedToInput = FBAUtils::GetLinkedToPins(LeftMostNode, EGPD_Input).FilterByPredicate(FBAUtils::IsExecPin);
			TArray<UEdGraphPin*> LinkedToOutput = FBAUtils::GetLinkedToPins(RightMostNode, EGPD_Output).FilterByPredicate(FBAUtils::IsExecPin);

			for (UEdGraphPin* Input : LinkedToInput)
			{
				for (UEdGraphPin* Output : LinkedToOutput)
				{
					if (FBAUtils::CanConnectPins(Input, Output, true, false))
					{
						Input->MakeLinkTo(Output);
						break;
					}
				}
			}

			const TArray<UEdGraphPin*> LeftMostInputPins = FBAUtils::GetExecPins(LeftMostNode, EGPD_Input);
			const TArray<UEdGraphPin*> RightMostOutputPins = FBAUtils::GetExecPins(RightMostNode, EGPD_Output);

			TArray<FBANodePinHandle> PinsToBreak;
			PinsToBreak.Reserve(LeftMostInputPins.Num() + RightMostOutputPins.Num());

			for (UEdGraphPin* Pin : LeftMostInputPins)
			{
				PinsToBreak.Add(FBANodePinHandle(Pin));
			}

			for (UEdGraphPin* Pin : RightMostOutputPins)
			{
				PinsToBreak.Add(FBANodePinHandle(Pin));
			}

			for (auto& Pin : PinsToBreak)
			{
				Schema->BreakPinLinks(*Pin.GetPin(), true);
			}
		}

		for (UEdGraphNode* Node : NodeTree)
		{
			Nodes.RemoveSwap(Node);
		}
	}
}

void FBAInputProcessor::DisconnectAllNodeLinks()
{
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	const UEdGraphSchema* Schema = GraphHandler->GetFocusedEdGraph()->GetSchema();
	if (SelectedNode != nullptr)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DisconnectAllNodeLinks", "Disconnect All Node Links"));

		Schema->BreakNodeLinks(*SelectedNode);
	}
}

void FBAInputProcessor::ReplaceNodeWith()
{
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	if (SelectedNode == nullptr || !SelectedNode->CanUserDeleteNode())
	{
		return;
	}

	TSharedPtr<SGraphEditor> GraphEditor = GraphHandler->GetGraphEditor();
	if (!GraphEditor.IsValid())
	{
		return;
	}

	const FVector2D MenuLocation = FSlateApplication::Get().GetCursorPos();
	const FVector2D SpawnLocation(SelectedNode->NodePosX, SelectedNode->NodePosY);

	TSharedPtr<FScopedTransaction> Transaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("UnrealEd", "ReplaceNodeWith", "Replace Node With")));

	OpenContextMenu(MenuLocation, SpawnLocation);

	GraphHandler->NodeToReplace = SelectedNode;
	GraphHandler->SetReplaceNewNodeTransaction(Transaction);

	FSlateApplication& SlateApp = FSlateApplication::Get();
	if (SlateApp.IsInitialized())
	{
		TSharedPtr<SWindow> Menu = SlateApp.GetActiveTopLevelWindow();

		if (Menu.IsValid())
		{
			if (Menu->GetContent()->GetTypeAsString().Contains("SMenuContentWrapper"))
			{
				TSharedPtr<SWidget> ActionMenu = FBAUtils::GetChildWidget(Menu, "SGraphActionMenu");

				if (ActionMenu.IsValid())
				{
#if ENGINE_MINOR_VERSION < 22 && ENGINE_MAJOR_VERSION == 4
					Menu->SetOnWindowClosed(FOnWindowClosed::CreateRaw(this, &FBAInputProcessor::OnReplaceNodeMenuClosed));
#else
					Menu->GetOnWindowClosedEvent().AddRaw(this, &FBAInputProcessor::OnReplaceNodeMenuClosed);
#endif
				}
			}
		}
	}
}

void FBAInputProcessor::OnReplaceNodeMenuClosed(const TSharedRef<SWindow>& Window)
{
	GraphHandler->ResetSingleNewNodeTransaction();
}

void FBAInputProcessor::DebugPrintGraphUnderCursor() const
{
	FSlateApplication& SlateApp = FSlateApplication::Get();
	FWidgetPath Path = SlateApp.LocateWindowUnderMouse(SlateApp.GetCursorPos(), SlateApp.GetInteractiveTopLevelWindows());
	if (Path.IsValid())
	{
		TSharedRef<SWidget> Widget = Path.GetLastWidget();
		TSharedPtr<SWidget> GraphPanelAsWidget = FBAUtils::GetChildWidget(Widget, "SGraphPanel");
		if (GraphPanelAsWidget.IsValid())
		{
			auto GraphPanel = StaticCastSharedPtr<SGraphPanel>(GraphPanelAsWidget);
			if (GraphPanel.IsValid())
			{
				if (UEdGraph* EdGraph = GraphPanel->GetGraphObj())
				{
					FName GraphTypeName = EdGraph->GetClass()->GetFName();
					EGraphType GraphType = FBAUtils::GetGraphType(EdGraph);

					UE_LOG(LogBlueprintAssist, Log, TEXT("PRINTING GRAPH INFO: GraphName <%s> | GraphType <%d>"), *GraphTypeName.ToString(), GraphType);

					if (FBAFormatterSettings* FormatterSettings = FBAUtils::FindFormatterSettings(EdGraph))
					{
						UE_LOG(LogBlueprintAssist, Warning, TEXT("Formatter settings %s"), *FormatterSettings->ToString());
					}
				}
			}
		}
	}

	TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	TSharedPtr<SGraphPanel> GraphPanel = StaticCastSharedPtr<SGraphPanel>(FBAUtils::GetChildWidget(ActiveWindow, "SGraphPanel"));
	if (GraphPanel.IsValid())
	{
		const auto& SelectedNodes = GraphPanel->SelectionManager.GetSelectedNodes();
		if (SelectedNodes.Num() > 0)
		{
			FBAUtils::PrintNodeInfo(Cast<UEdGraphNode>(SelectedNodes.Array()[0]));
		}
	}
}

// TODO: figure out a nice way to make this work for non-bp graphs as well
void FBAInputProcessor::ToggleNodes()
{
	TSet<UEdGraphNode*> SelectedNodes = GraphHandler->GetSelectedNodes();

	auto OnlyPureNodes = [](UEdGraphNode* Node)
	{
		return !FBAUtils::IsKnotNode(Node) && !FBAUtils::IsCommentNode(Node) && FBAUtils::IsNodeImpure(Node);
	};

	TArray<UEdGraphNode*> FilteredNodes = SelectedNodes.Array().FilterByPredicate(OnlyPureNodes);

	if (FilteredNodes.Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "ToggleNodes", "Toggle Nodes"));

	bool bAllNodesDisabled = true;
	for (UEdGraphNode* Node : FilteredNodes)
	{
		if (Node->GetDesiredEnabledState() != ENodeEnabledState::Disabled)
		{
			bAllNodesDisabled = false;
			break;
		}
	}

	for (UEdGraphNode* Node : FilteredNodes)
	{
		if (bAllNodesDisabled) // Set nodes to their default state
		{
			ENodeEnabledState DefaultEnabledState = ENodeEnabledState::Enabled;

			if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
			{
				if (UFunction* Function = CallFunctionNode->GetTargetFunction())
				{
					if (Function->HasMetaData(FBlueprintMetadata::MD_DevelopmentOnly))
					{
						DefaultEnabledState = ENodeEnabledState::DevelopmentOnly;
					}
				}
			}

			Node->Modify();
			Node->SetEnabledState(DefaultEnabledState);
		}
		else // Set all nodes to disabled
		{
			if (Node->GetDesiredEnabledState() != ENodeEnabledState::Disabled)
			{
				Node->Modify();
				Node->SetEnabledState(ENodeEnabledState::Disabled);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GraphHandler->GetBlueprint());
}

void FBAInputProcessor::SplitPin()
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	if (!GraphPanel.IsValid())
	{
		return;
	}

	auto EdGraph = GraphHandler->GetFocusedEdGraph();
	if (!EdGraph)
	{
		return;
	}

	UEdGraphPin* PinToUse = nullptr;

	TSharedPtr<SGraphPin> HoveredPin = FBAUtils::GetHoveredGraphPin(GraphPanel);
	if (HoveredPin.IsValid())
	{
		PinToUse = HoveredPin->GetPinObj();
	}

	if (PinToUse == nullptr)
	{
		PinToUse = GraphHandler->GetSelectedPin();
	}

	if (PinToUse != nullptr)
	{
		const UEdGraphSchema* Schema = EdGraph->GetSchema();

		if (PinToUse->GetOwningNode()->CanSplitPin(PinToUse))
		{
			const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SplitPin", "Split Pin"));

			Schema->SplitPin(PinToUse);

			GraphHandler->SetSelectedPin(PinToUse->SubPins[0]);
		}
	}
}

void FBAInputProcessor::RecombinePin()
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	if (!GraphPanel.IsValid())
	{
		return;
	}

	auto EdGraph = GraphHandler->GetFocusedEdGraph();
	if (!EdGraph)
	{
		return;
	}

	UEdGraphPin* PinToUse = nullptr;

	TSharedPtr<SGraphPin> HoveredPin = FBAUtils::GetHoveredGraphPin(GraphPanel);
	if (HoveredPin.IsValid())
	{
		PinToUse = HoveredPin->GetPinObj();
	}

	if (PinToUse == nullptr)
	{
		PinToUse = GraphHandler->GetSelectedPin();
	}

	if (PinToUse != nullptr)
	{
		const UEdGraphSchema* Schema = EdGraph->GetSchema();

		if (PinToUse->ParentPin != nullptr)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "RecombinePin", "Recombine Pin"));
			GraphHandler->SetSelectedPin(PinToUse->ParentPin);
			Schema->RecombinePin(PinToUse);
		}
	}
}

void FBAInputProcessor::CreateRerouteNode()
{
	TSharedPtr<SGraphPanel> GraphPanel = GraphHandler->GetGraphPanel();
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
	const FVector2D GraphPosition = GraphPanel->PanelCoordToGraphCoord(GraphPanel->GetTickSpaceGeometry().AbsoluteToLocal(CursorPos));

	UEdGraphPin* PinToCreateFrom = nullptr;

	// get pin from knot node
	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
	if (UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(SelectedNode))
	{
		if (GraphPosition.X > KnotNode->NodePosX)
		{
			PinToCreateFrom = KnotNode->GetOutputPin();
		}
		else
		{
			PinToCreateFrom = KnotNode->GetInputPin();
		}

		// if (!FBAUtils::IsPinLinked(KnotNode->GetOutputPin()))
		// {
		// 	PinToCreateFrom = KnotNode->GetOutputPin();
		// }
		// else if (!FBAUtils::IsPinLinked(KnotNode->GetInputPin()))
		// {
		// 	PinToCreateFrom = KnotNode->GetInputPin();
		// }
	}

	// get selected pin
	if (!PinToCreateFrom)
	{
		PinToCreateFrom = GraphHandler->GetSelectedPin();
	}

	if (!PinToCreateFrom)
	{
		return;
	}

	// get hovered graph pin
	auto HoveredGraphPin = FBAUtils::GetHoveredGraphPin(GraphHandler->GetGraphPanel());
	if (HoveredGraphPin.IsValid())
	{
		UEdGraphPin* HoveredPin = HoveredGraphPin->GetPinObj();

		if (FBAUtils::CanConnectPins(PinToCreateFrom, HoveredPin, true))
		{
			if (FBAUtils::TryCreateConnection(PinToCreateFrom, HoveredPin, true))
			{
				return;
			}
		}
	}

	UEdGraphPin* LinkedPin = PinToCreateFrom->LinkedTo.Num() > 0 ? PinToCreateFrom->LinkedTo[0] : nullptr;

	//@TODO: This constant is duplicated from inside of SGraphNodeKnot
	const FVector2D NodeSpacerSize(42.0f, 14.0f);

	FVector2D KnotTopLeft = GraphPosition;

	// Create a new knot
	UEdGraph* ParentGraph = PinToCreateFrom->GetOwningNode()->GetGraph();
	if (!FBlueprintEditorUtils::IsGraphReadOnly(ParentGraph))
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "CreateRerouteNode_BlueprintAssist", "Create Reroute Node"));

		UK2Node_Knot* NewKnot = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Knot>(ParentGraph, KnotTopLeft - NodeSpacerSize * 0.5f, EK2NewNodeFlags::SelectNewNode);

		// Move the connections across (only notifying the knot, as the other two didn't really change)
		UEdGraphPin* NewKnotPin = (PinToCreateFrom->Direction == EGPD_Output) ? NewKnot->GetInputPin() : NewKnot->GetOutputPin();

		PinToCreateFrom->MakeLinkTo(NewKnotPin);

		if (LinkedPin != nullptr)
		{
			PinToCreateFrom->BreakLinkTo(LinkedPin);
			UEdGraphPin* NewKnotPinForLinkedPin = (LinkedPin->Direction == EGPD_Output) ? NewKnot->GetInputPin() : NewKnot->GetOutputPin();
			LinkedPin->MakeLinkTo(NewKnotPinForLinkedPin);
		}

		NewKnot->PostReconstructNode();

		TSharedPtr<SGraphPin> GraphPin = FBAUtils::GetGraphPin(GraphPanel, PinToCreateFrom);
		const float PinY = PinToCreateFrom->GetOwningNode()->NodePosY + GraphPin->GetNodeOffset().Y;
		const float HeightDiff = FMath::Abs(PinY - KnotTopLeft.Y);
		if (HeightDiff < 25)
		{
			NewKnot->NodePosY = PinY - NodeSpacerSize.Y * 0.5f;
		}

		// Dirty the blueprint
		if (UBlueprint* Blueprint = GraphHandler->GetBlueprint())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}
}

void FBAInputProcessor::OpenBlueprintAssistHotkeyMenu()
{
	TSharedRef<SBAHotkeyMenu> Widget = SNew(SBAHotkeyMenu).BindingContextName("BlueprintAssistCommands");
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::RenameSelectedNode()
{
	if (!GraphHandler)
	{
		return;
	}

	UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();

	FName ItemName;

	if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(SelectedNode))
	{
		ItemName = VariableNode->GetVarName();
	}
	else if (UK2Node_CallFunction* FunctionCall = Cast<UK2Node_CallFunction>(SelectedNode))
	{
		ItemName = FunctionCall->FunctionReference.GetMemberName();
	}
	else if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(SelectedNode))
	{
		ItemName = Macro->GetMacroGraph()->GetFName();
	}

	TSharedPtr<SGraphActionMenu> ActionMenu = FBAUtils::GetGraphActionMenu();
	if (!ActionMenu)
	{
		return;
	}

	if (!ItemName.IsNone())
	{
		ActionMenu->SelectItemByName(ItemName, ESelectInfo::OnKeyPress);
		if (ActionMenu->CanRequestRenameOnActionNode())
		{
			ActionMenu->OnRequestRenameOnActionNode();
		}
	}
}

void FBAInputProcessor::ToggleFullscreen()
{
	TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (Window.IsValid())
	{
		if (Window->IsWindowMaximized())
		{
			Window->Restore();
		}
		else
		{
			Window->Maximize();
		}
	}
}

void FBAInputProcessor::SwitchWorkflowMode()
{
	if (UObject* CurrentAsset = FBAUtils::GetAssetForActiveTab<UObject>())
	{
		if (FWorkflowCentricApplication* App = static_cast<FWorkflowCentricApplication*>(FBAUtils::GetEditorFromActiveTab()))
		{
			const FString AssetClassName = CurrentAsset->GetClass()->GetName();
			if (AssetClassName == "WidgetBlueprint")
			{
				static const FName GraphMode(TEXT("GraphName"));
				static const FName DesignerMode(TEXT("DesignerName"));

				const FName& NewMode = App->IsModeCurrent(DesignerMode) ? GraphMode : DesignerMode;  
				App->SetCurrentMode(NewMode);
				return;
			}
			else if (AssetClassName == "BehaviorTree")
			{
				static const FName BehaviorTreeMode(TEXT("BehaviorTree"));
				static const FName BlackboardMode(TEXT("Blackboard"));

				const FName& NewMode = App->IsModeCurrent(BehaviorTreeMode) ? BlackboardMode : BehaviorTreeMode;  
				App->SetCurrentMode(NewMode);
				return;
			}
		}
	}

	TSharedRef<SBAWorkflowModeMenu> Widget = SNew(SBAWorkflowModeMenu);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::OpenAssetCreationMenu()
{
	TSharedRef<SBACreateAssetMenu> Widget = SNew(SBACreateAssetMenu);
	FBAUtils::OpenPopupMenu(Widget, Widget->GetWidgetSize());
}

void FBAInputProcessor::FocusGraphPanel()
{
	if (GraphHandler && GraphHandler->GetGraphPanel())
	{
		FSlateApplication::Get().SetKeyboardFocus(GraphHandler->GetGraphPanel(), EFocusCause::SetDirectly);
	}
}

bool FBAInputProcessor::CanExecuteActions() const
{
	return
		!GEditor->bIsSimulatingInEditor &&
		!GEditor->PlayWorld &&
		FSlateApplication::Get().IsInitialized();
}

bool FBAInputProcessor::HasEditablePin() const
{
	if (!CanExecuteActions())
	{
		return false;
	}

	const FSlateApplication& SlateApp = FSlateApplication::Get();
	TSharedPtr<SWidget> KeyboardFocusedWidget = SlateApp.GetKeyboardFocusedWidget();
	TSharedPtr<SWindow> Menu = SlateApp.GetActiveTopLevelWindow();

	return
		FBAUtils::IsUserInputWidget(KeyboardFocusedWidget) &&
		FBAUtils::GetParentWidgetOfType(KeyboardFocusedWidget, "SGraphPin").IsValid();
}

bool FBAInputProcessor::HasOpenActionMenu() const
{
	if (!HasOpenTab())
	{
		return false;
	}

	TSharedPtr<SWindow> Menu = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (Menu.IsValid())
	{
		if (Menu->GetContent()->GetTypeAsString().Contains("SMenuContentWrapper"))
		{
			TSharedPtr<SWidget> ActionMenu = FBAUtils::GetChildWidget(Menu, "SGraphActionMenu");
			if (ActionMenu.IsValid())
			{
				return true;
			}
		}
	}

	return false;
}

bool FBAInputProcessor::HasOpenBlueprintEditor() const
{
	FBlueprintEditor* BPEditor = FBAUtils::GetEditorFromActiveTabCasted<UBlueprint, FBlueprintEditor>();
	return BPEditor != nullptr && BPEditor->InEditingMode();
}

bool FBAInputProcessor::HasOpenTab() const
{
	if (CanExecuteActions() && GraphHandler.IsValid())
	{
		TSharedPtr<SDockTab> Tab = GraphHandler->GetTab();
		return Tab.IsValid() && Tab->IsForeground();
	}

	return false;
}

bool FBAInputProcessor::HasGraph() const
{
	if (CanExecuteActions() && HasOpenTab())
	{
		return GraphHandler->IsWindowActive();
	}

	return false;
}

bool FBAInputProcessor::HasGraphNonReadOnly() const
{
	if (CanExecuteActions() && HasOpenTab())
	{
		return GraphHandler->IsWindowActive() && !GraphHandler->IsGraphReadOnly();
	}

	return false;
}

bool FBAInputProcessor::HasSelectedPin() const
{
	return HasGraphNonReadOnly() ? (GraphHandler->GetSelectedPin() != nullptr) : false;
}

bool FBAInputProcessor::HasSingleNodeSelected() const
{
	return HasGraphNonReadOnly() ? (GraphHandler->GetSelectedNode() != nullptr) : false;
}

bool FBAInputProcessor::HasMultipleNodesSelected() const
{
	return HasGraphNonReadOnly() ? (GraphHandler->GetSelectedNodes().Num() > 0) : false;
}

bool FBAInputProcessor::HasMultipleNodesSelectedInclComments() const
{
	return HasGraphNonReadOnly() ? (GraphHandler->GetSelectedNodes(true).Num() > 0) : false;
}

bool FBAInputProcessor::CanRenameSelectedNode() const
{
	if (HasSingleNodeSelected())
	{
		UEdGraphNode* SelectedNode = GraphHandler->GetSelectedNode();
		return SelectedNode->IsA(UK2Node_Variable::StaticClass()) ||
			SelectedNode->IsA(UK2Node_CallFunction::StaticClass()) ||
			SelectedNode->IsA(UK2Node_MacroInstance::StaticClass());
	}

	return false;
}

bool FBAInputProcessor::CanToggleNodes() const
{
	return HasMultipleNodesSelected() && GraphHandler->GetBlueprint() != nullptr;
}

bool FBAInputProcessor::HasWorkflowModes() const
{
	const TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow)
	{
		return false;
	}

	TArray<TSharedPtr<SWidget>> ModeWidgets;
	FBAUtils::GetChildWidgets(ActiveWindow, "SModeWidget", ModeWidgets);

	TArray<TSharedPtr<SWidget>> AssetShortcutWidgets;
	FBAUtils::GetChildWidgets(ActiveWindow, "SAssetShortcut", AssetShortcutWidgets);

	return ModeWidgets.Num() > 0 || AssetShortcutWidgets.Num() > 0;
}

bool FBAInputProcessor::CanOpenEditDetailsMenu() const
{
	return CanExecuteActions() && SEditDetailsMenu::CanOpenMenu();
}

bool FBAInputProcessor::CanExecuteCommand(TSharedRef<const FUICommandInfo> Command) const
{
	for (TSharedPtr<FUICommandList> CommandList : CommandLists)
	{
		if (const FUIAction* Action = CommandList->GetActionForCommand(Command))
		{
			return Action->CanExecute();
		}
	}

	return false;
}

bool FBAInputProcessor::TryExecuteCommand(TSharedRef<const FUICommandInfo> Command)
{
	for (TSharedPtr<FUICommandList> CommandList : CommandLists)
	{
		if (const FUIAction* Action = CommandList->GetActionForCommand(Command))
		{
			if (Action->CanExecute())
			{
				return Action->Execute();
			}
		}
	}

	return false;
}

bool FBAInputProcessor::IsGameRunningOrCompiling() const
{
	return GEditor->bIsSimulatingInEditor || GEditor->PlayWorld != nullptr || FBAUtils::IsCompilingCode();
}
