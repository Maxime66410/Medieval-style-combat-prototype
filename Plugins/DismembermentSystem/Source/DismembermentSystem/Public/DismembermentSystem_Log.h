// © 2021, Brock Marsh. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 
 */

DISMEMBERMENTSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogDismembermentSystem, Log, All);

#define DISVerbose(Text, ...) UE_LOG(LogDismembermentSystem, Verbose, TEXT(Text), ##__VA_ARGS__);
#define DISLog(Text, ...) UE_LOG(LogDismembermentSystem, Log, TEXT(Text), ##__VA_ARGS__);
#define DISWarn(Text, ...) UE_LOG(LogDismembermentSystem, Warning, TEXT(Text), ##__VA_ARGS__);
#define DISError(Text, ...) UE_LOG(LogDismembermentSystem, Error, TEXT(Text), ##__VA_ARGS__);