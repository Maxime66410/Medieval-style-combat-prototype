// Copyright 2021 fpwong. All Rights Reserved.

#include "BlueprintAssistSizeCache.h"

#include "BlueprintAssistGlobals.h"
#include "BlueprintAssistModule.h"
#include "BlueprintAssistSettings.h"
#include "AssetRegistry/Public/AssetRegistryModule.h"
#include "AssetRegistry/Public/AssetRegistryState.h"
#include "Core/Public/HAL/PlatformFilemanager.h"
#include "Core/Public/Misc/CoreDelegates.h"
#include "Core/Public/Misc/FileHelper.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EngineSettings/Classes/GeneralProjectSettings.h"
#include "JsonUtilities/Public/JsonObjectConverter.h"
#include "Misc/LazySingleton.h"
#include "Projects/Public/Interfaces/IPluginManager.h"

#define CACHE_VERSION 1

FBASizeCache& FBASizeCache::Get()
{
	return TLazySingleton<FBASizeCache>::Get();
}

void FBASizeCache::TearDown()
{
	TLazySingleton<FBASizeCache>::TearDown();
}

void FBASizeCache::Init()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.OnFilesLoaded().AddRaw(this, &FBASizeCache::LoadCache);

	FCoreDelegates::OnPreExit.AddRaw(this, &FBASizeCache::SaveCache);
}

void FBASizeCache::LoadCache()
{
	if (!GetDefault<UBASettings>()->bSaveBlueprintAssistCacheToFile)
	{
		return;
	}

	if (bHasLoaded)
	{
		return;
	}

	bHasLoaded = true;

	const auto CachePath = GetCachePath();

	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*CachePath))
	{
		FString FileData;
		FFileHelper::LoadFileToString(FileData, *CachePath);

		if (FJsonObjectConverter::JsonObjectStringToUStruct(FileData, &PackageData, 0, 0))
		{
			UE_LOG(LogBlueprintAssist, Log, TEXT("Loaded blueprint assist node size cache: %s"), *CachePath);
		}
		else
		{
			UE_LOG(LogBlueprintAssist, Log, TEXT("Failed to load node size cache: %s"), *CachePath);
		}
	}

	if (PackageData.CacheVersion != CACHE_VERSION)
	{
		// clear the cache if our version doesn't match
		PackageData.PackageCache.Empty();

		PackageData.CacheVersion = CACHE_VERSION;
	}

	CleanupFiles();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.OnFilesLoaded().RemoveAll(this);
}

void FBASizeCache::SaveCache()
{
	if (!GetDefault<UBASettings>()->bSaveBlueprintAssistCacheToFile)
	{
		return;
	}

	const auto CachePath = GetCachePath();

	// Write data to file
	FString JsonAsString;
	FJsonObjectConverter::UStructToJsonObjectString(PackageData, JsonAsString);
	FFileHelper::SaveStringToFile(JsonAsString, *CachePath);
	UE_LOG(LogBlueprintAssist, Log, TEXT("Saved node cache to %s"), *CachePath);
}

void FBASizeCache::DeleteCache()
{
	FString CachePath = GetCachePath();
	PackageData.PackageCache.Empty();

	if (FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*CachePath))
	{
		UE_LOG(LogBlueprintAssist, Log, TEXT("Deleted cache file at %s"), *CachePath);
	}
	else
	{
		UE_LOG(LogBlueprintAssist, Log, TEXT("Delete cache failed: Cache file does not exist or is read-only %s"), *CachePath);
	}
}

void FBASizeCache::CleanupFiles()
{
	// Get all assets
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Get package guids from assets
	TSet<FName> CurrentPackageNames;

#if BA_UE_VERSION_OR_LATER(5, 0)
	TArray<FAssetData> Assets;
	FARFilter Filter;
	AssetRegistry.GetAllAssets(Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		CurrentPackageNames.Add(Asset.PackageName);
	}
#else
	const auto& AssetDataMap = AssetRegistry.GetAssetRegistryState()->GetObjectPathToAssetDataMap();
	for (const TPair<FName, const FAssetData*>& AssetDataPair : AssetDataMap)
	{
		const FAssetData* AssetData = AssetDataPair.Value;
		CurrentPackageNames.Add(AssetData->PackageName);
	}
#endif
	// Remove missing files
	TArray<FName> OldPackageGuids;
	PackageData.PackageCache.GetKeys(OldPackageGuids);
	for (FName PackageGuid : OldPackageGuids)
	{
		if (!CurrentPackageNames.Contains(PackageGuid))
		{
			PackageData.PackageCache.Remove(PackageGuid);
		}
	}
}

FBACacheData& FBASizeCache::GetGraphData(UEdGraph* Graph)
{
	UPackage* Package = Graph->GetOutermost();

	FBAGraphData& CacheData = PackageData.PackageCache.FindOrAdd(Package->GetFName());

	return CacheData.GraphCache.FindOrAdd(Graph->GraphGuid);
}

FString FBASizeCache::GetCachePath()
{
	const FString PluginDir = IPluginManager::Get().FindPlugin("BlueprintAssist")->GetBaseDir();

	const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
	const FGuid& ProjectID = ProjectSettings->ProjectID;

	return PluginDir + "/NodeSizeCache/" + ProjectID.ToString() + ".json";
}

void FBACacheData::CleanupGraph(UEdGraph* Graph)
{
	if (Graph == nullptr)
	{
		UE_LOG(LogBlueprintAssist, Error, TEXT("Tried to cleanup null graph"));
		return;
	}

	TSet<FGuid> CurrentNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		// Collect all node guids from the graph
		CurrentNodes.Add(Node->NodeGuid);

		if (FBANodeData* FoundNode = CachedNodes.Find(Node->NodeGuid))
		{
			// Collect current pin guids
			TSet<FGuid> CurrentPins;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				CurrentPins.Add(Pin->PinId);
			}

			// Collect cached pin guids
			TArray<FGuid> CachedPinGuids;
			FoundNode->CachedPins.GetKeys(CachedPinGuids);

			// Cleanup missing guids
			for (FGuid PinGuid : CachedPinGuids)
			{
				if (!CurrentPins.Contains(PinGuid))
				{
					FoundNode->CachedPins.Remove(PinGuid);
				}
			}
		}
	}

	// Remove any missing guids from the cached nodes
	TArray<FGuid> CachedNodeGuids;
	CachedNodes.GetKeys(CachedNodeGuids);

	for (FGuid NodeGuid : CachedNodeGuids)
	{
		if (!CurrentNodes.Contains(NodeGuid))
		{
			CachedNodes.Remove(NodeGuid);
		}
	}
}
