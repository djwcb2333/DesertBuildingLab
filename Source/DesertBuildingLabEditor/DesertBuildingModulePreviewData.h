#pragma once
#include "CoreMinimal.h"
#include "DesertBuilding.h"
#include "DesertBuildingModulePreviewData.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** 真正显示的网格/材质/姿态；是展示数据，不是放置合法性结果。 */
USTRUCT(BlueprintType)
struct FDesertModulePreviewPart
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Preview") TObjectPtr<UStaticMesh> Mesh = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Preview") FTransform Transform = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly, Category="Preview") TArray<TObjectPtr<UMaterialInterface>> Materials;
};

USTRUCT(BlueprintType)
struct FDesertModulePreviewData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Preview") TArray<FDesertModulePreviewPart> Parts;
    UPROPERTY(BlueprintReadOnly, Category="Preview") FBox Bounds = FBox(ForceInit);
    UPROPERTY(BlueprintReadOnly, Category="Preview") FString Description;
    UPROPERTY(BlueprintReadOnly, Category="Preview") bool bUsesRecipe = false;
    UPROPERTY(BlueprintReadOnly, Category="Preview") int32 ResolvedVariantIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="Preview") TObjectPtr<UStaticMesh> SelectedMesh = nullptr;
};

// 同一构造器同时供真实侧栏3D和只读native验证，绝不重建/修改Source本身。
DESERTBUILDINGLABEDITOR_API FDesertModulePreviewData DesertMakeModulePreviewData(
    const ADesertBuilding* Source, int32 Tool, EDesertStairLayout Layout, int32 VariantIndex,
    int32 Facing, FIntVector ContextCell);
DESERTBUILDINGLABEDITOR_API const TArray<EDesertStairLayout>& DesertGetAuthoringStairLayouts();
