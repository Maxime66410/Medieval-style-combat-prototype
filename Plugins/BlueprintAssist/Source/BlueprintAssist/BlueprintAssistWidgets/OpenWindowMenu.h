// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BAFilteredList.h"

class ISettingsSection;
class ISettingsCategory;
class ISettingsContainer;

struct FExecuteCommandItem
{
	TSharedPtr<FUICommandInfo> Command;

	FExecuteCommandItem() = default;

	FExecuteCommandItem(TSharedPtr<FUICommandInfo> InCommand)
		: Command(InCommand) { }

	FString ToString() const { return Command->GetCommandName().ToString(); }
};

struct FOpenSettingItem
{
	TSharedPtr<ISettingsContainer> Container;
	TSharedPtr<ISettingsCategory> Category;
	TSharedPtr<ISettingsSection> Section;

	FOpenSettingItem(TSharedPtr<ISettingsContainer> InContainer, TSharedPtr<ISettingsCategory> InCategory, TSharedPtr<ISettingsSection> InSection)
		: Container(InContainer)
		, Category(InCategory)
		, Section(InSection) { }

	FOpenSettingItem() = default;

	FString ToString() const;

	FString GetCategoryString() const;

	const FSlateBrush* GetIcon();
};

enum EBATabType
{
	BATabType_Setting,
	BATabType_Tab,
	BATabType_Command
};

struct FOpenTabItem
{
	FName TabName;
	FName TabIconStyle;
	FName TabDisplayName;
	TSharedPtr<FTabManager> AlternateTabManager;

	FSlateIcon Icon;

	FOpenTabItem() = default;

	FOpenTabItem(FName InTabName, FName InTabIcon, TSharedPtr<FTabManager> InAlternateTabManager);

	FOpenTabItem(FName InTabName, FName InTabIcon, FName InTabDisplayName = "", TSharedPtr<FTabManager> InAlternateTabManager = nullptr);

	FOpenTabItem(FName InTabName, const FSlateIcon& InIcon, FName InTabDisplayName = "", TSharedPtr<FTabManager> InAlternateTabManager = nullptr);
};

struct FOpenWindowItem final : IBAFilteredListItem
{
	FExecuteCommandItem ExecuteCommand;
	FOpenSettingItem Settings;
	FOpenTabItem TabInfo;
	EBATabType TabType;
	// TSharedPtr<FTabSpawnerEntry> TabSpawner;

	FOpenWindowItem(const FOpenSettingItem& InSettings);

	FOpenWindowItem(const FOpenTabItem& InTabInfo);

	FOpenWindowItem(TSharedPtr<FTabSpawnerEntry> InTabSpawner);

	FOpenWindowItem(TSharedPtr<FUICommandInfo> InCommandInfo);

	void OpenTab();

	virtual FString ToString() const override;

	virtual FString GetSearchText() const override;

	const FSlateBrush* GetIcon();
};

class BLUEPRINTASSIST_API SOpenWindowMenu final : public SCompoundWidget
{
	// @formatter:off
	SLATE_BEGIN_ARGS(SOpenWindowMenu) { }
	SLATE_END_ARGS()
	// @formatter:on

	static FVector2D GetWidgetSize() { return FVector2D(400, 300); }

	void Construct(const FArguments& InArgs);

	void InitListItems(TArray<TSharedPtr<FOpenWindowItem>>& Items);

	void AddOpenTabItems(TArray<TSharedPtr<FOpenWindowItem>>& Items);

	void AddOpenSettingsItems(TArray<TSharedPtr<FOpenWindowItem>>& Items);

	// void AddEditorUtilityWidgets(TArray<FOpenTabItem>& OutTabInfos);

	void AddCommandItems(TArray<TSharedPtr<FOpenWindowItem>>& Items);

	TSharedRef<ITableRow> CreateItemWidget(TSharedPtr<FOpenWindowItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const;

	void SelectItem(TSharedPtr<FOpenWindowItem> Item);
};
