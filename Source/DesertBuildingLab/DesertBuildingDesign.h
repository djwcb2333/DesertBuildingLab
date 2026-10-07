#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DesertBuilding.h"
#include "DesertBuildingDesign.generated.h"

class UDesertBuildingStyle;

/** 可复用建筑素材。布局、模块资源与版本保存在内容浏览器资产中。 */
UCLASS(BlueprintType)
class DESERTBUILDINGLAB_API UDesertBuildingDesign : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Building Design")
    TArray<FIntVector> Cells;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Building Design")
    TArray<FDesertRoomAppearance> RoomAppearanceOverrides;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Building Design")
    TArray<FDesertBlockPlacement> Blocks;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Building Design")
    TArray<FDesertRoofOpening> RoofOpenings;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Building Design")
    TObjectPtr<UDesertBuildingStyle> Style = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building Design")
    TSoftClassPtr<ADesertBuilding> BuildingClass;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building Design")
    int32 Revision = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building Design")
    int32 FormatVersion = 4;
    /** 0为旧版原位保存；1为以用户选择的目录为根的分类资源布局。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asset Organization")
    int32 AssetLayoutVersion = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asset Organization")
    FString AssetRoot;
    /** 此建筑收纳的持久美术资源；引擎内建资源与插件代码不复制。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asset Organization")
    TArray<FSoftObjectPath> CollectedAssets;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dimensions", meta=(Units="cm", ClampMin="100", ClampMax="1000"))
    float CellSize = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dimensions", meta=(Units="cm", ClampMin="100", ClampMax="1000"))
    float FloorHeight = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dimensions", meta=(Units="cm", ClampMin="10"))
    float FoundationDepth = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Generation")
    int32 Seed = 17;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Generation")
    bool bShowWoodBeams = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Generation")
    bool bEnableAwning = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Generation")
    bool bAutoSupportColumns = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Generation", meta=(Units="cm", ClampMin="100", ClampMax="50000"))
    float SupportTraceDistance = 10000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entrance")
    bool bUseEntranceOverride = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entrance", meta=(EditCondition="bUseEntranceOverride"))
    FIntVector EntranceOverrideCell = FIntVector::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entrance", meta=(EditCondition="bUseEntranceOverride", ClampMin="0", ClampMax="3"))
    int32 EntranceOverrideSide = 0;

    // 只复制制作输入；保存/传播由Editor模块负责，不在每次改草稿时写资源。
    void CaptureFrom(const ADesertBuilding* Source);
    // CDO使用false：只设置默认数据，不查询场景地面或创建几何。
    void ApplyTo(ADesertBuilding* Target, bool bRebuild = true) const;
};
