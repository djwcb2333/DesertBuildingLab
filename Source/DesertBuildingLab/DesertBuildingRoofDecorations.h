#pragma once
#include "CoreMinimal.h"
#include "DesertBuildingRoofDecorations.generated.h"

class UStaticMesh;

/** 纯装饰作者层；Cell为屋顶表面格，不参与Cells/Blocks占位。 */
USTRUCT(BlueprintType)
struct DESERTBUILDINGLAB_API FDesertRoofDecoration
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Decoration", meta=(ToolTip="表面格：Z=1是一层屋顶，Z=2是二层屋顶。"))
    FIntVector Cell = FIntVector(0,0,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Decoration", meta=(ClampMin="0",ClampMax="5"))
    int32 VariantIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Decoration", meta=(ClampMin="0",ClampMax="3"))
    int32 Facing = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Decoration")
    bool bEnabled = true;
};

/** 同一份实际网格与变换供生成、主窗口半透明预览、独立模块预览使用。 */
USTRUCT(BlueprintType)
struct DESERTBUILDINGLAB_API FDesertRoofDecorationPart
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") TObjectPtr<UStaticMesh> Mesh = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") FTransform Transform = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") bool bPotCluster = false;
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") int32 ResourceIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DESERTBUILDINGLAB_API FDesertRoofDecorationCheck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") bool bAllowed = false;
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") FString Reason;
    UPROPERTY(BlueprintReadOnly, Category="Roof Decoration") TArray<FBox> LocalBounds;
};

DESERTBUILDINGLAB_API FString DesertRoofDecorationVariantLabel(int32 Index);
