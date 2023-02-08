// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IDetailCustomization.h"

#include "BlueprintAssistSettings.generated.h"

UENUM()
enum class EBANodeFormattingStyle : uint8
{
	Expanded UMETA(DisplayName = "Expanded"),
	Compact UMETA(DisplayName = "Compact"),
};

UENUM()
enum class EBAParameterFormattingStyle : uint8
{
	Helixing UMETA(DisplayName = "Helixing"),
	LeftSide UMETA(DisplayName = "Left-side"),
};

UENUM()
enum class EBAWiringStyle : uint8
{
	AlwaysMerge UMETA(DisplayName = "Always Merge"),
	MergeWhenNear UMETA(DisplayName = "Merge When Near"),
	SingleWire UMETA(DisplayName = "Single Wire"),
};

UENUM()
enum class EBAAutoFormatting : uint8
{
	Never UMETA(DisplayName = "Never"),
	FormatAllConnected UMETA(DisplayName = "Format all connected nodes"),
	FormatSingleConnected UMETA(DisplayName = "Format relative to a connected node"),
};

UENUM()
enum class EBAFormatAllStyle : uint8
{
	Simple UMETA(DisplayName = "Simple (single column)"),
	Smart UMETA(DisplayName = "Smart (create columns from node position)"),
	NodeType UMETA(DisplayName = "Node Type (columns by node type)"),
};

UENUM()
enum class EBAFormatterType : uint8
{
	Blueprint UMETA(DisplayName = "Blueprint"),
	BehaviorTree UMETA(DisplayName = "BehaviorTree"),
	Simple UMETA(DisplayName = "Simple formatter"),
};

UENUM()
enum class EBAAutoZoomToNode : uint8
{
	Never UMETA(DisplayName = "Never"),
	Always UMETA(DisplayName = "Always"),
	Outside_Viewport UMETA(DisplayName = "Outside viewport"),
};

UENUM()
enum class EBAFunctionAccessSpecifier : uint8
{
	Public UMETA(DisplayName = "Public"),
	Protected UMETA(DisplayName = "Protected"),
	Private UMETA(DisplayName = "Private"),
};

USTRUCT()
struct FBAFormatterSettings
{
	GENERATED_BODY()

	FBAFormatterSettings() = default;

	FBAFormatterSettings(FVector2D InPadding, EBAAutoFormatting InAutoFormatting, EEdGraphPinDirection InFormatterDirection, TArray<FName> InRootNodes = TArray<FName>())
		: Padding(InPadding)
		, AutoFormatting(InAutoFormatting)
		, FormatterDirection(InFormatterDirection)
		, RootNodes(InRootNodes)
	{
	}

	/* Formatter to use */
	UPROPERTY(EditAnywhere, config, Category=FormatterSettings)
	EBAFormatterType FormatterType = EBAFormatterType::Simple;

	/* Padding used when formatting nodes */
	UPROPERTY(EditAnywhere, config, Category=FormatterSettings)
	FVector2D Padding = FVector2D(100, 100);

	/* Auto formatting method */
	UPROPERTY(EditAnywhere, config, Category=FormatterSettings)
	EBAAutoFormatting AutoFormatting = EBAAutoFormatting::FormatAllConnected;

	UPROPERTY(EditAnywhere, config, Category=FormatterSettings)
	TEnumAsByte<EEdGraphPinDirection> FormatterDirection = EGPD_Output;

	UPROPERTY(EditAnywhere, config, Category=FormatterSettings)
	TArray<FName> RootNodes;

	UPROPERTY(EditAnywhere, config, Category=FormatterSettings)
	FName ExecPinName;

	FString ToString() const
	{
		return FString::Printf(TEXT("FormatterType %d | ExecPinName %s"), FormatterType, *ExecPinName.ToString());
	}

	EBAAutoFormatting GetAutoFormatting() const;
};

UCLASS(config = EditorPerProjectUserSettings)
class BLUEPRINTASSIST_API UBASettings final : public UObject
{
	GENERATED_BODY()

public:
	UBASettings(const FObjectInitializer& ObjectInitializer);

	////////////////////////////////////////////////////////////
	// General
	////////////////////////////////////////////////////////////

	/* Add the BlueprintAssist widget to the toolbar */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bAddToolbarWidget;

	/* Highlight the currently selected pin with this color */
	UPROPERTY(EditAnywhere, config, Category = General)
	FLinearColor PinHighlightColor;

	/* Highlight the text for the currently selected pin with this color */
	UPROPERTY(EditAnywhere, config, Category = General)
	FLinearColor PinTextHighlightColor;

	/* Sets the 'Comment Bubble Pinned' bool for all nodes on the graph (Auto Size Comment plugin handles this value for comments) */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bEnableGlobalCommentBubblePinned;

	/* The global 'Comment Bubble Pinned' value */
	UPROPERTY(EditAnywhere, config, Category = General, meta = (EditCondition = "bEnableGlobalCommentBubblePinned"))
	bool bGlobalCommentBubblePinnedValue;

	/* Improves the default wiring behavior for new nodes */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bBetterWiringForNewNodes;

	/* Automatically add parent nodes to event nodes */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bAutoAddParentNode;

	/* Automatically rename Function getters and setters when the Function is renamed */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bAutoRenameGettersAndSetters;

	/* Merge the generate getter and setter into one button */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bMergeGenerateGetterAndSetterButton;

	/* Distance the viewport moves when running the Shift Camera command. Scaled by zoom distance. */
	UPROPERTY(EditAnywhere, config, Category = General)
	float ShiftCameraDistance;

	/* Enable more slower but more accurate node size caching */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bSlowButAccurateSizeCaching;

	/* Save the node size cache to a file (located in the the plugin folder) */
	UPROPERTY(EditAnywhere, config, Category = General)
	bool bSaveBlueprintAssistCacheToFile;

	/* Determines if we should auto zoom to a newly created node */
	UPROPERTY(EditAnywhere, config, Category = General)
	EBAAutoZoomToNode AutoZoomToNodeBehavior = EBAAutoZoomToNode::Outside_Viewport;

	////////////////////////////////////////////////////////////
	// Formatting options
	////////////////////////////////////////////////////////////

	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bGloballyDisableAutoFormatting; 

	/* Determines how execution nodes are positioned */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	EBANodeFormattingStyle FormattingStyle;

	/* Determines how parameters are positioned */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	EBAParameterFormattingStyle ParameterStyle;

	/* Determines how execution wires are created */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	EBAWiringStyle ExecutionWiringStyle;

	/* Determines how parameter wires are created */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	EBAWiringStyle ParameterWiringStyle;

	/* Faster formatting will only format chains of nodes have been moved or had connections changed. Greatly increases speed of the format all command. */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bEnableFasterFormatting;

	/* Reuse knot nodes instead of creating new ones every time */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bUseKnotNodePool;

	/* Whether to use HelixingHeightMax and SingleNodeMaxHeight */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bLimitHelixingHeight;

	/* Helixing is disabled if the total height of the parameter nodes is larger than this value */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions, meta = (EditCondition = "bLimitHelixingHeight"))
	float HelixingHeightMax;

	/* Helixing is disabled if a single node is taller than this value */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions, meta = (EditCondition = "bLimitHelixingHeight"))
	float SingleNodeMaxHeight;

	/* Cache node sizes of any newly detected nodes. Checks upon opening a blueprint or when a new node is added to the graph. */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bDetectNewNodesAndCacheNodeSizes;

	/* Refresh node sizes before formatting */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bRefreshNodeSizeBeforeFormatting;

	/* Create knot nodes */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bCreateKnotNodes;

	/* Add spacing to nodes so they are always in front of their input parameters */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bExpandNodesAheadOfParameters;

	/* Add spacing to nodes which have many connections, fixing hard to read wires */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bExpandNodesByHeight;

	/* Add spacing to parameter nodes which have many connections, fixing hard to read wires */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bExpandParametersByHeight;

	/* Snap nodes to grid (in the x-axis) after formatting */
	UPROPERTY(EditAnywhere, config, Category = FormattingOptions)
	bool bSnapToGrid;

	////////////////////////////////////////////////////////////
	/// Format All
	////////////////////////////////////////////////////////////

	/* Determines how nodes are positioned into columns when running formatting all nodes */
	UPROPERTY(EditAnywhere, config, Category = FormatAll)
	EBAFormatAllStyle FormatAllStyle;

	/* x values defines padding between columns, y value defines horizontal padding between node trees */
	UPROPERTY(EditAnywhere, config, Category = FormatAll)
	FVector2D FormatAllPadding;

	/* Call the format all function when a new event node is added to the graph */
	UPROPERTY(EditAnywhere, config, Category = FormatAll)
	bool bAutoPositionEventNodes;

	/* Call the format all function when ANY new node is added to the graph. Useful for when the 'UseColumnsForFormatAll' setting is on. */
	UPROPERTY(EditAnywhere, config, Category = FormatAll)
	bool bAlwaysFormatAll;

	////////////////////////////////////////////////////////////
	// Blueprint formatting
	////////////////////////////////////////////////////////////

	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	FBAFormatterSettings BlueprintFormatterSettings;

	/* Padding used between parameter nodes */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	FVector2D BlueprintParameterPadding;

	/* Blueprint formatting will be used for these types of graphs (you can see the type of a graph with the PrintGraphInfo command, default: unbound) */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = BlueprintFormatting)
	TArray<FName> UseBlueprintFormattingForTheseGraphs;

	/* When formatting treat delegate pins as execution pins, recommended to turn this option off and use the 'CreateEvent' node */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	bool bTreatDelegatesAsExecutionPins;

	/* Center node execution branches (Default: center nodes with 3 or more branches) */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	bool bCenterBranches;

	/* Only center branches if we have this (or more) number of branches */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting, meta = (EditCondition = "bCenterBranches"))
	int NumRequiredBranches;

	/* Center parameters nodes with multiple links */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	bool bCenterBranchesForParameters;

	/* Only center parameters which have this many (or more) number of links */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting, meta = (EditCondition = "bCenterBranchesForParameters"))
	int NumRequiredBranchesForParameters;

	/* Vertical spacing from the last linked pin */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	float VerticalPinSpacing;

	/* Vertical spacing from the last linked pin for parameters */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	float ParameterVerticalPinSpacing;

	/* Spacing used between wire tracks */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	float BlueprintKnotTrackSpacing;

	/* The width between pins required for a knot node to be created */
	UPROPERTY(EditAnywhere, config, Category = BlueprintFormatting)
	float KnotNodeDistanceThreshold;

	////////////////////////////////////////////////////////////
	// Other Graphs
	////////////////////////////////////////////////////////////

	UPROPERTY(EditAnywhere, config, Category = OtherGraphs)
	TMap<FName, FBAFormatterSettings> NonBlueprintFormatterSettings;

	////////////////////////////////////////////////////////////
	// Comment Settings
	////////////////////////////////////////////////////////////

	/* Apply comment padding when formatting */
	UPROPERTY(EditAnywhere, config, Experimental, Category = CommentSettings)
	bool bApplyCommentPadding;

	/* Add knot nodes to comments after formatting */
	UPROPERTY(EditAnywhere, config, Category = CommentSettings)
	bool bAddKnotNodesToComments;

	/* Padding around the comment box. Make sure this is the same as in the AutoSizeComments setting */
	UPROPERTY(EditAnywhere, config, Category = CommentSettings)
	FVector2D CommentNodePadding;

	////////////////////////////////////////////////////////////
	// Notifications
	////////////////////////////////////////////////////////////

	/* Whether to show a notification while the graph is caching node sizes */
	UPROPERTY(EditAnywhere, config, Category = Notifications)
	bool bEnableCachingNodeSizeNotification;

	/* Number of pending caching nodes required to show notification */
	UPROPERTY(EditAnywhere, config, Category = Notifications, meta = (EditCondition = "bEnableCachingNodeSizeNotification"))
	int RequiredNumPendingSizeForNotification;

	////////////////////////////////////////////////////////////
	// Create Variable defaults
	////////////////////////////////////////////////////////////

	/* Enable Variable defaults */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults")
	bool bEnableVariableDefaults;

	/* Variable default Instance Editable */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	bool bDefaultVariableInstanceEditable;

	/* Variable default Blueprint Read Only */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	bool bDefaultVariableBlueprintReadOnly;

	/* Variable default Expose on Spawn */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	bool bDefaultVariableExposeOnSpawn;

	/* Variable default Private */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	bool bDefaultVariablePrivate;

	/* Variable default Expose to Cinematics */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	bool bDefaultVariableExposeToCinematics;

	/* Variable default name */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	FString DefaultVariableName;

	/* Variable default Tooltip */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	FText DefaultVariableTooltip;

	/* Variable default Category */
	UPROPERTY(EditAnywhere, config, Category = "New Variable Defaults", meta = (EditCondition = "bEnableVariableDefaults"))
	FText DefaultVariableCategory;

	////////////////////////////////////////////////////////////
	// Create function defaults
	////////////////////////////////////////////////////////////

	/* Enable Function defaults */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults")
	bool bEnableFunctionDefaults;

	/* Function default AccessSpecifier */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	EBAFunctionAccessSpecifier DefaultFunctionAccessSpecifier;

	/* Function default Pure */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	bool bDefaultFunctionPure;
	
	/* Function default Const */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	bool bDefaultFunctionConst;

	/* Function default Exec */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	bool bDefaultFunctionExec;

	/* Function default Tooltip */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	FText DefaultFunctionTooltip;

	/* Function default Keywords */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	FText DefaultFunctionKeywords;

	/* Function default Category */
	UPROPERTY(EditAnywhere, config, Category = "New Function Defaults", meta = (EditCondition = "bEnableFunctionDefaults"))
	FText DefaultFunctionCategory;

	////////////////////////////////////////////////////////////
	// Misc
	////////////////////////////////////////////////////////////

	/* Enable invisible knot nodes (re-open any open graphs) */
	UPROPERTY(EditAnywhere, config, Category = "Misc")
	bool bEnableInvisibleKnotNodes;

	/* Play compile sound on *successful* live compile (may need to restart editor) */
	UPROPERTY(EditAnywhere, config, Category = "Misc")
	bool bPlayLiveCompileSound;

	/* Shift dragging a node will move all connected nodes */
	UPROPERTY(EditAnywhere, config, Category = "Misc")
	bool bEnableShiftDraggingNodes;

	////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////

	/* Ignore this (setting for custom debugging) */
	UPROPERTY(EditAnywhere, config, Category = Debug)
	int bCustomDebug;

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
};

class FBASettingsDetails final : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
