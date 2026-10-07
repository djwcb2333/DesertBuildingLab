#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UDesertBuildingStyle;
class UPackage;

/** Editor-only, unsaved result. The caller saves NewPackages together with its new Style/Design. */
struct FDesertCollectedAssets
{
    TMap<UObject*, UObject*> Replacements;
    TArray<UPackage*> NewPackages;
    TArray<FSoftObjectPath> Assets;
    int32 Created = 0;
    int32 Reused = 0;
};

/** Preflight the complete dependency graph before creating any asset. Never overwrites a destination. */
bool DesertCollectStyleAssets(UDesertBuildingStyle* Source, const FString& Root,
    FDesertCollectedAssets& Out, FString& Error);

/** Only call for a new/local object that the caller owns; never call on a reused collected asset. */
void DesertRemapCollectedReferences(UObject* Target, const TMap<UObject*, UObject*>& Replacements);
