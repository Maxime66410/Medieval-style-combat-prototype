// Fill out your copyright notice in the Description page of Project Settings.

#include "BABlueprintHandlerObject.h"

#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistSettings.h"
#include "BlueprintAssistUtils.h"
#include "K2Node_Knot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/MessageLog.h"

#if BA_UE_VERSION_OR_LATER(5, 0)
	#define BA_GET_ON_OBJECTS_REPLACED FCoreUObjectDelegates::OnObjectsReplaced
#else
	#define BA_GET_ON_OBJECTS_REPLACED GEditor->OnObjectsReplaced()
#endif

UBABlueprintHandlerObject::~UBABlueprintHandlerObject()
{
	if (BlueprintPtr.IsValid())
	{
		BlueprintPtr->OnChanged().RemoveAll(this);
		BlueprintPtr->OnCompiled().RemoveAll(this);
	}

	if (GEditor)
	{
		BA_GET_ON_OBJECTS_REPLACED.RemoveAll(this);
	}
}

void UBABlueprintHandlerObject::BindBlueprintChanged(UBlueprint* Blueprint)
{
	if (!Blueprint->IsValidLowLevelFast(false))
	{
		UE_LOG(LogBlueprintAssist, Error, TEXT("BAObject: Tried to bind to invalid blueprint"));
		return;
	}

	BlueprintPtr = TWeakObjectPtr<UBlueprint>(Blueprint);
	SetLastVariables(Blueprint);
	bProcessedChangesThisFrame = false;
	bActive = true;

	Blueprint->OnChanged().RemoveAll(this);
	Blueprint->OnChanged().AddUObject(this, &UBABlueprintHandlerObject::OnBlueprintChanged);

	Blueprint->OnCompiled().RemoveAll(this);
	Blueprint->OnCompiled().AddUObject(this, &UBABlueprintHandlerObject::OnBlueprintCompiled);

	if (GEditor)
	{
		BA_GET_ON_OBJECTS_REPLACED.RemoveAll(this);
		BA_GET_ON_OBJECTS_REPLACED.AddUObject(this, &UBABlueprintHandlerObject::OnObjectsReplaced);
	}
}

void UBABlueprintHandlerObject::UnbindBlueprintChanged(UBlueprint* Blueprint)
{
	LastVariables.Empty();
	bProcessedChangesThisFrame = false;
	bActive = false;

	if (BlueprintPtr.IsValid() && BlueprintPtr->IsValidLowLevelFast())
	{
		BlueprintPtr->OnChanged().RemoveAll(this);
		BlueprintPtr->OnCompiled().RemoveAll(this);
	}

	Blueprint->OnChanged().RemoveAll(this);
	Blueprint->OnCompiled().RemoveAll(this);
}

void UBABlueprintHandlerObject::SetLastVariables(UBlueprint* Blueprint)
{
	if (!Blueprint->IsValidLowLevelFast(false))
	{
		UE_LOG(LogBlueprintAssist, Error, TEXT("BAObject: Tried to update variables from an invalid blueprint"));
		return;
	}

	LastVariables = Blueprint->NewVariables;
}
// See UControlRigBlueprint::OnPostVariableChange
void UBABlueprintHandlerObject::OnBlueprintChanged(UBlueprint* Blueprint)
{
	// Blueprint should always be valid?
	if (!Blueprint->IsValidLowLevelFast(false))
	{
		UE_LOG(LogBlueprintAssist, Error, TEXT("BAObject: Invalid blueprint was changed, please report this on github"));
		return;
	}

	if (!BlueprintPtr.IsValid())
	{
		BlueprintPtr = Blueprint;
	}

	if (Blueprint != BlueprintPtr)
	{
		const FString OldBlueprintName = BlueprintPtr.IsValid() ? BlueprintPtr->GetName() : FString("nullptr");
		UE_LOG(LogBlueprintAssist, Warning, TEXT("BAObject: Blueprint was changed but it's the wrong blueprint? %s %s"), *Blueprint->GetName(), *OldBlueprintName);
		return;
	}

	if (!bActive)
	{
		return;
	}

	if (bProcessedChangesThisFrame)
	{
		return;
	}

	bProcessedChangesThisFrame = true;
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UBABlueprintHandlerObject::ResetProcessedChangesThisFrame));

	// This shouldn't happen!
	check(Blueprint->IsValidLowLevelFast(false));

	TMap<FGuid, int32> OldVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < LastVariables.Num(); VarIndex++)
	{
		OldVariablesByGuid.Add(LastVariables[VarIndex].VarGuid, VarIndex);
	}

	for (FBPVariableDescription& NewVariable : Blueprint->NewVariables)
	{
		if (!OldVariablesByGuid.Contains(NewVariable.VarGuid))
		{
			OnVariableAdded(Blueprint, NewVariable);
			continue;
		}

		const int32 OldVarIndex = OldVariablesByGuid.FindChecked(NewVariable.VarGuid);
		const FBPVariableDescription& OldVariable = LastVariables[OldVarIndex];

		// Make set instance editable to true when you set expose on spawn to true
		if (FBAUtils::HasMetaDataChanged(OldVariable, NewVariable, FBlueprintMetadata::MD_ExposeOnSpawn))
		{
			const bool bNotInstanceEditable = (NewVariable.PropertyFlags & CPF_DisableEditOnInstance) == CPF_DisableEditOnInstance;
			if (bNotInstanceEditable && NewVariable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) && NewVariable.GetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) == TEXT("true"))
			{
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, NewVariable.VarName, false);
			}
		}

		// Check if a variable has been renamed (use string cause names are not case-sensitive!)
		if (!OldVariable.VarName.ToString().Equals(NewVariable.VarName.ToString()))
		{
			OnVariableRenamed(Blueprint, OldVariable, NewVariable);
		}

		// Check if a variable type has changed
		if (OldVariable.VarType != NewVariable.VarType)
		{
			OnVariableTypeChanged(Blueprint, OldVariable, NewVariable);
		}
	}

	SetLastVariables(Blueprint);
}

void UBABlueprintHandlerObject::ResetProcessedChangesThisFrame()
{
	bProcessedChangesThisFrame = false;
}

void UBABlueprintHandlerObject::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	if (BlueprintPtr.IsValid())
	{
		if (UObject* Replacement = ReplacementMap.FindRef(BlueprintPtr.Get()))
		{
			UE_LOG(LogBlueprintAssist, Warning, TEXT("BAObject: Blueprint was replaced with %s"), *Replacement->GetName());
			UnbindBlueprintChanged(BlueprintPtr.Get());

			if (UBlueprint* NewBlueprint = Cast<UBlueprint>(Replacement))
			{
				BindBlueprintChanged(NewBlueprint);
			}
			else
			{
				BlueprintPtr = nullptr;
			}
		}
	}
}

void UBABlueprintHandlerObject::OnVariableAdded(UBlueprint* Blueprint, FBPVariableDescription& Variable)
{
	const UBASettings* BASettings = GetDefault<UBASettings>();
	if (BASettings->bEnableVariableDefaults)
	{
		if (BASettings->bDefaultVariableInstanceEditable)
		{
			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, Variable.VarName, false);
		}

		if (BASettings->bDefaultVariableBlueprintReadOnly)
		{
			FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, Variable.VarName, true);
		}

		if (BASettings->bDefaultVariableExposeOnSpawn)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
		}

		if (BASettings->bDefaultVariablePrivate)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, FBlueprintMetadata::MD_Private, TEXT("true"));
		}

		if (BASettings->bDefaultVariableExposeToCinematics)
		{
			FBlueprintEditorUtils::SetInterpFlag(Blueprint, Variable.VarName, true);
		}

		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Variable.VarName, nullptr, BASettings->DefaultVariableCategory);

		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, FBlueprintMetadata::MD_Tooltip, BASettings->DefaultVariableTooltip.ToString());
	}
}

void UBABlueprintHandlerObject::OnVariableRenamed(UBlueprint* Blueprint, const FBPVariableDescription& OldVariable, FBPVariableDescription& NewVariable)
{
	if (GetDefault<UBASettings>()->bAutoRenameGettersAndSetters)
	{
		RenameGettersAndSetters(Blueprint, OldVariable, NewVariable);
	}
}

void UBABlueprintHandlerObject::OnVariableTypeChanged(UBlueprint* Blueprint, const FBPVariableDescription& OldVariable, FBPVariableDescription& NewVariable)
{
	// Boolean variables may need to be renamed as well!
	if (GetDefault<UBASettings>()->bAutoRenameGettersAndSetters)
	{
		RenameGettersAndSetters(Blueprint, OldVariable, NewVariable);
	}
}

void UBABlueprintHandlerObject::RenameGettersAndSetters(UBlueprint* Blueprint, const FBPVariableDescription& OldVariable, FBPVariableDescription& NewVariable)
{
	const FString OldVariableName = FBAUtils::GetVariableName(OldVariable.VarName.ToString(), OldVariable.VarType.PinCategory, OldVariable.VarType.ContainerType);
	const FString NewVariableName = FBAUtils::GetVariableName(NewVariable.VarName.ToString(), NewVariable.VarType.PinCategory, NewVariable.VarType.ContainerType);

	// Do nothing if our names didn't change
	if (OldVariableName == NewVariableName)
	{
		return;
	}

	const FString GetterName = FString::Printf(TEXT("Get%s"), *OldVariableName);
	const FString SetterName = FString::Printf(TEXT("Set%s"), *OldVariableName);

	const FString NewGetterName = FString::Printf(TEXT("Get%s"), *NewVariableName);
	const FString NewSetterName = FString::Printf(TEXT("Set%s"), *NewVariableName);

	for (UEdGraph* VariableGraph : Blueprint->FunctionGraphs)
	{
		if (VariableGraph->GetName() == GetterName)
		{
			FBlueprintEditorUtils::RenameGraph(VariableGraph, NewGetterName);
		}
		else if (VariableGraph->GetName() == SetterName)
		{
			FBlueprintEditorUtils::RenameGraph(VariableGraph, NewSetterName);
		}
	}
}

void UBABlueprintHandlerObject::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint))
	{
		return;
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		DetectGraphIssues(Graph);
	}
}

void UBABlueprintHandlerObject::DetectGraphIssues(UEdGraph* Graph)
{
	if (!IsValid(Graph))
	{
		return;
	}

	struct FLocal
	{
		static void FocusNode(TWeakObjectPtr<UK2Node_Knot> Node)
		{
			if (Node.IsValid())
			{
				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node.Get(), false);
			}
		}
	};

	FMessageLog BlueprintAssistLog("BlueprintAssist");

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		// Detect bad knot nodes
		if (UK2Node_Knot* KnotNode = Cast<UK2Node_Knot>(Node))
		{
			// Detect empty knot nodes to be deleted
			if (FBAUtils::GetLinkedPins(KnotNode).Num() == 0)
			{
				TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Info);
				const FText MessageText = FText::FromString(FString::Printf(TEXT("Unlinked reroute node %s"), *KnotNode->NodeGuid.ToString()));
				Message->AddToken(FTextToken::Create(MessageText));
				Message->AddToken(FActionToken::Create(
					FText::FromString("GoTo"),
					FText::FromString("Go to node"),
					FOnActionTokenExecuted::CreateStatic(&FLocal::FocusNode, TWeakObjectPtr<UK2Node_Knot>(KnotNode))));

				BlueprintAssistLog.AddMessage(Message);
			}
			else
			{
				bool bOpenMessageLog = false;

				// Detect badly linked exec knot nodes
				for (UEdGraphPin* Pin : FBAUtils::GetLinkedPins(KnotNode, EGPD_Output).FilterByPredicate(FBAUtils::IsExecPin))
				{
					if (Pin->LinkedTo.Num() > 1)
					{
						TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error);
						const FText MessageText = FText::FromString(FString::Printf(TEXT("Badly linked reroute node (manually delete and remake this node) %s"), *KnotNode->NodeGuid.ToString()));
						Message->AddToken(FTextToken::Create(MessageText));
						Message->AddToken(FActionToken::Create(
							FText::FromString("GoTo"),
							FText::FromString("Go to node"),
							FOnActionTokenExecuted::CreateStatic(&FLocal::FocusNode, TWeakObjectPtr<UK2Node_Knot>(KnotNode))));

						BlueprintAssistLog.AddMessage(Message);

						bOpenMessageLog = true;
					}
				}

				if (bOpenMessageLog)
				{
					BlueprintAssistLog.Open();
				}
			}
		}
	}
}
