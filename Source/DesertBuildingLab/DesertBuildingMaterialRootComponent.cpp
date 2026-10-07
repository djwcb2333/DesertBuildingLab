#include "DesertBuildingMaterialRootComponent.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

UDesertBuildingMaterialRootComponent::UDesertBuildingMaterialRootComponent()
{
    bWantsOnUpdateTransform = true;
    PrimaryComponentTick.bCanEverTick = false;
}

void UDesertBuildingMaterialRootComponent::ApplyToActor(AActor* Actor)
{
    if (!Actor) return;
    const FVector Origin = Actor->GetActorLocation();
    const FQuat Rotation = Actor->GetActorQuat();
    const FVector Scale = Actor->GetActorScale3D();
    const FVector Basis[3] = {
        FMath::Abs(Scale.X) > SMALL_NUMBER ? Rotation.GetAxisX() / Scale.X : FVector::ZeroVector,
        FMath::Abs(Scale.Y) > SMALL_NUMBER ? Rotation.GetAxisY() / Scale.Y : FVector::ZeroVector,
        FMath::Abs(Scale.Z) > SMALL_NUMBER ? Rotation.GetAxisZ() / Scale.Z : FVector::ZeroVector
    };
    const FVector4 Rows[4] = { FVector4(Origin, 1.0), FVector4(Basis[0], 0.0),
        FVector4(Basis[1], 0.0), FVector4(Basis[2], 0.0) };
    TArray<UStaticMeshComponent*> Components;
    Actor->GetComponents(Components); // 包括ISM、独立地基及所有楼层。
    for (UStaticMeshComponent* Component : Components)
    {
        // SetDefault会重置内部CPD；先保留保留区以外的实时值，避免建筑移动清掉用户参数。
        const TArray<float> InternalData = Component->GetCustomPrimitiveData().Data;
        for (int32 Row = 0; Row < 4; ++Row)
        {
            // 仅覆盖保留的0..15，保留其他CPD。默认值随关卡保存，内部值供材质读取。
            Component->SetDefaultCustomPrimitiveDataVector4(Row * 4, Rows[Row]);
            Component->SetCustomPrimitiveDataVector4(Row * 4, Rows[Row]);
        }
        if (InternalData.Num() > 16)
            Component->SetCustomPrimitiveDataFloatArray(16,
                MakeArrayView(InternalData.GetData() + 16, InternalData.Num() - 16));
    }
}

void UDesertBuildingMaterialRootComponent::RefreshMaterialCoordinates()
{
    ApplyToActor(GetOwner());
}

void UDesertBuildingMaterialRootComponent::OnRegister()
{
    Super::OnRegister();
    RefreshMaterialCoordinates();
}

void UDesertBuildingMaterialRootComponent::OnUpdateTransform(
    EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
    RefreshMaterialCoordinates();
}
