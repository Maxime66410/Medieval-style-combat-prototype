// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistGraphExtender.h"

#include "BlueprintAssistGraphCommands.h"
#include "BlueprintAssistUtils.h"
#include "BlueprintEditor.h"
#include "GraphEditorModule.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"

void FBAGraphExtender::ApplyExtender()
{
	FGraphEditorModule& GraphEditorModule = FModuleManager::GetModuleChecked<FGraphEditorModule>(TEXT("GraphEditor"));
	GraphEditorModule.GetAllGraphEditorContextMenuExtender().Add(FGraphEditorModule::FGraphEditorMenuExtender_SelectedNode::CreateStatic(&FBAGraphExtender::ExtendSelectedNode));
}

TSharedRef<FExtender> FBAGraphExtender::ExtendSelectedNode(const TSharedRef<FUICommandList> CommandList, const UEdGraph* Graph, const UEdGraphNode* Node, const UEdGraphPin* Pin, bool bIsEditable)
{
	TSharedRef<FExtender> Extender(new FExtender());

	struct FLocal
	{
		static void CallGenerateGetter(const UEdGraph* Graph, const UEdGraphNode* Node)
		{
			GenerateGetter(Graph, Node);
		}
		
		static void CallGenerateSetter(const UEdGraph* Graph, const UEdGraphNode* Node)
		{
			GenerateSetter(Graph, Node);
		}
		
		static void AddGenerateGetterSetter(FMenuBuilder& MenuBuilder)
		{
			if (GetDefault<UBASettings>()->bMergeGenerateGetterAndSetterButton)
			{
				MenuBuilder.AddMenuEntry(FBAGraphCommands::Get().GenerateGetterAndSetter);
			}
			else
			{
				MenuBuilder.AddMenuEntry(FBAGraphCommands::Get().GenerateGetter);
				MenuBuilder.AddMenuEntry(FBAGraphCommands::Get().GenerateSetter);
			}
		}
	};

	CommandList->MapAction(
		FBAGraphCommands::Get().GenerateGetter,
		FExecuteAction::CreateStatic(&FLocal::CallGenerateGetter, Graph, Node));

	CommandList->MapAction(
		FBAGraphCommands::Get().GenerateSetter,
		FExecuteAction::CreateStatic(&FLocal::CallGenerateSetter, Graph, Node));

	CommandList->MapAction(
		FBAGraphCommands::Get().GenerateGetterAndSetter,
		FExecuteAction::CreateStatic(&FBAGraphExtender::GenerateGetterAndSetter, Graph, Node));

	if (Node->IsA(UK2Node_VariableGet::StaticClass()))
	{
		Extender->AddMenuExtension(
			"EdGraphSchemaNodeActions",
			EExtensionHook::After,
			CommandList,
			FMenuExtensionDelegate::CreateStatic(&FLocal::AddGenerateGetterSetter));
	}

	return Extender;
}

bool FBAGraphExtender::GenerateGetter(const UEdGraph* Graph, const UEdGraphNode* Node)
{
	const UK2Node_VariableGet* SourceVariableGet = Cast<UK2Node_VariableGet>(Node);
	check(SourceVariableGet);

	const FBlueprintEditor* BPEditor = FBAUtils::GetBlueprintEditorForGraph(Graph);
	if (!BPEditor)
	{
		return false;
	}

	UBlueprint* BlueprintObj = BPEditor->GetBlueprintObj();

	const FEdGraphPinType& PinType = SourceVariableGet->GetPinAt(0)->PinType;
	const FString VariableName = FBAUtils::GetVariableName(SourceVariableGet->VariableReference.GetMemberName().ToString(), PinType.PinCategory, PinType.ContainerType);

	const FString FunctionName = FString::Printf(TEXT("Get%s"), *VariableName);

	// Do nothing if function already exists
	if (FindObject<UEdGraph>(BlueprintObj, *FunctionName))
	{
		const FText Message = FText::FromString(FString::Printf(TEXT("Getter '%s' already exists"), *FunctionName));
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 2.0f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FEditorStyle::GetBrush(TEXT("Icons.Warning"));
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GenerateGetter_BlueprintAssist", "Generate Getter"));

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(BlueprintObj, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BlueprintObj, NewGraph, true, nullptr);

	BlueprintObj->Modify();
	NewGraph->Modify();

	UK2Node_EditablePinBase* FunctionEntryNodePtr = FBlueprintEditorUtils::GetEntryNode(NewGraph);
	UK2Node_FunctionResult* NewResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(FunctionEntryNodePtr);
	NewResultNode->NodePosX = 256;
	NewResultNode->NodePosY = 0;

	UEdGraphPin* Pin = NewResultNode->CreateUserDefinedPin("ReturnValue", SourceVariableGet->GetPinAt(0)->PinType, EGPD_Input);

	// Create variable get
	UK2Node_VariableGet* NewVarGet = NewObject<UK2Node_VariableGet>(NewGraph);
	NewVarGet->VariableReference = SourceVariableGet->VariableReference;
	NewGraph->AddNode(NewVarGet, false, false);
	NewVarGet->NodePosX = NewResultNode->NodePosX;
	NewVarGet->NodePosY = 128;

	NewVarGet->CreateNewGuid();
	NewVarGet->PostPlacedNewNode();
	NewVarGet->AllocateDefaultPins();

	// Link to output
	FBAUtils::TryCreateConnection(Pin, NewVarGet->GetPinAt(0));

	// Set pure
	UFunction* Function = BlueprintObj->SkeletonGeneratedClass->FindFunctionByName(NewGraph->GetFName());
	Function->FunctionFlags ^= FUNC_BlueprintPure;
	
	UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(FunctionEntryNodePtr);
	EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() ^ FUNC_BlueprintPure);

	{
		const bool bCurDisableOrphanSaving = NewResultNode->bDisableOrphanPinSaving;
		NewResultNode->bDisableOrphanPinSaving = true;
		NewResultNode->ReconstructNode();
		NewResultNode->bDisableOrphanPinSaving = bCurDisableOrphanSaving;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->HandleParameterDefaultValueChanged(NewResultNode);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BlueprintObj);

	return true;
}

bool FBAGraphExtender::GenerateSetter(const UEdGraph* Graph, const UEdGraphNode* Node)
{
	const UK2Node_VariableGet* SourceVariableGet = Cast<UK2Node_VariableGet>(Node);
	check(SourceVariableGet);

	const FBlueprintEditor* BPEditor = FBAUtils::GetBlueprintEditorForGraph(Graph);
	if (!BPEditor)
	{
		return false;
	}

	UBlueprint* BlueprintObj = BPEditor->GetBlueprintObj();

	const FEdGraphPinType& PinType = SourceVariableGet->GetPinAt(0)->PinType;
	const FString VariableName = FBAUtils::GetVariableName(SourceVariableGet->VariableReference.GetMemberName().ToString(), PinType.PinCategory, PinType.ContainerType);

	const FString FunctionName = FString::Printf(TEXT("Set%s"), *VariableName);

	// Do nothing if function already exists
	if (FindObject<UEdGraph>(BlueprintObj, *FunctionName))
	{
		const FText Message = FText::FromString(FString::Printf(TEXT("Setter '%s' already exists"), *FunctionName));
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 2.0f;
		Info.bUseSuccessFailIcons = true;
		Info.Image = FEditorStyle::GetBrush(TEXT("Icons.Warning"));
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GenerateSetter_BlueprintAssist", "Generate Setter"));
	BlueprintObj->Modify();

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(BlueprintObj, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BlueprintObj, NewGraph, true, nullptr);

	NewGraph->Modify();

	UK2Node_EditablePinBase* FunctionEntryNodePtr = FBlueprintEditorUtils::GetEntryNode(NewGraph);

	// Create variable setter
	UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(NewGraph);
	SetNode->VariableReference = SourceVariableGet->VariableReference;
	NewGraph->AddNode(SetNode, false, false);
	SetNode->NodePosX = 256;
	SetNode->NodePosY = 16;

	SetNode->CreateNewGuid();
	SetNode->PostPlacedNewNode();
	SetNode->AllocateDefaultPins();

	// Create input pin getter
	UEdGraphPin* NewInputPin = FunctionEntryNodePtr->CreateUserDefinedPin("NewValue", SourceVariableGet->GetPinAt(0)->PinType, EGPD_Output);

	// Link nodes
	FBAUtils::TryCreateConnection(FunctionEntryNodePtr->Pins[0], FBAUtils::GetExecPins(SetNode, EGPD_Input)[0]);
	FBAUtils::TryCreateConnection(FBAUtils::GetParameterPins(SetNode, EGPD_Input)[0], NewInputPin);

	{
		const bool bCurDisableOrphanSaving = FunctionEntryNodePtr->bDisableOrphanPinSaving;
		FunctionEntryNodePtr->bDisableOrphanPinSaving = true;
		FunctionEntryNodePtr->ReconstructNode();
		FunctionEntryNodePtr->bDisableOrphanPinSaving = bCurDisableOrphanSaving;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->HandleParameterDefaultValueChanged(FunctionEntryNodePtr);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BlueprintObj);

	return true;
}

void FBAGraphExtender::GenerateGetterAndSetter(const UEdGraph* Graph, const UEdGraphNode* Node)
{
	FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GenerateGetterAndSetter_BlueprintAssist", "Generate Getter And Setter"));

	bool bSuccess = false;
	bSuccess |= GenerateGetter(Graph, Node);
	bSuccess |= GenerateSetter(Graph, Node);

	if (!bSuccess)
	{
		Transaction.Cancel();
	}
}
