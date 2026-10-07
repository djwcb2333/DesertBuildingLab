#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DesertBuildingMaterialRootComponent.generated.h"

/** 烘焙快照的根：移动、旋转、缩放时刷新所有墙片共用的材质投影坐标。
 * 不替换旧DesertBuilding根，保留旧建筑关卡的默认子组件名称与结构。
 */
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent))
class DESERTBUILDINGLAB_API UDesertBuildingMaterialRootComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UDesertBuildingMaterialRootComponent();

    UFUNCTION(BlueprintCallable, Category="Building|Materials")
    void RefreshMaterialCoordinates();

    // 建筑Actor与静态快照调用同一实现，CPD0/4/8/12不产生两套约定。
    static void ApplyToActor(AActor* Actor);

protected:
    virtual void OnRegister() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags,
        ETeleportType Teleport = ETeleportType::None) override;
};
