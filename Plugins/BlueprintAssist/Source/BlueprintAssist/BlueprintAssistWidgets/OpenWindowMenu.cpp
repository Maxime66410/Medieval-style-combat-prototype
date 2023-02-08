// Copyright 2021 fpwong. All Rights Reserved.

#include "OpenWindowMenu.h"

#include "BlueprintAssistCommands.h"
#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistInputProcessor.h"
#include "BlueprintAssistModule.h"
#include "BlueprintAssistUtils.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorTabs.h"
#include "ISettingsCategory.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "LevelEditor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

// TODO: Use workspace menu structure to read windows
// #include "WorkspaceMenuStructure.h"
// #include "WorkspaceMenuStructureModule.h"

FOpenTabItem::FOpenTabItem(FName InTabName, FName InTabIcon, TSharedPtr<FTabManager> InAlternateTabManager)
{
	TabName = InTabName;
	TabIconStyle = "EditorStyle";
	TabDisplayName = InTabName;
	AlternateTabManager = InAlternateTabManager;
	Icon = FSlateIcon("EditorStyle", (!InTabIcon.IsNone() ? InTabIcon : "Icons.Help"));
	if (!Icon.GetOptionalIcon())
	{
		Icon = FSlateIcon("EditorStyle", "Icons.Help");
	}
}

FOpenTabItem::FOpenTabItem(FName InTabName, FName InTabIcon, FName InTabDisplayName, TSharedPtr<FTabManager> InAlternateTabManager)
{
	TabName = InTabName;
	TabIconStyle = "EditorStyle";
	TabDisplayName = InTabDisplayName;
	AlternateTabManager = InAlternateTabManager;
	Icon = FSlateIcon("EditorStyle", (!InTabIcon.IsNone() ? InTabIcon : "Icons.Help"));
	if (!Icon.GetOptionalIcon())
	{
		Icon = FSlateIcon("EditorStyle", "Icons.Help");
	}
}

FOpenTabItem::FOpenTabItem(FName InTabName, const FSlateIcon& InTabIcon, FName InTabDisplayName, TSharedPtr<FTabManager> InAlternateTabManager)
{
	TabName = InTabName;
	Icon = (InTabIcon.GetOptionalIcon() ? InTabIcon : FSlateIcon("EditorStyle", "Icons.Help"));
	TabDisplayName = InTabDisplayName;
	AlternateTabManager = InAlternateTabManager;
}

FOpenWindowItem::FOpenWindowItem(const FOpenSettingItem& InSettings)
{
	TabType = BATabType_Setting;
	Settings = InSettings;
}

FOpenWindowItem::FOpenWindowItem(const FOpenTabItem& InTabInfo)
{
	TabType = BATabType_Tab;
	TabInfo = InTabInfo;
}

FOpenWindowItem::FOpenWindowItem(TSharedPtr<FTabSpawnerEntry> InTabSpawner)
{
	TabType = BATabType_Tab;
	// TabSpawner = InTabSpawner;
}

FOpenWindowItem::FOpenWindowItem(TSharedPtr<FUICommandInfo> InCommandInfo)
{
	TabType = BATabType_Command;
	ExecuteCommand = FExecuteCommandItem(InCommandInfo);
}

void FOpenWindowItem::OpenTab()
{
	switch (TabType)
	{
		case BATabType_Setting:
		{
			if (FModuleManager::Get().IsModuleLoaded(TEXT("Settings")))
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(
					Settings.Container->GetName(),
					Settings.Category->GetName(),
					Settings.Section->GetName());
			}

			break;
		}
		case BATabType_Tab:
		{
			// TSharedPtr<FWorkspaceItem> Parent = TabSpawner->GetParent();

			// UE_LOG(LogBlueprintAssist, Warning, TEXT("%s | %s"), *Parent->GetDisplayName().ToString(), *TabSpawner->GetDisplayName().ToString());
			// FGlobalTabmanager::Get()->TryInvokeTab(FName(*TabSpawner->GetDisplayName().ToString()));

			if (TabInfo.AlternateTabManager.IsValid())
			{
#if ENGINE_MINOR_VERSION >= 26 || ENGINE_MAJOR_VERSION >= 5
				TabInfo.AlternateTabManager->TryInvokeTab(TabInfo.TabName);
#else
				TabInfo.AlternateTabManager->InvokeTab(TabInfo.TabName);
#endif
			}
			else
			{
#if ENGINE_MINOR_VERSION >= 26 || ENGINE_MAJOR_VERSION >= 5
				FGlobalTabmanager::Get()->TryInvokeTab(TabInfo.TabName);
#else
				FGlobalTabmanager::Get()->InvokeTab(TabInfo.TabName);
#endif
			}
			break;
		}
		case BATabType_Command:
		{
			if (ExecuteCommand.Command.IsValid())
			{
				FBAInputProcessor::Get().TryExecuteCommand(ExecuteCommand.Command.ToSharedRef());
			}
			break;
		}
		default: ;
	}
}

FString FOpenWindowItem::ToString() const
{
	switch (TabType)
	{
		case BATabType_Setting:
			return Settings.ToString();
		case BATabType_Tab:
			return TabInfo.TabDisplayName.IsNone() ? TabInfo.TabName.ToString() : TabInfo.TabDisplayName.ToString();
		case BATabType_Command:
			return ExecuteCommand.ToString();
		default:
			return FString("");
	}
}

FString FOpenWindowItem::GetSearchText() const
{
	FString SearchText = ToString();
	if (TabType == BATabType_Setting)
	{
		SearchText += Settings.GetCategoryString();
	}

	return SearchText;
}

const FSlateBrush* FOpenWindowItem::GetIcon()
{
	switch (TabType)
	{
		case BATabType_Setting:
		{
			return Settings.GetIcon();
		}
		case BATabType_Tab:
		{
			return TabInfo.Icon.GetIcon();
		}
		case BATabType_Command:
		{
			return FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.BlueprintCore").GetIcon();
		}
		default:
			return nullptr;
	}
}

void SOpenWindowMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBAFilteredList<TSharedPtr<FOpenWindowItem>>)
		.InitListItems(this, &SOpenWindowMenu::InitListItems)
		.OnGenerateRow(this, &SOpenWindowMenu::CreateItemWidget)
		.OnSelectItem(this, &SOpenWindowMenu::SelectItem)
		.WidgetSize(GetWidgetSize())
		.MenuTitle(FString("Open Window"))
	];
}

void SOpenWindowMenu::InitListItems(TArray<TSharedPtr<FOpenWindowItem>>& Items)
{
	// TODO: Dynamically add tabs from  MenuStructure (needs pull request to access private FTabSpawnerEntry members
	// const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
	// TSharedRef<FWorkspaceItem> Root = MenuStructure.GetStructureRoot();

	AddOpenTabItems(Items);
	AddOpenSettingsItems(Items);
	AddCommandItems(Items);
}

void SOpenWindowMenu::AddOpenTabItems(TArray<TSharedPtr<FOpenWindowItem>>& Items)
{
	TArray<FOpenTabItem> TabInfos;

	// TODO: Fix adding editor utility tabs
	// AddEditorUtilityWidgets(TabInfos);

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	// Category General
	TabInfos.Add(FOpenTabItem("GameplayCueApp", "Profiler.EventGraph.ExpandHotPath16", "GameplayCue Editor"));

	// Category Find in Blueprints
	TabInfos.Add(FOpenTabItem("GlobalFindResults_01", "Kismet.Tabs.FindResults", "Find in Blueprints"));
	TabInfos.Add(FOpenTabItem("GlobalFindResults_02", "Kismet.Tabs.FindResults", "Find in Blueprints 2"));
	TabInfos.Add(FOpenTabItem("GlobalFindResults_03", "Kismet.Tabs.FindResults", "Find in Blueprints 3"));
	TabInfos.Add(FOpenTabItem("GlobalFindResults_04", "Kismet.Tabs.FindResults", "Find in Blueprints 4"));

	// Category Miscellaneous
	TabInfos.Add(FOpenTabItem("AssetAudit", "GenericEditor.Tabs.Properties", "Asset Audit"));
	TabInfos.Add(FOpenTabItem("ClassViewerApp", "ClassViewer.TabIcon", "Class Viewer"));
	TabInfos.Add(FOpenTabItem("DeviceManager", "DeviceDetails.TabIcon", "Device Manager"));
	TabInfos.Add(FOpenTabItem("DeviceProfileEditor", "DeviceDetails.Tabs.ProfileEditor", "Device Profiles"));
	TabInfos.Add(FOpenTabItem("FontAtlasVisualizer", FName(), "Font Atlas Visualizer"));
	TabInfos.Add(FOpenTabItem("HierarchicalProfiler", FSlateIcon("ControlRigEditorStyle", "ControlRig.RigUnit"), "Hierarchical Profiler"));
	TabInfos.Add(FOpenTabItem("MaterialAnalyzer", "MaterialEditor.ToggleMaterialStats.Tab", "Material Analyzer"));
	TabInfos.Add(FOpenTabItem("MergeActors", "MergeActors.TabIcon", "Merge Actors"));
	TabInfos.Add(FOpenTabItem("LevelEditorPixelInspector", FSlateIcon("PixelInspectorStyle", "PixelInspector.TabIcon"), "Pixel Inspector"));
	TabInfos.Add(FOpenTabItem("SessionFrontend", "SessionFrontEnd.TabIcon", "Session Frontend"));
	TabInfos.Add(FOpenTabItem("StructViewerApp", "ClassViewer.TabIcon", "Struct Viewer"));
	TabInfos.Add(FOpenTabItem("TextureAtlasVisualizer", FName(), "Texture Atlas Visualizer"));
	TabInfos.Add(FOpenTabItem("TimecodeProviderTab", "TimecodeProvider.TabIcon", "Timecode Provider", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("VisualLogger", FSlateIcon("LogVisualizerStyle", "LogVisualizerApp.TabIcon"), "Visual Logger"));
	TabInfos.Add(FOpenTabItem("WidgetReflector", FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "WidgetReflector.TabIcon"), "Widget Reflector"));

	// Category Log
	TabInfos.Add(FOpenTabItem("DeviceOutputLog", "Log.TabIcon", "Device Output Log"));
	TabInfos.Add(FOpenTabItem("OutputLog", "Log.TabIcon", "Output Log"));
	TabInfos.Add(FOpenTabItem("MessageLog", "MessageLog.TabIcon", "Message Log"));

	// Category Debug
	TabInfos.Add(FOpenTabItem("DebuggerApp", "BlueprintDebugger.TabIcon", "Blueprint Debugger"));
	TabInfos.Add(FOpenTabItem("CollisionAnalyzerApp", "CollisionAnalyzer.TabIcon", "Collision Analyzer"));
	TabInfos.Add(FOpenTabItem("DebugTools", "DebugTools.TabIcon", "Debug Tools"));
	TabInfos.Add(FOpenTabItem("VisualizerSpawnPoint", FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "Profiler.Tab"), "Profile Data Visualizer"));

	// Category Content Browser
	TabInfos.Add(FOpenTabItem("ContentBrowserTab1", "ContentBrowser.TabIcon", "Content Browser 1"));
	TabInfos.Add(FOpenTabItem("ContentBrowserTab2", "ContentBrowser.TabIcon", "Content Browser 2"));
	TabInfos.Add(FOpenTabItem("ContentBrowserTab3", "ContentBrowser.TabIcon", "Content Browser 3"));
	TabInfos.Add(FOpenTabItem("ContentBrowserTab4", "ContentBrowser.TabIcon", "Content Browser 4"));

	// Category Level Editor
	TabInfos.Add(FOpenTabItem("CameraShakePreviewer", "LevelViewport.ToggleActorPilotCameraView", "Camera Shake Previewer", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorEnvironmentLightingViewer", "EditorViewport.ReflectionOverrideMode", "Env. Light Mixer", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorHierarchicalLODOutliner", "LevelEditor.Tabs.HLOD", "Hierarchical LOD Outliner", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorLayerBrowser", "LevelEditor.Tabs.Layers", "Layers", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("WorldBrowserDetails", "LevelEditor.Tabs.WorldBrowserDetails", "Level Details", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("WorldBrowserHierarchy", "LevelEditor.Tabs.WorldBrowser", "Levels", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("PlacementBrowser", "LevelEditor.Tabs.PlacementBrowser", "Place Actors", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorStatsViewer", "LevelEditor.Tabs.StatsViewer", "Statistics", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorToolBar", "LevelEditor.Tabs.Toolbar", "Level Editor Toolbar", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("VariantManager_VariantManagerMain", "LevelEditor.GameSettings.Small", "Variant Manager", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("WorldBrowserComposition", "LevelEditor.Tabs.WorldBrowserComposition", "World Composition", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorSceneOutliner", "LevelEditor.Tabs.Outliner", "World Outliner", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("WorldSettingsTab", "LevelEditor.WorldProperties.Tab", "World Settings", LevelEditorTabManager));

	// Category Viewports
	TabInfos.Add(FOpenTabItem("LevelEditor", "LevelEditor.Tabs.Viewports", "Level Editor"));
	TabInfos.Add(FOpenTabItem("LevelEditorViewport", "LevelEditor.Tabs.Viewports", "Viewport 1", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorViewport_Clone1", "LevelEditor.Tabs.Viewports", "Viewport 2", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorViewport_Clone2", "LevelEditor.Tabs.Viewports", "Viewport 3", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorViewport_Clone3", "LevelEditor.Tabs.Viewports", "Viewport 4", LevelEditorTabManager));

	// Category Editor Modes
	TabInfos.Add(FOpenTabItem("EditorModeToolbar", "LevelEditor.Tabs.PlacementBrowser", "Active Mode Toolbar", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorToolBox", "LevelEditor.Tabs.Modes", "Active Mode Toolbox", LevelEditorTabManager));

	// Category Details
	TabInfos.Add(FOpenTabItem("LevelEditorSelectionDetails", "LevelEditor.Tabs.Details", "Details 1", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorSelectionDetails2", "LevelEditor.Tabs.Details", "Details 2", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorSelectionDetails3", "LevelEditor.Tabs.Details", "Details 3", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("LevelEditorSelectionDetails4", "LevelEditor.Tabs.Details", "Details 4", LevelEditorTabManager));

	// Category Cinematics
	TabInfos.Add(FOpenTabItem("SequenceRecorder", "SequenceRecorder.TabIcon", "Sequence Recorder"));
	TabInfos.Add(FOpenTabItem("Sequencer", FSlateIcon("LevelSequenceEditorStyle", "LevelSequenceEditor.Tabs.Sequencer"), "Sequencer", LevelEditorTabManager));

	// Unlisted tabs - search for '->RegisterNomadTabSpawner('
	TabInfos.Add(FOpenTabItem("ReferenceViewer", "Kismet.Tabs.FindResults", "Reference Viewer"));
	TabInfos.Add(FOpenTabItem("LevelEditorBuildAndSubmit", "LevelEditor.Tabs.PlacementBrowser", "Build and Submit", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("SequencerGraphEditor", "GenericCurveEditor.TabIcon", "Sequencer Curves", LevelEditorTabManager));
	TabInfos.Add(FOpenTabItem("UndoHistory", "UndoHistory.TabIcon", "Undo History"));
	TabInfos.Add(FOpenTabItem("PluginsEditor", FSlateIcon("PluginStyle", "Plugins.TabIcon"), "Plugins"));
	TabInfos.Add(FOpenTabItem("PluginCreator", FSlateIcon("PluginStyle", "Plugins.TabIcon"), "New Plugin"));
	TabInfos.Add(FOpenTabItem("ProjectLauncher", "Launcher.TabIcon", "Project Launcher"));
	TabInfos.Add(FOpenTabItem("MaterialAnalyzer", "MaterialEditor.ToggleMaterialStats.Tab", "Material Analyzer"));
	TabInfos.Add(FOpenTabItem("ActorPaletteTab1", FSlateIcon("ActorPaletteStyle", "ActorPalette.TabIcon"), "Actor Palette 1"));
	TabInfos.Add(FOpenTabItem("ActorPaletteTab2", FSlateIcon("ActorPaletteStyle", "ActorPalette.TabIcon"), "Actor Palette 2"));
	TabInfos.Add(FOpenTabItem("ActorPaletteTab3", FSlateIcon("ActorPaletteStyle", "ActorPalette.TabIcon"), "Actor Palette 3"));
	TabInfos.Add(FOpenTabItem("ActorPaletteTab4", FSlateIcon("ActorPaletteStyle", "ActorPalette.TabIcon"), "Actor Palette 4"));
	TabInfos.Add(FOpenTabItem("VisualLogger", FSlateIcon("LogVisualizerStyle", "LogVisualizerApp.TabIcon"), "VisualLogger"));
	TabInfos.Add(FOpenTabItem("SessionFrontEnd", "SessionFrontEnd.TabIcon", "Session Frontend"));
	TabInfos.Add(FOpenTabItem("ConfigEditor", "ConfigEditor.TabIcon", "Config Editor"));
	TabInfos.Add(FOpenTabItem("DebuggerApp", "BlueprintDebugger.TabIcon", "Blueprint Debugger"));

	// InsightsManager.cpp 
	TabInfos.Add(FOpenTabItem("StartPage", FSlateIcon("InsightsStyle", "StartPage.Icon.Small"), "Unreal Insights"));
	TabInfos.Add(FOpenTabItem("SessionInfo", FSlateIcon("InsightsStyle", "SessionInfo.Icon.Small"), "Session"));
	TabInfos.Add(FOpenTabItem("TimingProfiler", FSlateIcon("InsightsStyle", "TimingProfiler.Icon.Small"), "Timing Insights"));
	TabInfos.Add(FOpenTabItem("LoadingProfiler", FSlateIcon("InsightsStyle", "LoadingProfiler.Icon.Small"), "Asset Loading Insights"));
	TabInfos.Add(FOpenTabItem("NetworkingProfiler", FSlateIcon("InsightsStyle", "NetworkingProfiler.Icon.Small"), "Networking Insights"));
	TabInfos.Add(FOpenTabItem("MemoryProfiler", FSlateIcon("InsightsStyle", "MemoryProfiler.Icon.Small"), "Memory Insights"));
	// TabInfos.Add(FOpenTabItem("MessageLog", FSlateIcon("InsightsStyle", "MessageLog.Icon.Small"), "Message Log"));
	TabInfos.Add(FOpenTabItem("AutomationWindow", FSlateIcon("InsightsStyle", "AutomationWindow.Icon.Small"), "Automation"));

	// Add editor specific tabs
	if (IAssetEditorInstance* Editor = FBAUtils::GetEditorFromActiveTab())
	{
		TSharedPtr<FTabManager> EditorTabManager = Editor->GetAssociatedTabManager();

		FName EditorName = Editor->GetEditorName();
		if (EditorName == "BlueprintEditor")
		{
			// BlueprintEditorTabs.cpp
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::DetailsID, "Kismet.Tabs.BlueprintDefaults", "Details", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::DefaultEditorID, "Kismet.Tabs.BlueprintDefaults", "Class Defaults", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::DebugID, "DebugTools.TabIcon", "Debug", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::PaletteID, "Kismet.Tabs.Palette", "Palette", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::BookmarksID, "Kismet.Tabs.Bookmarks", "Bookmarks", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::CompilerResultsID, "Kismet.Tabs.CompilerResults", "Compiler Results", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::FindResultsID, "Kismet.Tabs.FindResults", "Find Results", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::ConstructionScriptEditorID, "Kismet.Tabs.Components", "Components", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::SCSViewportID, "LevelEditor.Tabs.Viewports", "Viewport", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::MyBlueprintID, "ClassIcon.BlueprintCore", "My Blueprint", EditorTabManager));
			TabInfos.Add(FOpenTabItem(FBlueprintEditorTabs::ReplaceNodeReferencesID, "ClassIcon.BlueprintCore", "Replace References", EditorTabManager));
		}
			// TODO: Finish the tabs for the other types of graphs
		else if (EditorName == "Behavior Tree")
		{
			// BehaviorTreeEditorTabs.cpp
			TabInfos.Add(FOpenTabItem("BehaviorTreeEditor_Properties", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("BehaviorTreeEditor_Search", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("BehaviorTreeEditor_Blackboard", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("BehaviorTreeEditor_BlackboardEditor", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("BehaviorTreeEditor_BlackboardProperties", FName(), EditorTabManager));
		}
		else if (EditorName == "AnimationBlueprintEditor")
		{
			// AnimationBlueprintEditor.cpp
			// TabInfos.Add(FOpenTabItem("DetailsTab", FName(), EditorTabManager)); <- unused
			TabInfos.Add(FOpenTabItem("SkeletonTreeView", "Persona.Tabs.SkeletonTree", EditorTabManager));
			TabInfos.Add(FOpenTabItem("Viewport", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("AdvancedPreviewTab", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("SequenceBrowser", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("AnimBlueprintPreviewEditor", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("AnimBlueprintParentPlayerEditor", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("SkeletonSlotNames", "Persona.Tabs.AnimSlotManager", EditorTabManager));
			TabInfos.Add(FOpenTabItem("AnimCurveViewerTab", "Persona.Tabs.SkeletonCurves", EditorTabManager));

			// PersonaModule - See TabFactories
		}
		else if (EditorName == "MaterialEditor")
		{
			// MaterialEditor.cpp
			TabInfos.Add(FOpenTabItem("MaterialEditor_Preview", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_GraphCanvas", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_MaterialProperties", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_Palette", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_Find", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_PreviewSettings", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_ParameterDefaults", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialEditor_CustomPrimitiveData", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("MaterialInstanceEditor_MaterialLayerProperties", FName(), EditorTabManager));
		}
		else if (EditorName == "Niagara")
		{
			// NiagaraScriptToolkit.cpp
			TabInfos.Add(FOpenTabItem("NiagaraEditor_NodeGraph", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("NiagaraEditor_ScriptDetails", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("NiagaraEditor_SelectedDetails", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("NiagaraEditor_Parameters", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("NiagaraEditor_Paramters2", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("NiagaraEditor_Stats", FName(), EditorTabManager));
			TabInfos.Add(FOpenTabItem("NiagaraEditor_MessageLog", FName(), EditorTabManager));
		}
	}

	// Skip items which are invalid
	for (const FOpenTabItem& Item : TabInfos)
	{
		if (Item.AlternateTabManager.IsValid())
		{
			if (!Item.AlternateTabManager->HasTabSpawner(Item.TabName) &&
				!Item.AlternateTabManager->FindExistingLiveTab(Item.TabName).IsValid())
			{
				continue;
			}
		}
		else
		{
			if (!FGlobalTabmanager::Get()->HasTabSpawner(Item.TabName) &&
				!FGlobalTabmanager::Get()->FindExistingLiveTab(Item.TabName).IsValid())
			{
				continue;
			}
		}
		// if (TabInfo.TabDisplayName.IsNone())
		// {
		// 	UE_LOG(LogBA, Warning, TEXT("No display name for tab %s"), *TabInfo.TabName.ToString());
		// }
		// if (FEditorStyle::GetBrush(TabInfo.TabIcon) == FEditorStyle::GetDefaultBrush())
		// {
		// 	UE_LOG(LogBlueprintAssist, Warning, TEXT("Invalid icon for tab %s"), *TabInfo.TabName.ToString());
		// }

		Items.Add(MakeShareable(new FOpenWindowItem(Item)));
	}
}

void SOpenWindowMenu::AddOpenSettingsItems(TArray<TSharedPtr<FOpenWindowItem>>& Items)
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("Settings")))
	{
		ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
		TArray<FName> AllContainers;
		SettingsModule.GetContainerNames(AllContainers);
		for (auto ContainerName : AllContainers)
		{
			TSharedPtr<ISettingsContainer> Container = SettingsModule.GetContainer(ContainerName);
			if (Container.IsValid())
			{
				TArray<TSharedPtr<ISettingsCategory>> AllCategories;
				Container->GetCategories(AllCategories);

				for (auto Category : AllCategories)
				{
					if (Category.IsValid())
					{
						TArray<TSharedPtr<ISettingsSection>> AllSections;
						Category->GetSections(AllSections);

						for (auto Section : AllSections)
						{
							TSharedPtr<FOpenWindowItem> OpenTabInfo = MakeShareable(new FOpenWindowItem(FOpenSettingItem(Container, Category, Section)));
							Items.Add(OpenTabInfo);
						}
					}
				}
			}
		}
	}
}

void SOpenWindowMenu::AddCommandItems(TArray<TSharedPtr<FOpenWindowItem>>& Items)
{
	const FBACommandsImpl& BACommands = FBACommands::Get();
	TArray<TSharedPtr<FUICommandInfo>> Commands = {
		BACommands.AddSymbolMenu,
		BACommands.EditDetailsMenu,
		BACommands.LinkPinMenu,
		BACommands.OpenContextMenu,
		BACommands.VariableSelectorMenu,
		BACommands.OpenBlueprintAssistHotkeySheet,
		BACommands.TabSwitcherMenu,
	};

	for (TSharedPtr<FUICommandInfo> Command : Commands)
	{
		if (FBAInputProcessor::Get().CanExecuteCommand(Command.ToSharedRef()))
		{
			Items.Add(MakeShareable(new FOpenWindowItem(Command)));
		}
	}
}

// TODO: Look into this function again, currently does not work
// void SOpenWindowMenu::AddEditorUtilityWidgets(TArray<FOpenTabItem>& OutTabInfos)
// {
// 	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
// 	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
// 	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
//
// 	for (TSharedRef<FWorkspaceItem> Category : MenuStructure.GetToolsCategory()->GetChildItems())
// 	{
// 		const bool bIsUtilityCategory = Category->GetDisplayName().EqualTo(FText::FromName("Editor Utility Widgets"));
// 		if (bIsUtilityCategory)
// 		{
// 			struct Local
// 			{
// 				TSharedPtr<FTabManager> LevelEditorTabManager;
//
// 				void AddSpawners(TArray<FOpenTabItem>& OutTabInfos, TSharedRef<FWorkspaceItem> Item)
// 				{
// 					if (auto SpawnerEntry = Item->AsSpawnerEntry())
// 					{
// 						FName TabName = FName(*Item->GetDisplayName().ToString());
// 						OutTabInfos.Add(FOpenTabItem(TabName, Item->GetIcon().GetStyleName(), TabName));
// 					}
// 					else
// 					{
// 						for (auto Child : Item->GetChildItems())
// 						{
// 							AddSpawners(OutTabInfos, Child);
// 						}
// 					}
// 				};
// 			};
//
// 			Local MyLocal;
// 			MyLocal.LevelEditorTabManager = LevelEditorTabManager;
// 			MyLocal.AddSpawners(OutTabInfos, Category);
// 			break;
// 		}
// 	}
// }

TSharedRef<ITableRow> SOpenWindowMenu::CreateItemWidget(TSharedPtr<FOpenWindowItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	const FSlateBrush* ContextIcon = Item->GetIcon();

	float VerticalPadding = 4.0f;

	TSharedRef<SVerticalBox> TextVBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->ToString()))
		];

	if (Item->TabType == BATabType_Setting)
	{
		VerticalPadding = 2.0f;

		TextVBox->AddSlot().AttachWidget(SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("ContentBrowser.AssetListViewClassFont"))
			.Text(FText::FromString(Item->Settings.GetCategoryString()))
		);
	}

	TSharedRef<SHorizontalBox> RowContents = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, ContextIcon != nullptr ? 5 : 0, 0)
		[
			SNew(SImage).Image(ContextIcon)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).HAlign(HAlign_Left).FillWidth(1)
		[
			TextVBox
		];

	return
		SNew(STableRow<TSharedPtr<FString>>, OwnerTable).Padding(FMargin(2.0f, VerticalPadding))
		[
			RowContents
		];
}

void SOpenWindowMenu::SelectItem(TSharedPtr<FOpenWindowItem> Item)
{
	Item->OpenTab();
}

/***********************/
/* FOpenSettingItem */
/***********************/
FString FOpenSettingItem::ToString() const
{
	return Section->GetDisplayName().ToString();
}

FString FOpenSettingItem::GetCategoryString() const
{
	return FString::Printf(TEXT("%s | %s"),
		*Container->GetDisplayName().ToString(),
		*Category->GetDisplayName().ToString());
}

const FSlateBrush* FOpenSettingItem::GetIcon()
{
	const FName& ContainerName = Container->GetName();

	if (ContainerName == "Project")
	{
		return FEditorStyle::GetBrush("ProjectSettings.TabIcon");
	}

	if (ContainerName == "Editor")
	{
		return FEditorStyle::GetBrush("EditorPreferences.TabIcon");
	}

	return nullptr;
}
