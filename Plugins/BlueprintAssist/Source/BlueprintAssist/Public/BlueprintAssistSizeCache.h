// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SGraphPin.h"

#include "BlueprintAssistSizeCache.generated.h"

USTRUCT()
struct BLUEPRINTASSIST_API FBANodeData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FVector2D CachedNodeSize;

	UPROPERTY()
	TMap<FGuid, float> CachedPins;
};

USTRUCT()
struct BLUEPRINTASSIST_API FBACacheData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TMap<FGuid, FBANodeData> CachedNodes;

	void CleanupGraph(UEdGraph* Graph);
};

USTRUCT()
struct BLUEPRINTASSIST_API FBAGraphData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TMap<FGuid, FBACacheData> GraphCache;
};

USTRUCT()
struct BLUEPRINTASSIST_API FBAPackageData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TMap<FName, FBAGraphData> PackageCache;

	UPROPERTY()
	int CacheVersion = -1;
};

class BLUEPRINTASSIST_API FBASizeCache
{
public:
	static FBASizeCache& Get();
	static void TearDown();

	void Init();

	FBAPackageData& GetPackageData() { return PackageData; }

	void LoadCache();

	void SaveCache();

	void DeleteCache();

	void CleanupFiles();

	FBACacheData& GetGraphData(UEdGraph* Graph);

	FString GetCachePath();

private:
	bool bHasLoaded = false;

	FBAPackageData PackageData;
};
