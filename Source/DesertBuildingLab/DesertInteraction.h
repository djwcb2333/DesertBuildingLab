#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "DesertInteraction.generated.h"

class ADesertBuilding;
class ACameraActor;
class UDesertBuildingStyle;

// 交互层只编辑格子数据。它不决定墙、窗或屋顶使用哪个模型。
UCLASS()
class DESERTBUILDINGLAB_API ADesertBuildController : public APlayerController
{
    GENERATED_BODY()
public:
    ADesertBuildController();
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<ADesertBuilding> Building;
    UPROPERTY(BlueprintReadOnly) FIntVector HoverCell = FIntVector::ZeroValue;
    UPROPERTY(BlueprintReadOnly) FIntVector AddTarget = FIntVector::ZeroValue;
    UPROPERTY(BlueprintReadOnly) bool bHasHover = false;
    UPROPERTY(BlueprintReadOnly) bool bCanAdd = false;
    UPROPERTY(BlueprintReadOnly) FString StatusMessage;
private:
    UPROPERTY() TObjectPtr<ACameraActor> OrbitCamera;
    UPROPERTY() TObjectPtr<UDesertBuildingStyle> SandStyle;
    UPROPERTY() TObjectPtr<UDesertBuildingStyle> WhiteStyle;
    float Yaw = -55.f;
    float Pitch = 32.f;
    float Distance = 4200.f;
    bool bPreviewOcclusion = false;
    FVector Focus = FVector(0, 0, 340);
    void UpdateCamera();
    void UpdateHover();
    void SaveLayout();
    void LoadLayout();
};

UCLASS()
class DESERTBUILDINGLAB_API ADesertBuildHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS()
class DESERTBUILDINGLAB_API ADesertBuildGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ADesertBuildGameMode();
};
