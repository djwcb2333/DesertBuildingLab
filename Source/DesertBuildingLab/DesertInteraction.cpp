#include "DesertInteraction.h"
#include "DesertBuilding.h"
#include "DesertBuildingStyle.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/DefaultValueHelper.h"

ADesertBuildGameMode::ADesertBuildGameMode()
{
    PlayerControllerClass = ADesertBuildController::StaticClass();
    HUDClass = ADesertBuildHUD::StaticClass();
    DefaultPawnClass = nullptr;
}

ADesertBuildController::ADesertBuildController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
    PrimaryActorTick.bCanEverTick = true;
}

void ADesertBuildController::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<ADesertBuilding> It(GetWorld()); It; ++It) { Building = *It; break; }
    if (!Building) { StatusMessage = TEXT("No DesertBuilding actor found in this map."); return; }
    SandStyle = LoadObject<UDesertBuildingStyle>(nullptr, TEXT("/Game/DesertPrototype/Styles/DA_DesertSand.DA_DesertSand"));
    WhiteStyle = LoadObject<UDesertBuildingStyle>(nullptr, TEXT("/Game/DesertPrototype/Styles/DA_DesertChalk.DA_DesertChalk"));
    OrbitCamera = GetWorld()->SpawnActor<ACameraActor>();
    OrbitCamera->GetCameraComponent()->SetFieldOfView(45.f);
    OrbitCamera->GetCameraComponent()->bConstrainAspectRatio = false;
    UpdateCamera();
    SetViewTarget(OrbitCamera);
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    StatusMessage = TEXT("Ready. Add on roofs, sides, or the ground grid.");
}

void ADesertBuildController::UpdateCamera()
{
    if (!OrbitCamera) return;
    const float Y = FMath::DegreesToRadians(Yaw), P = FMath::DegreesToRadians(Pitch);
    const FVector Target = Focus + (Building ? Building->GetActorLocation() : FVector::ZeroVector);
    const FVector Offset(FMath::Cos(Y) * FMath::Cos(P), FMath::Sin(Y) * FMath::Cos(P), FMath::Sin(P));
    const FVector Position = Target + Offset * Distance;
    OrbitCamera->SetActorLocationAndRotation(Position, (Target - Position).Rotation());
}

// 射线检测逻辑格子而非窗框或矮墙，避免细节模型改变之后选中的格子变乱。
void ADesertBuildController::UpdateHover()
{
    bHasHover = false; bCanAdd = false;
    if (!Building) return;
    FVector Origin, Direction;
    if (!DeprojectMousePositionToWorld(Origin, Direction)) return;
    Origin = Building->GetActorTransform().InverseTransformPosition(Origin);
    Direction = Building->GetActorTransform().InverseTransformVector(Direction).GetSafeNormal();
    float Closest = TNumericLimits<float>::Max();
    FIntVector Normal = FIntVector::ZeroValue;
    const FVector Size(Building->CellSize, Building->CellSize, Building->FloorHeight);
    for (const FIntVector& Cell : Building->Cells)
    {
        const FVector Min = FVector(Cell) * Size, Max = Min + Size;
        float Near = 0.f, Far = TNumericLimits<float>::Max();
        FIntVector EntryNormal = FIntVector::ZeroValue;
        bool Valid = true;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::Abs(Direction[Axis]) < KINDA_SMALL_NUMBER)
            {
                if (Origin[Axis] < Min[Axis] || Origin[Axis] > Max[Axis]) { Valid = false; break; }
                continue;
            }
            float A = (Min[Axis] - Origin[Axis]) / Direction[Axis];
            float B = (Max[Axis] - Origin[Axis]) / Direction[Axis];
            int32 Sign = -1;
            if (A > B) { Swap(A, B); Sign = 1; }
            if (A > Near) { Near = A; EntryNormal = FIntVector::ZeroValue; EntryNormal[Axis] = Sign; }
            Far = FMath::Min(Far, B);
            if (Far < Near) { Valid = false; break; }
        }
        if (Valid && Near > 0.f && Near < Closest)
        {
            Closest = Near; HoverCell = Cell; Normal = EntryNormal; bHasHover = true;
        }
    }
    if (bHasHover) AddTarget = HoverCell + Normal;
    else if (Direction.Z < -KINDA_SMALL_NUMBER)
    {
        const float T = -Origin.Z / Direction.Z;
        if (T <= 0.f) return;
        const FVector P = Origin + T * Direction;
        AddTarget = FIntVector(FMath::FloorToInt(P.X / Size.X), FMath::FloorToInt(P.Y / Size.Y), 0);
    }
    else return;
    bCanAdd = AddTarget.X >= -3 && AddTarget.X <= 3 && AddTarget.Y >= -3 && AddTarget.Y <= 3 &&
        AddTarget.Z >= 0 && AddTarget.Z <= 4 && !Building->HasCell(AddTarget) &&
        (AddTarget.Z == 0 || Building->HasCell(AddTarget - FIntVector(0,0,1)));
    if (bCanAdd)
    {
        const FVector Center = Building->GetActorTransform().TransformPosition((FVector(AddTarget) + FVector(.5)) * Size);
        DrawDebugBox(GetWorld(), Center, Size * .49 * Building->GetActorScale3D().GetAbs(), Building->GetActorQuat(), FColor(60, 230, 170), false, 0.f, 0, 2.f);
    }
    if (bHasHover)
    {
        const FVector Center = Building->GetActorTransform().TransformPosition((FVector(HoverCell) + FVector(.5)) * Size);
        DrawDebugBox(GetWorld(), Center, Size * .495 * Building->GetActorScale3D().GetAbs(), Building->GetActorQuat(), FColor(255, 201, 96), false, 0.f, 0, 1.f);
    }
}

void ADesertBuildController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!Building) return;
    if (IsInputKeyDown(EKeys::MiddleMouseButton))
    {
        float DX, DY; GetInputMouseDelta(DX, DY);
        Yaw -= DX * .4f; Pitch = FMath::Clamp(Pitch + DY * .3f, 12.f, 80.f);
    }
    if (WasInputKeyJustPressed(EKeys::MouseScrollUp)) Distance = FMath::Max(1200.f, Distance - 200.f);
    if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) Distance = FMath::Min(6500.f, Distance + 200.f);
    UpdateCamera(); UpdateHover();
    if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        if (bCanAdd && Building->AddCell(AddTarget)) StatusMessage = TEXT("Added: walls, roof and parapets updated.");
        else StatusMessage = TEXT("Cannot add here: occupied, out of bounds, or no support below.");
    }
    if (WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        if (bHasHover && Building->RemoveCell(HoverCell)) StatusMessage = TEXT("Removed: newly exposed surfaces rebuilt.");
        else StatusMessage = TEXT("Remove the upper cell first; floating floors are disabled in v1.");
    }
    if (WasInputKeyJustPressed(EKeys::R)) { Building->ResetDemo(); StatusMessage = TEXT("Demo layout restored (save file unchanged)."); }
    if (WasInputKeyJustPressed(EKeys::One) && SandStyle) { Building->Style = SandStyle; Building->Rebuild(); StatusMessage = TEXT("Sand style. Layout unchanged."); }
    if (WasInputKeyJustPressed(EKeys::Two) && WhiteStyle) { Building->Style = WhiteStyle; Building->Rebuild(); StatusMessage = TEXT("Chalk style. Layout unchanged."); }
    if (WasInputKeyJustPressed(EKeys::O)) SaveLayout();
    if (WasInputKeyJustPressed(EKeys::I)) LoadLayout();
    if (WasInputKeyJustPressed(EKeys::B))
    {
        bPreviewOcclusion = !bPreviewOcclusion;
        Building->SetOcclusionFade(bPreviewOcclusion ? .15f : 1.f);
        StatusMessage = bPreviewOcclusion ? TEXT("Occlusion preview: building fades; foundation stays solid.") : TEXT("Occlusion preview off. Collision was unchanged.");
    }
}

void ADesertBuildController::SaveLayout()
{
    if (!Building) return;
    FString Data = TEXT("DESERT_LAYOUT_V1\n");
    for (const FIntVector& C : Building->Cells) Data += FString::Printf(TEXT("%d,%d,%d\n"), C.X, C.Y, C.Z);
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("DesertPrototype");
    IFileManager::Get().MakeDirectory(*Dir, true);
    StatusMessage = FFileHelper::SaveStringToFile(Data, *(Dir / TEXT("UserLayout.txt"))) ?
        TEXT("Saved to Saved/DesertPrototype/UserLayout.txt") : TEXT("Save failed.");
}

void ADesertBuildController::LoadLayout()
{
    FString Data;
    if (!FFileHelper::LoadFileToString(Data, *(FPaths::ProjectSavedDir() / TEXT("DesertPrototype/UserLayout.txt"))))
    { StatusMessage = TEXT("No saved layout. Press O to save one first."); return; }
    TArray<FString> Lines; Data.ParseIntoArrayLines(Lines);
    if (Lines.Num() == 0 || Lines[0] != TEXT("DESERT_LAYOUT_V1")) { StatusMessage = TEXT("Invalid save version."); return; }
    TArray<FIntVector> NewCells;
    for (int32 I = 1; I < Lines.Num(); ++I)
    {
        TArray<FString> Parts; Lines[I].ParseIntoArray(Parts, TEXT(","), false);
        int32 X, Y, Z;
        if (Parts.Num() != 3 || !FDefaultValueHelper::ParseInt(Parts[0], X) || !FDefaultValueHelper::ParseInt(Parts[1], Y) || !FDefaultValueHelper::ParseInt(Parts[2], Z) ||
            X < -3 || X > 3 || Y < -3 || Y > 3 || Z < 0 || Z > 4)
        { StatusMessage = TEXT("Invalid save cell. Current building kept."); return; }
        const FIntVector C(X,Y,Z);
        if (NewCells.Contains(C)) { StatusMessage = TEXT("Duplicate cell in save. Current building kept."); return; }
        NewCells.Add(C);
    }
    for (const FIntVector& C : NewCells)
        if (C.Z > 0 && !NewCells.Contains(C - FIntVector(0,0,1))) { StatusMessage = TEXT("Unsupported cell in save. Current building kept."); return; }
    Building->Cells = MoveTemp(NewCells); Building->Rebuild(); StatusMessage = TEXT("Saved layout loaded.");
}

void ADesertBuildHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* PC = Cast<ADesertBuildController>(PlayerOwner);
    if (!Canvas || !PC) return;
    DrawRect(FLinearColor(.028f,.04f,.05f,.87f), 20, 20, 720, 230);
    DrawText(TEXT("DESERT / MODULAR BUILDING LAB"), FLinearColor(1.f,.8f,.45f), 36, 34, GEngine->GetMediumFont(), 1.8f);
    DrawText(TEXT("LMB Add    RMB Remove    MMB drag Orbit"), FLinearColor::White, 36, 75, GEngine->GetMediumFont(), 1.5f);
    DrawText(TEXT("Wheel Zoom    R Reset    1 Sand / 2 Chalk"), FLinearColor::White, 36, 109, GEngine->GetMediumFont(), 1.5f);
    DrawText(TEXT("O Save    I Load    B Fade preview    Esc Stop"), FLinearColor::White, 36, 143, GEngine->GetMediumFont(), 1.5f);
    if (PC->Building)
    {
        const FString Stats = FString::Printf(TEXT("Cells %d   Walls %d   Roofs %d   Parapets %d"), PC->Building->Cells.Num(),
            PC->Building->VisibleWallCount, PC->Building->RoofCount, PC->Building->ParapetCount);
        DrawText(Stats, FLinearColor(.5f,.95f,.85f), 36, 180, GEngine->GetMediumFont(), 1.5f);
    }
    DrawText(TEXT("Green = add target / amber = occupied cell"), FLinearColor(.7f,.75f,.8f), 36, 217, GEngine->GetMediumFont(), 1.35f);
    DrawRect(FLinearColor(.028f,.04f,.05f,.87f), 20, Canvas->SizeY-75, FMath::Min(1250.f,float(Canvas->SizeX)-40.f), 50);
    DrawText(PC->StatusMessage, FLinearColor::White, 34, Canvas->SizeY-63, GEngine->GetMediumFont(), 1.5f);
}
