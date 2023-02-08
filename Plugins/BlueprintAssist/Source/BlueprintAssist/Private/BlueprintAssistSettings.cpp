// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistSettings.h"

#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistModule.h"
#include "BlueprintAssistSizeCache.h"
#include "BlueprintAssistTabHandler.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "BlueprintAssist/GraphFormatters/BehaviorTreeGraphFormatter.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Input/SButton.h"

EBAAutoFormatting FBAFormatterSettings::GetAutoFormatting() const
{
	return GetDefault<UBASettings>()->bGloballyDisableAutoFormatting ? EBAAutoFormatting::Never : AutoFormatting; 
}

UBASettings::UBASettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UseBlueprintFormattingForTheseGraphs =
	{
		"EdGraph",
		"GameplayAbilityGraph",
		"AnimationTransitionGraph",
	};

	ShiftCameraDistance = 400.0f;

	bSaveBlueprintAssistCacheToFile = true;

	bAddToolbarWidget = true;

	PinHighlightColor = FLinearColor(0.2f, 0.2f, 0.2f);
	PinTextHighlightColor = FLinearColor(0.728f, 0.364f, 0.003f);

	// ------------------- //
	// Format all settings //
	// ------------------- //

	FormatAllStyle = EBAFormatAllStyle::Simple;
	bAutoPositionEventNodes = false;
	bAlwaysFormatAll = false;
	FormatAllPadding = FVector2D(800, 250);

	ExecutionWiringStyle = EBAWiringStyle::AlwaysMerge;
	ParameterWiringStyle = EBAWiringStyle::AlwaysMerge;

	bGloballyDisableAutoFormatting = false;
	FormattingStyle = EBANodeFormattingStyle::Expanded;
	ParameterStyle = EBAParameterFormattingStyle::Helixing;

	BlueprintParameterPadding = FVector2D(40, 25);
	BlueprintKnotTrackSpacing = 26.f;
	VerticalPinSpacing = 26.f;
	ParameterVerticalPinSpacing = 26.f;

	bLimitHelixingHeight = true;
	HelixingHeightMax = 500;
	SingleNodeMaxHeight = 300;

	// ------------------ //
	// Formatter Settings //
	// ------------------ //

	BlueprintFormatterSettings.FormatterType = EBAFormatterType::Blueprint;
	BlueprintFormatterSettings.Padding = FVector2D(100, 100); 
	BlueprintFormatterSettings.AutoFormatting = EBAAutoFormatting::FormatAllConnected; 
	BlueprintFormatterSettings.FormatterDirection = EGPD_Output;
	BlueprintFormatterSettings.RootNodes = { "K2Node_Tunnel" }; 
	BlueprintFormatterSettings.ExecPinName = UEdGraphSchema_K2::PC_Exec; 
	BlueprintFormatterSettings.ExecPinName = "exec";

	FBAFormatterSettings BehaviorTreeSettings(
		FVector2D(100, 100),
		EBAAutoFormatting::FormatAllConnected,
		EGPD_Output,
		{ "BehaviorTreeGraphNode_Root" }
	);

	BehaviorTreeSettings.FormatterType = EBAFormatterType::BehaviorTree;

	NonBlueprintFormatterSettings.Add("BehaviorTreeGraph", BehaviorTreeSettings);

	FBAFormatterSettings SoundCueSettings(
		FVector2D(200, 100),
		EBAAutoFormatting::Never,
		EGPD_Input,
		{ "SoundCueGraphNode_Root" }
	);
	NonBlueprintFormatterSettings.Add("SoundCueGraph", SoundCueSettings);

	FBAFormatterSettings MaterialGraphSettings(
		FVector2D(80, 150),
		EBAAutoFormatting::FormatAllConnected,
		EGPD_Output,
		{ "MaterialGraphNode_Root" }
	);
	NonBlueprintFormatterSettings.Add("MaterialGraph", MaterialGraphSettings);

	FBAFormatterSettings AnimGraphSetting;
	AnimGraphSetting.Padding = 	FVector2D(80, 150); 
	AnimGraphSetting.AutoFormatting = EBAAutoFormatting::FormatAllConnected; 
	AnimGraphSetting.FormatterDirection = EGPD_Output;
	AnimGraphSetting.RootNodes = { "AnimGraphNode_Root", "AnimGraphNode_TransitionResult", "AnimGraphNode_StateResult" }; 
	AnimGraphSetting.ExecPinName = "PoseLink"; 
	NonBlueprintFormatterSettings.Add("AnimationGraph", AnimGraphSetting);
	NonBlueprintFormatterSettings.Add("AnimationStateGraph", AnimGraphSetting);

	FBAFormatterSettings NiagaraSettings;
	NiagaraSettings.Padding = FVector2D(80, 150);
	NiagaraSettings.AutoFormatting = EBAAutoFormatting::FormatAllConnected;
	NiagaraSettings.FormatterDirection = EGPD_Input;
	NiagaraSettings.RootNodes = { "NiagaraNodeInput" };
	NiagaraSettings.ExecPinName = "NiagaraParameterMap";
	NonBlueprintFormatterSettings.Add("NiagaraGraphEditor", NiagaraSettings);

	// TODO: Reenable support for control rig after fixing issues
	// FBAFormatterSettings ControlRigSettings(
	// 	FVector2D(80, 150),
	// 	EBAAutoFormatting::FormatAllConnected,
	// 	EGPD_Output
	// );
	// ControlRigSettings.ExecPinName = "ControlRigExecuteContext";
	// NonBlueprintFormatterSettings.Add("ControlRigGraph", ControlRigSettings);

	FBAFormatterSettings MetaSoundSettings(
		FVector2D(80, 150),
		EBAAutoFormatting::FormatAllConnected,
		EGPD_Output,
		{ "MetasoundEditorGraphInputNode" }
	);
	NonBlueprintFormatterSettings.Add("MetasoundEditorGraph", MetaSoundSettings);

	FBAFormatterSettings EnvironmentQuerySettings;
	EnvironmentQuerySettings.FormatterType = EBAFormatterType::BehaviorTree;
	EnvironmentQuerySettings.Padding = FVector2D(80, 150);
	EnvironmentQuerySettings.AutoFormatting = EBAAutoFormatting::FormatAllConnected;
	EnvironmentQuerySettings.FormatterDirection = EGPD_Output;
	EnvironmentQuerySettings.RootNodes = { "EnvironmentQueryGraphNode_Root" };
	NonBlueprintFormatterSettings.Add("EnvironmentQueryGraph", EnvironmentQuerySettings);

	bCreateKnotNodes = true;

	bBetterWiringForNewNodes = true;
	bAutoAddParentNode = true;

	bAutoRenameGettersAndSetters = true;
	bMergeGenerateGetterAndSetterButton = false;

	bEnableGlobalCommentBubblePinned = false;
	bGlobalCommentBubblePinnedValue = true;

	bDetectNewNodesAndCacheNodeSizes = false;
	bRefreshNodeSizeBeforeFormatting = true;

	bTreatDelegatesAsExecutionPins = false;

	bCenterBranches = false;
	NumRequiredBranches = 3;

	bCenterBranchesForParameters = false;
	NumRequiredBranchesForParameters = 2;

	bAddKnotNodesToComments = true;
	CommentNodePadding = FVector2D(30, 30);

	bEnableFasterFormatting = false;

	bUseKnotNodePool = false;

	bSlowButAccurateSizeCaching = false;

	bApplyCommentPadding = false;

	KnotNodeDistanceThreshold = 800.f;

	bExpandNodesAheadOfParameters = true;
	bExpandNodesByHeight = true;
	bExpandParametersByHeight = false;

	bSnapToGrid = false;

	bEnableCachingNodeSizeNotification = true;
	RequiredNumPendingSizeForNotification = 50;

	// ------------------------ //
	// Create variable defaults //
	// ------------------------ //

	bEnableVariableDefaults = false;
	bDefaultVariableInstanceEditable = false;
	bDefaultVariableBlueprintReadOnly = false;
	bDefaultVariableExposeOnSpawn = false;
	bDefaultVariablePrivate = false;
	bDefaultVariableExposeToCinematics = false;
	DefaultVariableName = TEXT("VarName");
	DefaultVariableTooltip = FText::FromString(TEXT(""));
	DefaultVariableCategory = FText::FromString(TEXT(""));

	// ----------------- //
	// Function defaults //
	// ----------------- //

	bEnableFunctionDefaults = false;
	DefaultFunctionAccessSpecifier = EBAFunctionAccessSpecifier::Public;
	bDefaultFunctionPure = false;
	bDefaultFunctionConst = false;
	bDefaultFunctionExec = false;
	DefaultFunctionTooltip = FText::FromString(TEXT(""));
	DefaultFunctionKeywords = FText::FromString(TEXT(""));
	DefaultFunctionCategory = FText::FromString(TEXT(""));

	// ------------------------ //
	// Misc                     //
	// ------------------------ //
	bPlayLiveCompileSound = false;

	bEnableInvisibleKnotNodes = false;

	bEnableShiftDraggingNodes = false;

	// ------------------------ //
	// Debug                    //
	// ------------------------ //
	bCustomDebug = -1;
}

void UBASettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	check(FBlueprintAssistModule::IsAvailable())

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	TSharedPtr<FBAGraphHandler> GraphHandler = FBATabHandler::Get().GetActiveGraphHandler();
	if (GraphHandler.IsValid())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bEnableGlobalCommentBubblePinned) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bGlobalCommentBubblePinnedValue))
		{
			GraphHandler->ApplyGlobalCommentBubblePinned();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, ParameterStyle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, FormattingStyle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, ParameterWiringStyle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, ExecutionWiringStyle)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bLimitHelixingHeight)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, HelixingHeightMax)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, SingleNodeMaxHeight)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, BlueprintKnotTrackSpacing)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, BlueprintParameterPadding)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, FormatAllPadding)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bTreatDelegatesAsExecutionPins)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bExpandNodesByHeight)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bExpandParametersByHeight)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UBASettings, bCreateKnotNodes)
			|| PropertyName == NAME_None) // if the name is none, this probably means we changed a property through the toolbar
			// TODO: maybe there's a way to change property externally while passing in correct info name
		{
			GraphHandler->ClearFormatters();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

TSharedRef<IDetailCustomization> FBASettingsDetails::MakeInstance()
{
	return MakeShareable(new FBASettingsDetails);
}

void FBASettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)
	{
		return;
	}

	//--------------------
	// General
	// -------------------

	IDetailCategoryBuilder& GeneralCategory = DetailBuilder.EditCategory("General");
	auto& SizeCache = FBASizeCache::Get();

	const FString CachePath = SizeCache.GetCachePath();

	const auto DeleteSizeCache = [&SizeCache]()
	{
		SizeCache.DeleteCache();
		return FReply::Handled();
	};

	GeneralCategory.AddCustomRow(FText::FromString("Size Cache"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString("Size Cache"))
			.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(5).AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString("Delete size cache file"))
				.ToolTipText(FText::FromString(FString::Printf(TEXT("Delete size cache file located at: %s"), *CachePath)))
				.OnClicked_Lambda(DeleteSizeCache)
			]
		];
}
