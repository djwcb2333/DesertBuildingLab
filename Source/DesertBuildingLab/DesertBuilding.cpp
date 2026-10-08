#include "DesertBuilding.h"

#include "DesertBuildingStyle.h"
#include "DesertBuildingDesign.h"
#include "DesertBuildingModule.h"
#include "DesertBuildingMaterialRootComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/ScopeExit.h"

namespace DesertBuildingRules
{
    // 顺序为前、右、后、左；模型统一外侧朝 -Y，通过旋转复用。
    static const FIntVector Sides[4] = {
        FIntVector(0, -1, 0), FIntVector(1, 0, 0),
        FIntVector(0, 1, 0), FIntVector(-1, 0, 0)
    };

    static FQuat SideRotation(int32 Side)
    {
        return FRotator(0.0, Side * 90.0, 0.0).Quaternion();
    }

    static void ApplyModuleMaterialOverrides(const UDesertBuildingStyle* Style,
        UInstancedStaticMeshComponent* Component)
    {
        if (!Style || !Component || Style->ModuleMaterialOverrides.IsEmpty()) return;
        const UStaticMesh* Mesh = Component->GetStaticMesh();
        if (!Mesh) return;
        for (int32 Index = 0; Index < Mesh->GetStaticMaterials().Num(); ++Index)
        {
            UMaterialInterface* Source = Mesh->GetMaterial(Index);
            if (!Source) continue;
            const TObjectPtr<UMaterialInterface>* Replacement = Style->ModuleMaterialOverrides.Find(
                TObjectPtr<UMaterialInterface>(Source));
            if (Replacement && IsValid(Replacement->Get())) Component->SetMaterial(Index, Replacement->Get());
        }
    }

    static uint32 StableHash(const FIntVector& Cell, int32 Side, int32 Seed)
    {
        // 仅依赖坐标和种子。添加另一格不会让已经存在的窗户随机跳动。
        uint32 Hash = static_cast<uint32>(Seed);
        Hash = Hash * 1664525u + static_cast<uint32>(Cell.X) * 73856093u;
        Hash = Hash * 1664525u + static_cast<uint32>(Cell.Y) * 19349663u;
        Hash = Hash * 1664525u + static_cast<uint32>(Cell.Z) * 83492791u;
        return Hash ^ (static_cast<uint32>(Side) * 2654435761u);
    }

    static void InsetSlabToWalls(const TSet<FIntVector>& Occupied, const FIntVector& Cell,
        float CellSize, float WallThickness, FVector& Center, FVector& Size)
    {
        // 板只铺到外墙内侧；相邻格的公共边保持整格，两个板在网格线上无缝相接。
        // 每边单独收缩，非对称情况同时移动中心，不能只把整块板等比缩小。
        const float MinX = Occupied.Contains(Cell + Sides[3]) ? 0.0f : WallThickness;
        const float MaxX = Occupied.Contains(Cell + Sides[1]) ? 0.0f : WallThickness;
        const float MinY = Occupied.Contains(Cell + Sides[0]) ? 0.0f : WallThickness;
        const float MaxY = Occupied.Contains(Cell + Sides[2]) ? 0.0f : WallThickness;
        Center.X += (MinX - MaxX) * 0.5f;
        Center.Y += (MinY - MaxY) * 0.5f;
        Size.X = CellSize - MinX - MaxX;
        Size.Y = CellSize - MinY - MaxY;
    }
}

ADesertBuilding::ADesertBuilding()
{
    SelectedBlock.PotPlacement=EDesertPotPlacement::Automatic;
    SelectedBlock.StairConnection=EDesertStairConnection::AdjacentFloors;
    PrimaryActorTick.bCanEverTick = false;
    BuildingRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BuildingRoot"));
    SetRootComponent(BuildingRoot);
    BuildingRoot->SetMobility(EComponentMobility::Movable);
    FoundationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Foundation"));
    FoundationRoot->SetupAttachment(BuildingRoot);
    FoundationRoot->SetMobility(EComponentMobility::Movable);
    FoundationRoot->ComponentTags.Add(TEXT("DesertFoundation"));
    FirstFloorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Floor_01"));
    FirstFloorRoot->SetupAttachment(BuildingRoot);
    FirstFloorRoot->SetMobility(EComponentMobility::Movable);
    FirstFloorRoot->ComponentTags.Add(TEXT("DesertFloor_1"));

    // 共用一个立方体模型，实例只记录位置/旋转/缩放。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
    CubeMesh = CubeFinder.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    DefaultBaseMaterial = MaterialFinder.Object;

    auto MakeInstances = [this](FName Name, bool bCube, bool bCollision)
    {
        UInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Component->SetupAttachment(FirstFloorRoot);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetStaticMesh(bCube ? CubeMesh.Get() : nullptr);
        Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        Component->SetCollisionResponseToAllChannels(ECR_Block);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->ComponentTags.Add(TEXT("DesertFloor_1"));
        return Component;
    };

    WallParts = MakeInstances(TEXT("WallParts"), true, true);
    TrimParts = MakeInstances(TEXT("TrimParts"), true, true);
    WoodParts = MakeInstances(TEXT("WoodParts"), true, false);
    DarkParts = MakeInstances(TEXT("DarkParts"), true, false);
    ClothParts = MakeInstances(TEXT("ClothParts"), true, false);
    SolidWallModules = MakeInstances(TEXT("SolidWallModules"), false, true);
    WindowWallModules = MakeInstances(TEXT("WindowWallModules"), false, true);
    DoorWallModules = MakeInstances(TEXT("DoorWallModules"), false, true);
    RoofModules = MakeInstances(TEXT("RoofModules"), false, true);
    ParapetModules = MakeInstances(TEXT("ParapetModules"), false, true);
    AwningModules = MakeInstances(TEXT("AwningModules"), false, false);
    FoundationParts = MakeInstances(TEXT("FoundationParts"), true, true);
    FoundationModules = MakeInstances(TEXT("FoundationModules"), false, true);
    FoundationParts->SetupAttachment(FoundationRoot);
    FoundationModules->SetupAttachment(FoundationRoot);
    FoundationParts->ComponentTags.Add(TEXT("DesertFoundation"));
    FoundationModules->ComponentTags.Add(TEXT("DesertFoundation"));
    FoundationParts->ComponentTags.Remove(TEXT("DesertFloor_1"));
    FoundationModules->ComponentTags.Remove(TEXT("DesertFloor_1"));
    RoomCellModules = MakeInstances(TEXT("RoomCellModules"), false, true);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for (int32 Index = 0; Index < 29; ++Index)
    {
        const bool bCollision = Index < 18 ? (Index % 6 <= 1) : (Index == 18 || Index == 19 || Index == 21 || Index >= 27);
        UInstancedStaticMeshComponent* Parts = MakeInstances(FName(*FString::Printf(TEXT("BlockParts_%02d"),Index)), false, bCollision);
        if (Index < 18) Parts->SetStaticMesh(Index / 6 == 0 ? CubeMesh.Get() : (Index / 6 == 1 ? CylinderFinder.Object.Get() : SphereFinder.Object.Get()));
        BlockParts.Add(Parts);
    }
    const TCHAR* WallKinds[] = { TEXT("Solid"), TEXT("Window"), TEXT("Door") };
    const TCHAR* WallEnds[] = { TEXT("Left"), TEXT("Right"), TEXT("Both") };
    for (int32 WallType = 0; WallType < 3; ++WallType)
        for (int32 EndIndex = 0; EndIndex < 3; ++EndIndex)
            WallEndModules.Add(MakeInstances(FName(*FString::Printf(TEXT("%sWallTrim%sModules"),
                WallKinds[WallType], WallEnds[EndIndex])), false, true));
    RubbleModules = MakeInstances(TEXT("RubbleModules"), false, false);
    RubbleParts = MakeInstances(TEXT("RubbleParts"), true, false);
    RoofCrownModules = MakeInstances(TEXT("RoofCrownModules"), false, true);
    for (int32 WallType = 0; WallType < 3; ++WallType)
        for (int32 EndIndex = 0; EndIndex < 3; ++EndIndex)
            WallOuterCornerModules.Add(MakeInstances(FName(*FString::Printf(TEXT("%sWallOuterSoft%sModules"),
                WallKinds[WallType], WallEnds[EndIndex])), false, true));
    LShapeStairModules=MakeInstances(TEXT("LShapeStairModules"),false,true);
    UShapeStairModules=MakeInstances(TEXT("UShapeStairModules"),false,true);
    SetDemoCells();
}

void ADesertBuilding::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // 地图加载阶段外部地面碰撞可能未就绪，必须保留已经保存的实例。
    // 仅在作者编辑属性、结束移动或点击工具按钮时重新判断规则。
    if (bPendingPlacementGeneration && bGenerateOnPlacement && !IsTemplate() && !HasAnyFlags(RF_WasLoaded))
    {
        bPendingPlacementGeneration = false;
        ApplyLinkedDesign(false);
        Rebuild();
    }
    UpdateBuildingMaterialCoordinates();
}

void ADesertBuilding::UpdateBuildingMaterialCoordinates()
{
    UDesertBuildingMaterialRootComponent::ApplyToActor(this);
}

void ADesertBuilding::PostActorCreated()
{
    Super::PostActorCreated();
    // PostActorCreated只用于新创建Actor；PostLoad/既有地图加载不会走这条入口。
    bPendingPlacementGeneration = bGenerateOnPlacement && !IsTemplate() && !HasAnyFlags(RF_WasLoaded);
}

void ADesertBuilding::ApplyLinkedDesign(bool bRebuild)
{
    if (!DesignAsset) return;
    DesignAsset->ApplyTo(this, false);
    AppliedDesignRevision = DesignAsset->Revision;
    if (bRebuild) Rebuild();
}

void ADesertBuilding::BeginPlay()
{
    Super::BeginPlay();
    // 通常编辑器保存时已经同步。只给未同步的链接资产兜底，旧V2 Actor不生成。
    if (DesignAsset && AppliedDesignRevision != DesignAsset->Revision) ApplyLinkedDesign(true);
}

#if WITH_EDITOR
void ADesertBuilding::PostEditUndo()
{
    Super::PostEditUndo();
    Rebuild();
}
void ADesertBuilding::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (!IsTemplate() && GetWorld() && !GetWorld()->IsGameWorld()) Rebuild();
}
void ADesertBuilding::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished && GetWorld() && !GetWorld()->IsGameWorld()) Rebuild();
    else UpdateBuildingMaterialCoordinates();
}
#endif

bool ADesertBuilding::SetRoofOpening(FIntVector Cell, int32 Side, float Width)
{
    if (Side < 0 || Side > 3 || !HasCell(Cell) || HasCell(Cell + FIntVector(0,0,1)) ||
        HasCell(Cell + DesertBuildingRules::Sides[Side]) || !FMath::IsFinite(Width)) return false;
    Modify();
    for (FDesertRoofOpening& Opening : RoofOpenings)
    {
        if (Opening.Cell == Cell && Opening.Side == Side)
        {
            Opening.Width = FMath::Clamp(Width, 60.0f, CellSize);
            Rebuild();
            return true;
        }
    }
    FDesertRoofOpening& Opening = RoofOpenings.AddDefaulted_GetRef();
    Opening.Cell = Cell;
    Opening.Side = Side;
    Opening.Width = FMath::Clamp(Width, 60.0f, CellSize);
    Rebuild();
    return true;
}

void ADesertBuilding::SetDemoCells()
{
    Cells = {
        FIntVector(-1, -1, 0), FIntVector(0, -1, 0), FIntVector(1, -1, 0),
        FIntVector(-1, 0, 0), FIntVector(0, 0, 0), FIntVector(1, 0, 0),
        FIntVector(-1, 0, 1), FIntVector(0, 0, 1), FIntVector(-1, 0, 2)
    };
}

bool ADesertBuilding::IsCellInBounds(FIntVector Cell) const
{
    return Cell.X >= -3 && Cell.X <= 3 && Cell.Y >= -3 && Cell.Y <= 3 && Cell.Z >= 0 && Cell.Z <= 4;
}

bool ADesertBuilding::HasCell(FIntVector Cell) const
{
    return Cells.Contains(Cell);
}

bool ADesertBuilding::AddCell(FIntVector Cell)
{
    FDesertBlockPlacement Proposal;
    Proposal.Cell=Cell;
    return TryAddPlacement(true,Proposal);
}

bool ADesertBuilding::RemoveCell(FIntVector Cell)
{
    FDesertBlockPlacement Proposal;
    Proposal.Cell=Cell;
    return RemovePlacementAndDependents(true,Proposal)>0;
}

void ADesertBuilding::AddSelectedCell() { AddCell(SelectedCell); }
void ADesertBuilding::RemoveSelectedCell() { RemoveCell(SelectedCell); }

void ADesertBuilding::ResetDemo()
{
    Modify();
    SetDemoCells();
    RoofOpenings.Reset();
    Blocks.Reset();
    RoofDecorations.Reset();
    RoomAppearanceOverrides.Reset();
    Rebuild();
}

void ADesertBuilding::NormalizeCells()
{
    // 原始输入持久保存。只整理生成用副本，旧越界/重复数据由诊断和显式清理处理。
    NormalizedCells=Cells;
    NormalizedCells.Sort([](const FIntVector& A, const FIntVector& B)
    {
        if (A.Z != B.Z) return A.Z < B.Z;
        if (A.Y != B.Y) return A.Y < B.Y;
        return A.X < B.X;
    });
    Occupied.Reset();
    TArray<FIntVector> ValidCells;
    ValidCells.Reserve(NormalizedCells.Num());
    for (const FIntVector& Cell : NormalizedCells)
    {
        if (!IsCellInBounds(Cell) || Occupied.Contains(Cell)) continue;
        Occupied.Add(Cell);
        ValidCells.Add(Cell);
    }
    NormalizedCells = MoveTemp(ValidCells);
}

void ADesertBuilding::ConfigureMeshesAndMaterials()
{
    // 先移除上一轮的动态实例，避免重建时一层层套MID或把旧皮肤带到新网格。
    RestoreOcclusionMaterials();
    UInstancedStaticMeshComponent* AllComponents[] = {
        WallParts, TrimParts, WoodParts, DarkParts, ClothParts,
        SolidWallModules, WindowWallModules, DoorWallModules, RoofModules, ParapetModules, AwningModules,
        FoundationParts, FoundationModules, RoomCellModules
    };
    for (UInstancedStaticMeshComponent* Component : AllComponents)
    {
        Component->ClearInstances();
    }
    for (UInstancedStaticMeshComponent* Component : WallEndModules) Component->ClearInstances();
    for (int32 Index = 0; Index < WallOuterCornerModules.Num(); ++Index)
    {
        UInstancedStaticMeshComponent* Component = WallOuterCornerModules[Index];
        Component->ClearInstances();
        Component->EmptyOverrideMaterials();
        Component->SetStaticMesh(Style ? Style->ResolveWallOuterCornerMesh(Index / 3, Index % 3 + 1) : nullptr);
    }
    if (Style && Style->bUseWallOuterCornerVariants && !Style->bUseWallEndVariants)
        ValidationMessages.Add(TEXT("外露墙角软化需要先启用Wall End Variants，保留原墙行为；未在未让位的旧完整侧墙上叠加圆角。"));
    RubbleModules->ClearInstances();
    RubbleModules->EmptyOverrideMaterials();
    RubbleModules->SetStaticMesh(Style ? Style->ModuleRubbleCluster.Get() : nullptr);
    RoofCrownModules->EmptyOverrideMaterials();
    RoofCrownModules->ClearInstances();
    RoofCrownModules->SetStaticMesh(Style ? Style->ModuleRoofCrown.Get() : nullptr);
    for (UInstancedStaticMeshComponent* Component : {LShapeStairModules.Get(),UShapeStairModules.Get()})
    { Component->ClearInstances(); Component->EmptyOverrideMaterials(); }
    LShapeStairModules->SetStaticMesh(Style ? Style->ModuleLShapeStairs.Get() : nullptr);
    UShapeStairModules->SetStaticMesh(Style ? Style->ModuleUShapeStairs.Get() : nullptr);
    DesertBuildingRules::ApplyModuleMaterialOverrides(Style,LShapeStairModules);
    DesertBuildingRules::ApplyModuleMaterialOverrides(Style,UShapeStairModules);
    RubbleParts->ClearInstances();

    // 保底材质仍能使没有任何自制资产的项目运行；正式色彩由 Style 材质提供。
    if (FallbackMaterials.Num() != 5)
    {
        FallbackMaterials.Reset();
        const FLinearColor Colors[5] = {
            FLinearColor(0.57f, 0.36f, 0.16f), FLinearColor(0.80f, 0.61f, 0.33f),
            FLinearColor(0.16f, 0.075f, 0.025f), FLinearColor(0.035f, 0.02f, 0.009f),
            FLinearColor(0.38f, 0.06f, 0.045f)
        };
        for (int32 Index = 0; Index < 5; ++Index)
        {
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(DefaultBaseMaterial, this);
            if (Material) Material->SetVectorParameterValue(TEXT("Color"), Colors[Index]);
            FallbackMaterials.Add(Material);
        }
    }
    WallParts->SetMaterial(0, Style && Style->WallMaterial ? Style->WallMaterial.Get() : FallbackMaterials[0].Get());
    TrimParts->SetMaterial(0, Style && Style->TrimMaterial ? Style->TrimMaterial.Get() : FallbackMaterials[1].Get());
    WoodParts->SetMaterial(0, Style && Style->WoodMaterial ? Style->WoodMaterial.Get() : FallbackMaterials[2].Get());
    DarkParts->SetMaterial(0, Style && Style->DarkMaterial ? Style->DarkMaterial.Get() : FallbackMaterials[3].Get());
    ClothParts->SetMaterial(0, Style && Style->ClothMaterial ? Style->ClothMaterial.Get() : FallbackMaterials[4].Get());
    FoundationParts->SetMaterial(0, Style && Style->FoundationMaterial ? Style->FoundationMaterial.Get()
        : (Style && Style->TrimMaterial ? Style->TrimMaterial.Get() : FallbackMaterials[1].Get()));
    RubbleParts->SetMaterial(0, Style && Style->FoundationMaterial ? Style->FoundationMaterial.Get() : FallbackMaterials[1].Get());

    // 自定义网格保留资产自己的多材质槽，便于在同一个门窗模块中使用砂岩和木材。
    UInstancedStaticMeshComponent* CustomComponents[] = {
        SolidWallModules, WindowWallModules, DoorWallModules, RoofModules, ParapetModules, AwningModules, FoundationModules, RoomCellModules
    };
    for (UInstancedStaticMeshComponent* Component : CustomComponents) Component->EmptyOverrideMaterials();
    SolidWallModules->SetStaticMesh(Style ? Style->WallSolid.Get() : nullptr);
    WindowWallModules->SetStaticMesh(Style ? Style->WallWindow.Get() : nullptr);
    DoorWallModules->SetStaticMesh(Style ? Style->WallDoor.Get() : nullptr);
    for (int32 Index = 0; Index < WallEndModules.Num(); ++Index)
    {
        UInstancedStaticMeshComponent* Component = WallEndModules[Index];
        Component->EmptyOverrideMaterials();
        Component->SetStaticMesh(Style && Style->bUseWallEndVariants
            ? Style->ResolveWallMesh(Index / 3, Index % 3 + 1) : nullptr);
    }
    RoofModules->SetStaticMesh(Style ? Style->RoofTile.Get() : nullptr);
    ParapetModules->SetStaticMesh(Style ? Style->Parapet.Get() : nullptr);
    AwningModules->SetStaticMesh(Style ? Style->AwningModule.Get() : nullptr);
    FoundationModules->SetStaticMesh(Style ? Style->Foundation.Get() : nullptr);
    RoomCellModules->SetStaticMesh(Style ? Style->RoomCellMesh.Get() : nullptr);
    // 在楼层组件复制模板之前统一替换；读取StaticMesh原槽而非上轮Override，
    // 防止反复重建变成A→B→C。整房的淡化参数检查也使用实际替换后的材质。
    for (UInstancedStaticMeshComponent* Component : CustomComponents)
        DesertBuildingRules::ApplyModuleMaterialOverrides(Style, Component);
    for (UInstancedStaticMeshComponent* Component : WallEndModules)
        DesertBuildingRules::ApplyModuleMaterialOverrides(Style, Component);
    for (UInstancedStaticMeshComponent* Component : WallOuterCornerModules)
        DesertBuildingRules::ApplyModuleMaterialOverrides(Style, Component);
    DesertBuildingRules::ApplyModuleMaterialOverrides(Style, RubbleModules);
    DesertBuildingRules::ApplyModuleMaterialOverrides(Style, RoofCrownModules);
    if (RoomCellModules->GetStaticMesh())
    {
        for (int32 Index = 0; Index < RoomCellModules->GetNumMaterials(); ++Index)
            if (UMaterialInterface* Material = RoomCellModules->GetMaterial(Index))
            {
                TArray<FMaterialParameterInfo> Parameters;
                TArray<FGuid> ParameterIds;
                Material->GetAllScalarParameterInfo(Parameters, ParameterIds);
                if (!Parameters.ContainsByPredicate([this](const FMaterialParameterInfo& Info) { return Info.Name == OcclusionFadeParameter; }))
                    ValidationMessages.AddUnique(FString::Printf(TEXT("整房模型材质 %s 没有标量参数 %s；楼层结构与碰撞正常，但此材质不会自动变透明，请在美术材质中实现该参数。"), *Material->GetName(), *OcclusionFadeParameter.ToString()));
            }
    }
}

void ADesertBuilding::RestoreOcclusionMaterials()
{
    for (const FDesertBuildingFadeSlot& Slot : OcclusionMaterialSlots)
    {
        // 若外部已经替换了此槽，不把外部的新材质覆盖回旧资源。
        if (Slot.Component && Slot.Component->GetMaterial(Slot.MaterialIndex) == Slot.DynamicMaterial)
        {
            Slot.Component->SetMaterial(Slot.MaterialIndex, Slot.BaseMaterial);
        }
    }
    OcclusionMaterialSlots.Reset();
}

TArray<UInstancedStaticMeshComponent*> ADesertBuilding::GetFloorTemplateComponents() const
{
    TArray<UInstancedStaticMeshComponent*> Result = {
        WallParts, TrimParts, WoodParts, DarkParts, ClothParts,
        SolidWallModules, WindowWallModules, DoorWallModules, RoofModules, ParapetModules, AwningModules,
        RoomCellModules
    };
    for (UInstancedStaticMeshComponent* Part : BlockParts) Result.Add(Part);
    // AdditionalFloorGroups序列化保存模板索引，不能把新桶插入旧桶中间。
    for (UInstancedStaticMeshComponent* Part : WallEndModules) Result.Add(Part);
    Result.Add(RubbleModules);
    Result.Add(RubbleParts);
    Result.Add(RoofCrownModules);
    // 旧Art06变体模板仍从53起。把新角桶追加在已存在的变体之后，
    // 再扩展变体库时新变体追加在角桶之后，不挪动任何持久楼层组件索引。
    const int32 Prefix = WallOuterCornerVariantPrefix == INDEX_NONE ? AwningVariantModules.Num() :
        FMath::Clamp(WallOuterCornerVariantPrefix, 0, AwningVariantModules.Num());
    for (int32 Index = 0; Index < Prefix; ++Index) Result.Add(AwningVariantModules[Index]);
    for (UInstancedStaticMeshComponent* Part : WallOuterCornerModules) Result.Add(Part);
    const int32 StairPrefix=StairTemplateVariantPrefix==INDEX_NONE ? AwningVariantModules.Num() :
        FMath::Clamp(StairTemplateVariantPrefix,Prefix,AwningVariantModules.Num());
    for (int32 Index = Prefix; Index < StairPrefix; ++Index) Result.Add(AwningVariantModules[Index]);
    Result.Add(LShapeStairModules); Result.Add(UShapeStairModules);
    const int32 Art09Prefix=Art09TemplateVariantPrefix==INDEX_NONE?AwningVariantModules.Num():
        FMath::Clamp(Art09TemplateVariantPrefix,StairPrefix,AwningVariantModules.Num());
    for (int32 Index = StairPrefix; Index < Art09Prefix; ++Index) Result.Add(AwningVariantModules[Index]);
    for(UInstancedStaticMeshComponent* Part:Art09AdditionalTemplates) Result.Add(Part);
    return Result;
}

TArray<UInstancedStaticMeshComponent*> ADesertBuilding::GetFoundationComponents() const
{
    TArray<UInstancedStaticMeshComponent*> Result;
    for (UInstancedStaticMeshComponent* Component : {FoundationParts.Get(), FoundationModules.Get()})
        if (Component && Component->GetInstanceCount() > 0) Result.Add(Component);
    for (UInstancedStaticMeshComponent* Component : FoundationBlockParts)
        if (Component && Component->GetInstanceCount() > 0) Result.Add(Component);
    return Result;
}

TArray<UInstancedStaticMeshComponent*> ADesertBuilding::GetFloorComponents(int32 FloorIndex) const
{
    TArray<UInstancedStaticMeshComponent*> Result;
    if (FloorIndex == 1)
    {
        for (UInstancedStaticMeshComponent* Component : GetFloorTemplateComponents())
            if (Component && Component->GetInstanceCount() > 0) Result.Add(Component);
    }
    else if (FloorIndex > 1)
    {
        for (const FDesertFloorComponentGroup& Group : AdditionalFloorGroups)
            if (Group.FloorIndex == FloorIndex)
                for (UInstancedStaticMeshComponent* Component : Group.Parts)
                    if (Component && Component->GetInstanceCount() > 0) Result.Add(Component);
    }
    return Result;
}

int32 ADesertBuilding::GetFloorCount() const
{
    int32 Count = GetFloorComponents(1).IsEmpty() ? 0 : 1;
    for (const FDesertFloorComponentGroup& Group : AdditionalFloorGroups)
        if (!GetFloorComponents(Group.FloorIndex).IsEmpty()) Count = FMath::Max(Count, Group.FloorIndex);
    return Count;
}

int32 ADesertBuilding::GetFloorIndexForComponent(UPrimitiveComponent* Component) const
{
    if (!Component) return -1;
    for (UInstancedStaticMeshComponent* Foundation : GetFoundationComponents())
        if (Foundation == Component) return 0;
    for (int32 FloorIndex = 1; FloorIndex <= GetFloorCount(); ++FloorIndex)
        for (UInstancedStaticMeshComponent* FloorPart : GetFloorComponents(FloorIndex))
            if (FloorPart == Component) return FloorIndex;
    return -1;
}

void ADesertBuilding::PrepareLayerComponents()
{
    // 重建只清除实例，保留按需创建的持久组件，Undo及地图重载仍拥有相同结构。
    for (UInstancedStaticMeshComponent* Component : GetFloorTemplateComponents())
        if (Component && Component->GetAttachParent() != FirstFloorRoot)
            Component->AttachToComponent(FirstFloorRoot, FAttachmentTransformRules::KeepRelativeTransform);
    for (UInstancedStaticMeshComponent* Component : {FoundationParts.Get(), FoundationModules.Get()})
        if (Component && Component->GetAttachParent() != FoundationRoot)
            Component->AttachToComponent(FoundationRoot, FAttachmentTransformRules::KeepRelativeTransform);
    for (FDesertFloorComponentGroup& Group : AdditionalFloorGroups)
        for (UInstancedStaticMeshComponent* Component : Group.Parts)
            if (Component) Component->ClearInstances();
    for (UInstancedStaticMeshComponent* Component : FoundationBlockParts)
        if (Component) Component->ClearInstances();
}

UInstancedStaticMeshComponent* ADesertBuilding::ResolveLayerComponent(UInstancedStaticMeshComponent* Template)
{
    if (!Template || Template == FoundationParts || Template == FoundationModules || GenerationFloorIndex == 1) return Template;
    const TArray<UInstancedStaticMeshComponent*> Templates = GetFloorTemplateComponents();
    const int32 TemplateIndex = Templates.IndexOfByKey(Template);
    if (TemplateIndex == INDEX_NONE) return Template;

    TArray<TObjectPtr<UInstancedStaticMeshComponent>>* Parts = nullptr;
    USceneComponent* Parent = FoundationRoot;
    const int32 FloorIndex = FMath::Max(GenerationFloorIndex, 0);
    if (FloorIndex == 0) Parts = &FoundationBlockParts;
    else
    {
        FDesertFloorComponentGroup* Group = AdditionalFloorGroups.FindByPredicate(
            [FloorIndex](const FDesertFloorComponentGroup& Item) { return Item.FloorIndex == FloorIndex; });
        if (!Group)
        {
            FDesertFloorComponentGroup& Added = AdditionalFloorGroups.AddDefaulted_GetRef();
            Added.FloorIndex = FloorIndex;
            Added.Root = NewObject<USceneComponent>(this, FName(*FString::Printf(TEXT("Floor_%02d"), FloorIndex)), RF_Transactional);
            Added.Root->CreationMethod = EComponentCreationMethod::Instance;
            AddInstanceComponent(Added.Root);
            Added.Root->SetMobility(EComponentMobility::Movable);
            Added.Root->SetupAttachment(BuildingRoot);
            Added.Root->ComponentTags.Add(FName(*FString::Printf(TEXT("DesertFloor_%d"), FloorIndex)));
            if (GetWorld()) Added.Root->RegisterComponent();
            Group = &Added;
        }
        Parts = &Group->Parts;
        Parent = Group->Root;
    }
    while (Parts->Num() <= TemplateIndex) Parts->Add(nullptr);
    UInstancedStaticMeshComponent* Component = (*Parts)[TemplateIndex];
    if (!Component)
    {
        const FName Name(*FString::Printf(TEXT("Layer_%02d_%s"), FloorIndex, *Template->GetFName().ToString()));
        Component = NewObject<UInstancedStaticMeshComponent>(this, Name, RF_Transactional);
        Component->CreationMethod = EComponentCreationMethod::Instance;
        AddInstanceComponent(Component);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetupAttachment(Parent);
        Component->ComponentTags.Add(FloorIndex == 0 ? FName(TEXT("DesertFoundation")) : FName(*FString::Printf(TEXT("DesertFloor_%d"), FloorIndex)));
        (*Parts)[TemplateIndex] = Component;
    }
    // 模板在本轮已设置皮肤，所有楼层都同步该网格的碰撞与多材质槽。
    if (Component->GetInstanceCount() == 0)
    {
        Component->SetStaticMesh(Template->GetStaticMesh());
        Component->EmptyOverrideMaterials();
        for (int32 Index = 0; Index < Template->GetNumMaterials(); ++Index) Component->SetMaterial(Index, Template->GetMaterial(Index));
        Component->SetCollisionEnabled(Template->BodyInstance.GetCollisionEnabled(false));
        Component->SetCollisionObjectType(Template->GetCollisionObjectType());
        Component->SetCollisionResponseToChannels(Template->GetCollisionResponseToChannels());
        Component->SetGenerateOverlapEvents(Template->GetGenerateOverlapEvents());
        Component->SetCanEverAffectNavigation(Template->CanEverAffectNavigation());
    }
    if (!Component->IsRegistered() && GetWorld()) Component->RegisterComponent();
    return Component;
}

void ADesertBuilding::AddLayerInstance(UInstancedStaticMeshComponent* Template, const FTransform& Transform)
{
    if (UInstancedStaticMeshComponent* Component = ResolveLayerComponent(Template)) Component->AddInstance(Transform, false);
}

float ADesertBuilding::GetComponentOcclusionAmount(const UInstancedStaticMeshComponent* Component) const
{
    int32 FloorIndex = 1;
    for (const FDesertFloorComponentGroup& Group : AdditionalFloorGroups)
        if (Group.Parts.Contains(Component)) { FloorIndex = Group.FloorIndex; break; }
    if (const float* Amount = FloorOcclusionAmounts.Find(FloorIndex)) return *Amount;
    return CurrentOcclusionFade;
}

void ADesertBuilding::RefreshOcclusionMaterials()
{
    RestoreOcclusionMaterials();
    CachedOcclusionParameter = OcclusionFadeParameter;
    // 有意排除 FoundationParts / FoundationModules：地基始终保留显示和碰撞。
    TArray<UInstancedStaticMeshComponent*> FadeComponents;
    for (int32 FloorIndex = 1; FloorIndex <= GetFloorCount(); ++FloorIndex) FadeComponents.Append(GetFloorComponents(FloorIndex));
    for (UInstancedStaticMeshComponent* Component : FadeComponents)
    {
        if (!Component || !Component->GetStaticMesh()) continue;
        const float Amount = GetComponentOcclusionAmount(Component);
        if (Amount >= 1.0f) continue;
        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
        {
            UMaterialInterface* BaseMaterial = Component->GetMaterial(Index);
            if (!BaseMaterial) continue;
            // 内置颜色材质可能本身已经是MID；复制参数到同级实例，避免MID套MID。
            UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(BaseMaterial);
            UMaterialInterface* ParentMaterial = ExistingMID ? ExistingMID->Parent.Get() : BaseMaterial;
            if (!ParentMaterial) continue;
            UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(ParentMaterial, this);
            if (!DynamicMaterial) continue;
            if (ExistingMID) DynamicMaterial->CopyParameterOverrides(ExistingMID);
            DynamicMaterial->SetScalarParameterValue(OcclusionFadeParameter, Amount);
            Component->SetMaterial(Index, DynamicMaterial);
            FDesertBuildingFadeSlot& Slot = OcclusionMaterialSlots.AddDefaulted_GetRef();
            Slot.Component = Component;
            Slot.BaseMaterial = BaseMaterial;
            Slot.DynamicMaterial = DynamicMaterial;
            Slot.MaterialIndex = Index;
        }
    }
}

void ADesertBuilding::SetOcclusionFade(float VisibleAmount)
{
    // 仅更新这一栋的动态材质参数；插值、遮挡判断交给调用方，碰撞始终不变。
    CurrentOcclusionFade = FMath::IsFinite(VisibleAmount) ? FMath::Clamp(VisibleAmount, 0.0f, 1.0f) : 1.0f;
    FloorOcclusionAmounts.Reset();
    TArray<AActor*> Attached;
    GetAttachedActors(Attached, true, true);
    for (AActor* Actor : Attached)
    {
        if (ADesertBuildingModule* Module = Cast<ADesertBuildingModule>(Actor)) Module->SetOcclusionFade(CurrentOcclusionFade);
    }
    ApplyOcclusionMaterials();
}

void ADesertBuilding::SetFloorOcclusionFade(int32 FloorIndex, float VisibleAmount)
{
    if (FloorIndex < 1 || FloorIndex > GetFloorCount() || GetFloorComponents(FloorIndex).IsEmpty()) return;
    FloorOcclusionAmounts.Add(FloorIndex, FMath::IsFinite(VisibleAmount) ? FMath::Clamp(VisibleAmount, 0.0f, 1.0f) : 1.0f);
    ApplyOcclusionMaterials();
}

void ADesertBuilding::RestoreAllOcclusionFade()
{
    SetOcclusionFade(1.0f);
}

void ADesertBuilding::ApplyOcclusionMaterials()
{
    // 新增淡化楼层或满显示恢复时，重新收集槽；仅重复插值时原位更新MID。
    bool bNeedsRefresh = CachedOcclusionParameter != OcclusionFadeParameter;
    for (int32 FloorIndex = 1; FloorIndex <= GetFloorCount(); ++FloorIndex)
        for (UInstancedStaticMeshComponent* Component : GetFloorComponents(FloorIndex))
        {
            const float Amount = GetComponentOcclusionAmount(Component);
            int32 CachedSlots = 0;
            for (const FDesertBuildingFadeSlot& Slot : OcclusionMaterialSlots)
                if (Slot.Component == Component)
                {
                    ++CachedSlots;
                    if (!Slot.DynamicMaterial || Component->GetMaterial(Slot.MaterialIndex) != Slot.DynamicMaterial || Amount >= 1.0f) bNeedsRefresh = true;
                }
            if (Amount < 1.0f && CachedSlots == 0 && Component->GetNumMaterials() > 0) bNeedsRefresh = true;
        }
    if (bNeedsRefresh) { RefreshOcclusionMaterials(); return; }
    for (const FDesertBuildingFadeSlot& Slot : OcclusionMaterialSlots)
        if (Slot.DynamicMaterial) Slot.DynamicMaterial->SetScalarParameterValue(OcclusionFadeParameter, GetComponentOcclusionAmount(Slot.Component));
}

FVector ADesertBuilding::CellBottomCenter(const FIntVector& Cell) const
{
    return FVector((Cell.X + 0.5) * CellSize, (Cell.Y + 0.5) * CellSize, Cell.Z * FloorHeight);
}

FVector ADesertBuilding::FaceOrigin(const FIntVector& Cell, int32 Side) const
{
    const float Thickness = CellSize * (20.0f / 300.0f);
    const FIntVector Direction = DesertBuildingRules::Sides[Side];
    return CellBottomCenter(Cell) + FVector(Direction.X, Direction.Y, 0) * (CellSize - Thickness) * 0.5;
}

void ADesertBuilding::AddBox(UInstancedStaticMeshComponent* Component, const FVector& Center,
    const FVector& Size, const FQuat& Rotation)
{
    if (!Component || Size.X <= KINDA_SMALL_NUMBER || Size.Y <= KINDA_SMALL_NUMBER || Size.Z <= KINDA_SMALL_NUMBER) return;
    // UE 自带 Cube 边长100厘米；将想要的尺寸除以100就是对应缩放。
    AddLayerInstance(Component, FTransform(Rotation, Center, Size / 100.0));
}

void ADesertBuilding::AddFaceBox(UInstancedStaticMeshComponent* Component, const FVector& Origin,
    const FQuat& Rotation, const FVector& LocalCenter, const FVector& Size)
{
    AddBox(Component, Origin + Rotation.RotateVector(LocalCenter), Size, Rotation);
}

int32 ADesertBuilding::ResolveWallOuterCornerMask(const FIntVector& Cell, int32 Side) const
{
    // 前/后Full墙拥有完整20cm角体，左右墙继续按旧规则让位。
    // 在角四格中只有当前格占用才是外露凸角；对角接触也不能软化接口。
    if ((Side != 0 && Side != 2) || !Occupied.Contains(Cell) ||
        Occupied.Contains(Cell + DesertBuildingRules::Sides[Side])) return 0;
    const FIntVector Outward = DesertBuildingRules::Sides[Side];
    const FIntVector Tangent = DesertBuildingRules::Sides[(Side + 1) % 4];
    auto IsExposedConvex = [this, &Cell, &Outward](const FIntVector& Along)
    {
        return !Occupied.Contains(Cell + Along) && !Occupied.Contains(Cell + Outward + Along);
    };
    const FIntVector LeftAlong(-Tangent.X, -Tangent.Y, -Tangent.Z);
    return (IsExposedConvex(LeftAlong) ? 1 : 0) | (IsExposedConvex(Tangent) ? 2 : 0);
}

void ADesertBuilding::BuildWall(const FIntVector& Cell, int32 Side, EWallType Type)
{
    const FVector Origin = FaceOrigin(Cell, Side);
    const FQuat Rotation = DesertBuildingRules::SideRotation(Side);
    const float Thickness = CellSize * (20.0f / 300.0f);
    // 白模与启用端版的正式墙共用同一邻接平接规则。前后墙占满整格，左右墙让位。
    // 门窗开口仍位于名义墙中心；端版使用相同300cm基准缩放，不按缩短后的包围盒适配。
    float LeftTrim = 0.0f;
    float RightTrim = 0.0f;
    if (Side == 1 || Side == 3)
    {
        const bool bFrontWall = !Occupied.Contains(Cell + DesertBuildingRules::Sides[0]);
        const bool bBackWall = !Occupied.Contains(Cell + DesertBuildingRules::Sides[2]);
        // 右墙本地+X朝+Y；左墙本地+X朝-Y，因此两端的对应关系相反。
        LeftTrim = ((Side == 1) ? bFrontWall : bBackWall) ? Thickness : 0.0f;
        RightTrim = ((Side == 1) ? bBackWall : bFrontWall) ? Thickness : 0.0f;
    }

    if (Type == EWallType::Door && Cell.Z > 0)
    {
        // 外墙内收后的楼板在门洞处缺少整个墙厚，必须补真正有碰撞的门槛，
        // 不能用暗面遮缝。门槛与本层楼板、相邻露台都以Cell底面为可行走顶面。
        // 整段墙脚承托可兼容不同宽度的自定义门洞；仅向下加厚，不封门或窗。
        const float FloorThickness = FloorHeight * (12.0f / 300.0f);
        const FIntVector Below = Cell - FIntVector(0, 0, 1);
        const FIntVector Terrace = Below + DesertBuildingRules::Sides[Side];
        const bool bHasTerrace = Occupied.Contains(Terrace) &&
            !Occupied.Contains(Terrace + FIntVector(0, 0, 1));
        const bool bInsetTerrace = !RoofModules->GetStaticMesh() ||
            (Style && Style->bInsetCustomRoofToWalls);
        // 有下层邻室时屋顶公共边原本已贯通；只有门房悬挑、邻露台外缘内收
        // 的情况才补露台一侧的20cm。不向没有露台的空中额外伸出平台。
        const float TerraceBridge = bHasTerrace && bInsetTerrace && !Occupied.Contains(Below)
            ? Thickness : 0.0f;
        AddFaceBox(TrimParts, Origin, Rotation,
            FVector((LeftTrim - RightTrim) * 0.5f, -TerraceBridge * 0.5f, -FloorThickness * 0.5f),
            FVector(CellSize - LeftTrim - RightTrim, Thickness + TerraceBridge, FloorThickness));
    }

    UInstancedStaticMeshComponent* Module = Type == EWallType::Door ? DoorWallModules.Get()
        : (Type == EWallType::Window ? WindowWallModules.Get() : SolidWallModules.Get());
    const int32 EndMask = (LeftTrim > 0.0f ? 1 : 0) | (RightTrim > 0.0f ? 2 : 0);
    const int32 CornerMask = Style && Style->bUseWallOuterCornerVariants && Style->bUseWallEndVariants
        ? ResolveWallOuterCornerMask(Cell, Side) : 0;
    if (CornerMask != 0)
    {
        const int32 Index = static_cast<int32>(Type) * 3 + CornerMask - 1;
        UInstancedStaticMeshComponent* CornerModule = WallOuterCornerModules.IsValidIndex(Index)
            ? WallOuterCornerModules[Index].Get() : nullptr;
        if (CornerModule && CornerModule->GetStaticMesh()) Module = CornerModule;
        else ValidationMessages.AddUnique(FString::Printf(
            TEXT("外露墙角软化资产缺失：格(%d,%d,%d)，墙类型%s，朝向%d，外角%s。保留原合法完整墙及平接接口，没有缩放洞口或改用错误角版。"),
            Cell.X, Cell.Y, Cell.Z, Type == EWallType::Solid ? TEXT("实墙") : (Type == EWallType::Window ? TEXT("窗墙") : TEXT("门墙")),
            Side, CornerMask == 1 ? TEXT("左") : (CornerMask == 2 ? TEXT("右") : TEXT("两侧"))));
    }
    if (Style && Style->bUseWallEndVariants && EndMask != 0)
    {
        const int32 Index = static_cast<int32>(Type) * 3 + EndMask - 1;
        Module = WallEndModules.IsValidIndex(Index) ? WallEndModules[Index].Get() : nullptr;
        if (!Module || !Module->GetStaticMesh())
            ValidationMessages.AddUnique(FString::Printf(
                TEXT("自定义墙端资产缺失：格(%d,%d,%d)，墙类型%s，朝向%d，端部%s。已生成按20cm基准平接让位的白模；请补齐Style端版，未使用可能重叠的完整墙。"),
                Cell.X, Cell.Y, Cell.Z, Type == EWallType::Solid ? TEXT("实墙") : (Type == EWallType::Window ? TEXT("窗墙") : TEXT("门墙")),
                Side, EndMask == 1 ? TEXT("左") : (EndMask == 2 ? TEXT("右") : TEXT("两端"))));
    }
    if (Module && Module->GetStaticMesh())
    {
        AddLayerInstance(Module, FTransform(Rotation, Origin,
            FVector(CellSize / 300.0f, CellSize / 300.0f, FloorHeight / 300.0f)));
        return;
    }

    if (Type == EWallType::Solid)
    {
        AddFaceBox(WallParts, Origin, Rotation,
            FVector((LeftTrim - RightTrim) * 0.5f, 0, FloorHeight * 0.5f),
            FVector(CellSize - LeftTrim - RightTrim, Thickness, FloorHeight));
        return;
    }

    const bool bDoor = Type == EWallType::Door;
    const float OpeningWidth = CellSize * (bDoor ? 0.34f : 0.28f);
    const float OpeningBottom = bDoor ? 0.0f : FloorHeight * 0.38f;
    const float OpeningHeight = FloorHeight * (bDoor ? 0.72f : 0.32f);
    const float OpeningTop = OpeningBottom + OpeningHeight;
    const float LeftWidth = (CellSize - OpeningWidth) * 0.5f - LeftTrim;
    const float RightWidth = (CellSize - OpeningWidth) * 0.5f - RightTrim;

    // 门窗不是贴在实墙上的图片：左右墙、过梁、窗下墙围出真正的开口。
    AddFaceBox(WallParts, Origin, Rotation,
        FVector(-(OpeningWidth + LeftWidth) * 0.5f, 0, FloorHeight * 0.5f),
        FVector(LeftWidth, Thickness, FloorHeight));
    AddFaceBox(WallParts, Origin, Rotation,
        FVector((OpeningWidth + RightWidth) * 0.5f, 0, FloorHeight * 0.5f),
        FVector(RightWidth, Thickness, FloorHeight));
    AddFaceBox(WallParts, Origin, Rotation,
        FVector(0, 0, (OpeningTop + FloorHeight) * 0.5f), FVector(OpeningWidth, Thickness, FloorHeight - OpeningTop));
    if (!bDoor)
    {
        AddFaceBox(WallParts, Origin, Rotation,
            FVector(0, 0, OpeningBottom * 0.5f), FVector(OpeningWidth, Thickness, OpeningBottom));
        // 退后到墙内的暗板增加深度，不阻挡门洞，也不作为整面墙的碰撞。
        AddFaceBox(DarkParts, Origin, Rotation,
            FVector(0, Thickness * 1.5f, OpeningBottom + OpeningHeight * 0.5f),
            FVector(OpeningWidth, Thickness * 0.15f, OpeningHeight));
        // 粗糙的石窗台。
        AddFaceBox(TrimParts, Origin, Rotation,
            FVector(0, -Thickness * 0.3f, OpeningBottom - FloorHeight * 0.015f),
            FVector(OpeningWidth + CellSize * 0.06f, Thickness * 1.6f, FloorHeight * 0.03f));
    }
    // 木过梁位于开口上方；并没有把开口填死。
    AddFaceBox(WoodParts, Origin, Rotation,
        FVector(0, -Thickness * 0.1f, OpeningTop + FloorHeight * 0.02f),
        FVector(OpeningWidth + CellSize * 0.08f, Thickness * 1.15f, FloorHeight * 0.04f));
}

void ADesertBuilding::BuildRoof(const FIntVector& Cell)
{
    ++RoofCount;
    const FVector Base = CellBottomCenter(Cell);
    const float RoofThickness = FloorHeight * (16.0f / 300.0f);
    const float WallThickness = CellSize * (20.0f / 300.0f);
    const float ParapetHeight = FloorHeight * (45.0f / 300.0f);
    const float TopZ = Base.Z + FloorHeight;
    if (RoofModules->GetStaticMesh())
    {
        // 旧Style保留完整板。新基础板明确选择邻接内收，避免外侧与外墙同面。
        FVector RoofOrigin(Base.X, Base.Y, TopZ - RoofThickness);
        FVector RoofSize(CellSize, CellSize, RoofThickness);
        if (Style && Style->bInsetCustomRoofToWalls)
            DesertBuildingRules::InsetSlabToWalls(Occupied, Cell, CellSize, WallThickness, RoofOrigin, RoofSize);
        AddLayerInstance(RoofModules, FTransform(FQuat::Identity,
            RoofOrigin, FVector(RoofSize.X / 300.0f, RoofSize.Y / 300.0f, FloorHeight / 300.0f)));
    }
    else
    {
        FVector RoofCenter(Base.X, Base.Y, TopZ - RoofThickness * 0.5f);
        FVector RoofSize(CellSize, CellSize, RoofThickness);
        DesertBuildingRules::InsetSlabToWalls(Occupied, Cell, CellSize, WallThickness, RoofCenter, RoofSize);
        AddBox(TrimParts, RoofCenter, RoofSize);
    }

    BuildRoofDressing(Cell);
    // 合法整墙冠已经拥有这个屋面的围边；保留楼板，但不再叠一圈普通女儿墙。
    // 无效/缺模型的输入不会进入该集合，因此不会删掉旧正常屋面围边。
    if (RoofCrownRoofCells.Contains(Cell)) return;
    bool Exposed[4];
    for (int32 Side = 0; Side < 4; ++Side)
    {
        Exposed[Side] = !Occupied.Contains(Cell + DesertBuildingRules::Sides[Side]);
    }
    for (int32 Side = 0; Side < 4; ++Side)
    {
        if (!Exposed[Side]) continue;
        FVector Origin = FaceOrigin(Cell, Side);
        Origin.Z = TopZ;
        const FQuat Rotation = DesertBuildingRules::SideRotation(Side);
        float Length = CellSize;
        // 前后段贯通，左右段在角部让位，避免重叠顶面和闪烁。
        if (Side == 1 || Side == 3)
        {
            const float FrontTrim = Exposed[0] ? WallThickness : 0.0f;
            const float BackTrim = Exposed[2] ? WallThickness : 0.0f;
            Length -= FrontTrim + BackTrim;
            Origin.Y += (FrontTrim - BackTrim) * 0.5f;
        }
        auto AddParapetSegment = [&](float Min, float Max)
        {
            const float SegmentLength = Max - Min;
            if (SegmentLength <= KINDA_SMALL_NUMBER) return;
            ++ParapetCount;
            const FVector SegmentOrigin = Origin + Rotation.RotateVector(FVector((Min + Max) * 0.5f,0,0));
            if (ParapetModules->GetStaticMesh())
                AddLayerInstance(ParapetModules, FTransform(Rotation, SegmentOrigin,
                    FVector(SegmentLength / 300.0f, CellSize / 300.0f, FloorHeight / 300.0f)));
            else
                AddFaceBox(TrimParts, SegmentOrigin, Rotation, FVector(0,0,ParapetHeight * 0.5f),
                    FVector(SegmentLength,WallThickness,ParapetHeight));
        };
        float GapWidth = 0.0f;
        for (const FDesertRoofOpening& Opening : EffectiveRoofOpenings)
            if (Opening.Cell == Cell && Opening.Side == Side && FMath::IsFinite(Opening.Width))
                GapWidth = FMath::Max(GapWidth, FMath::Clamp(Opening.Width, 0.0f, CellSize));
        if (GapWidth > 0.0f)
        {
            // 转角收边可能使矮墙中心移动，但通行口始终对准格子边的原始中心。
            const float GapCenter = Rotation.UnrotateVector(FaceOrigin(Cell, Side) - Origin).X;
            AddParapetSegment(-Length * 0.5f, FMath::Clamp(GapCenter - GapWidth * 0.5f, -Length * 0.5f, Length * 0.5f));
            AddParapetSegment(FMath::Clamp(GapCenter + GapWidth * 0.5f, -Length * 0.5f, Length * 0.5f), Length * 0.5f);
        }
        else AddParapetSegment(-Length * 0.5f, Length * 0.5f);
        if (bShowWoodBeams)
        {
            // 默认资产之外的可关装饰。换成自带梁头的模块时可关闭该选项。
            FVector BeamOrigin = FaceOrigin(Cell, Side);
            for (int32 Beam = -1; Beam <= 1; ++Beam)
            {
                AddFaceBox(WoodParts, BeamOrigin, Rotation,
                    FVector(Beam * CellSize * 0.28f, -WallThickness * 0.8f, FloorHeight * 0.87f),
                    FVector(CellSize * 0.04f, CellSize * 0.15f, FloorHeight * 0.04f));
            }
        }
    }
}

void ADesertBuilding::BuildRoofDressing(const FIntVector& Cell)
{
    // A manual layout supersedes automatic dressing on this roof only.
    for(const FDesertRoofDecoration& Decoration:RoofDecorations)
        if(Decoration.bEnabled && Decoration.Cell==Cell+FIntVector(0,0,1)) return;
    if(!Style || !Style->bEnableRoofDressing || Occupied.Contains(Cell+FIntVector(0,0,1)) ||
        RoofDressingBlockedCells.Contains(Cell)) return;
    for(const FDesertRoofOpening& Opening:EffectiveRoofOpenings) if(Opening.Cell==Cell) return;
    const int32 Index=Style->ResolveRoofDressingVariantIndex(Cell,Seed);
    UStaticMesh* Mesh=Style->RoofDressingVariants.IsValidIndex(Index)?Style->RoofDressingVariants[Index].Get():nullptr;
    if(!Mesh || !RoofDressingModules.IsValidIndex(Index))
    {ValidationMessages.AddUnique(TEXT("已启用屋顶装饰，但指定/随机资源为空或下标越界；该格保留原屋顶，没有换用别件。"));return;}
    const FBox Bounds=Mesh->GetBoundingBox();
    if(!Bounds.IsValid || Bounds.Min.Z < -0.5f || Bounds.Max.Z>100.5f ||
        Bounds.Min.X < -120.5f || Bounds.Max.X>120.5f || Bounds.Min.Y < -120.5f || Bounds.Max.Y>120.5f)
    {ValidationMessages.AddUnique(TEXT("屋顶装饰模型需底心Z0、XY±120cm内且高≤100cm；当前资源超出安全范围，保留原屋顶。"));return;}
    FVector Position=CellBottomCenter(Cell);Position.Z+=FloorHeight+0.3f*(FloorHeight/300.f);
    AddLayerInstance(RoofDressingModules[Index],FTransform(FQuat::Identity,Position,
        FVector(CellSize/300.f,CellSize/300.f,FloorHeight/300.f)));
}

void ADesertBuilding::BuildAwning(const FIntVector& DoorCell, int32 DoorSide)
{
    // 门前整个网格是最保守的占地测试。暂不处理任意形状碰撞或角色通行寻路。
    if (DoorSide<0 || DoorSide>3) return;
    const FIntVector FrontCell = DoorCell + DesertBuildingRules::Sides[DoorSide];
    if (!bEnableAwning || Occupied.Contains(FrontCell) || Occupied.Contains(FrontCell + FIntVector(0, 0, 1))) return;
    ++AwningCount;
    const float HorizontalScale = CellSize / 300.0f;
    const float VerticalScale = FloorHeight / 300.0f;
    const FQuat Rotation=DesertBuildingRules::SideRotation(DoorSide);
    const FIntVector Direction=DesertBuildingRules::Sides[DoorSide];
    FVector Origin = CellBottomCenter(DoorCell) + FVector(Direction.X,Direction.Y,0)*CellSize*.5f;
    if (AwningModules->GetStaticMesh())
    {
        AddLayerInstance(AwningModules, FTransform(Rotation, Origin,
            FVector(HorizontalScale, HorizontalScale, VerticalScale)));
        return;
    }

    const float Width = 240.0f * HorizontalScale;
    const float Depth = 180.0f * HorizontalScale;
    const float BackHeight = 250.0f * VerticalScale;
    const float FrontHeight = 230.0f * VerticalScale;
    const float Drop = BackHeight - FrontHeight;
    const float SlopedLength = FMath::Sqrt(Depth * Depth + Drop * Drop);
    const FQuat Slope=Rotation*FQuat(FVector::XAxisVector, FMath::Atan2(Drop, Depth));
    const FVector CanopyCenter = Origin + Rotation.RotateVector(FVector(0, -Depth * 0.5f, (BackHeight + FrontHeight) * 0.5f));
    AddBox(ClothParts, CanopyCenter, FVector(Width, SlopedLength, 4.0f * VerticalScale), Slope);

    // 前侧两根柱子，横梁和沿坡度的两根边梁；都是可替换美术之前的结构白模。
    const float PostSize = 8.0f * HorizontalScale;
    for (const float Sign : {-1.0f, 1.0f})
    {
        AddBox(WoodParts, Origin + Rotation.RotateVector(FVector(Sign * (Width * 0.5f - PostSize), -Depth + PostSize,
            FrontHeight * 0.5f)), FVector(PostSize, PostSize, FrontHeight), Rotation);
        AddBox(WoodParts, CanopyCenter + Rotation.RotateVector(FVector(Sign * (Width * 0.5f - PostSize), 0, -6.0f * VerticalScale)),
            FVector(PostSize, SlopedLength, 8.0f * VerticalScale), Slope);
    }
    AddBox(WoodParts, Origin + Rotation.RotateVector(FVector(0, -Depth + PostSize, FrontHeight - 4.0f * VerticalScale)),
        FVector(Width, PostSize, 8.0f * VerticalScale), Rotation);
    AddBox(WoodParts, Origin + Rotation.RotateVector(FVector(0, -PostSize * 0.5f, BackHeight - 4.0f * VerticalScale)),
        FVector(Width, PostSize, 8.0f * VerticalScale), Rotation);
}

void ADesertBuilding::BuildFoundation(const FIntVector& Cell)
{
    const FVector TopCenter = CellBottomCenter(Cell);
    if (FoundationModules->GetStaticMesh())
    {
        // 自定义地基枢轴在顶面中心，参考高度100厘米且全部位于Z负半轴。
        FoundationModules->AddInstance(FTransform(FQuat::Identity, TopCenter,
            FVector(CellSize / 300.0f, CellSize / 300.0f, FoundationDepth / 100.0f)), false);
    }
    else
    {
        AddBox(FoundationParts, TopCenter - FVector(0, 0, FoundationDepth * 0.5f),
            FVector(CellSize, CellSize, FoundationDepth));
    }
}

void ADesertBuilding::RebuildRoomPlacementPreview(FIntVector Cell)
{
    RoomPlacementPreviewCell=Cell;
    ON_SCOPE_EXIT { RoomPlacementPreviewCell.Reset(); };
    Rebuild();
}

bool ADesertBuilding::ResolveEntranceCandidate(const TSet<FIntVector>& Bodies,
    const TArray<FBox>& Stairs, FIntVector& OutCell, int32& OutSide, FString* OverrideError) const
{
    OutCell=FIntVector::ZeroValue; OutSide=-1;
    if (OverrideError) OverrideError->Reset();
    TArray<FIntVector> ExplicitOrder=Bodies.Array();
    ExplicitOrder.Sort([](const FIntVector& A,const FIntVector& B){ return A.Y!=B.Y ? A.Y<B.Y : A.X<B.X; });
    for (const FIntVector& Cell : ExplicitOrder)
    {
        const FDesertRoomAppearance Appearance=GetRoomAppearance(Cell);
        FString Error;
        if (Cell.Z==0 && Appearance.DoorMode>=EDesertRoomDoorMode::Front && ValidateRoomAppearance(Appearance,Bodies,Stairs,Error))
        { OutCell=Cell; OutSide=static_cast<int32>(Appearance.DoorMode)-2; return true; }
    }
    if (bUseEntranceOverride)
    {
        auto Reject=[OverrideError](const TCHAR* Reason)
        { if (OverrideError) *OverrideError=Reason; return false; };
        if (GetRoomAppearance(EntranceOverrideCell).DoorMode!=EDesertRoomDoorMode::Automatic)
            return Reject(TEXT("该房间的逐房门配置已覆盖Actor入口；没有自动换到其他墙面"));
        if (EntranceOverrideSide<0 || EntranceOverrideSide>3)
            return Reject(TEXT("人工入口方向必须为0前-Y、1右+X、2后+Y或3左-X"));
        if (EntranceOverrideCell.Z!=0 || !Bodies.Contains(EntranceOverrideCell))
            return Reject(TEXT("人工入口必须选择有有效首层房间的Z=0格子"));
        const FIntVector Direction=DesertBuildingRules::Sides[EntranceOverrideSide];
        if (Bodies.Contains(EntranceOverrideCell+Direction))
            return Reject(TEXT("人工入口指定的是房间之间的内墙；请选择外露墙面"));
        const FVector Edge=CellBottomCenter(EntranceOverrideCell)+FVector(Direction.X,Direction.Y,0)*CellSize*.5f;
        const FBox Entry=FBox(FVector(-CellSize*.2f,-80,0),FVector(CellSize*.2f,0,220))
            .TransformBy(FTransform(FRotator(0,EntranceOverrideSide*90,0),Edge)).ExpandBy(-.5f);
        for (const FBox& Stair : Stairs)
            if (Entry.Intersect(Stair)) return Reject(TEXT("人工入口门前通路被楼梯实际占用或头顶预约空间遮挡"));
        OutCell=EntranceOverrideCell; OutSide=EntranceOverrideSide;
        return true;
    }
    TArray<FIntVector> Order = Bodies.Array();
    Order.Sort([](const FIntVector& A, const FIntVector& B)
    { return A.Z!=B.Z ? A.Z<B.Z : (A.Y!=B.Y ? A.Y<B.Y : A.X<B.X); });
    bool bFound=false;
    // 与原Rebuild入口策略相同；新散石规则复用它，不使用上一轮缓存的EntranceCell。
    for (int32 Side=0; Side<4 && !bFound; ++Side)
        for (const FIntVector& Cell : Order)
        {
            if (Cell.Z!=0 || GetRoomAppearance(Cell).DoorMode!=EDesertRoomDoorMode::Automatic || Bodies.Contains(Cell+DesertBuildingRules::Sides[Side])) continue;
            const FIntVector Direction=DesertBuildingRules::Sides[Side];
            const FVector Edge=CellBottomCenter(Cell)+FVector(Direction.X,Direction.Y,0)*CellSize*.5f;
            const FBox LocalEntry(FVector(-CellSize*.2f,-80,0),FVector(CellSize*.2f,0,220));
            const FBox Entry=LocalEntry.TransformBy(FTransform(FRotator(0,Side*90,0),Edge)).ExpandBy(-.5f);
            bool bBlocked=false;
            for (const FBox& Stair : Stairs) if (Entry.Intersect(Stair)) { bBlocked=true; break; }
            if (bBlocked) continue;
            if (!bFound || Cell.Y<OutCell.Y || (Cell.Y==OutCell.Y && FMath::Abs(Cell.X)<FMath::Abs(OutCell.X)))
            { OutCell=Cell; OutSide=Side; bFound=true; }
        }
    return bFound;
}

void ADesertBuilding::Rebuild()
{
    if (!WallParts || !CubeMesh) return;
    CellSize = FMath::IsFinite(CellSize) ? FMath::Clamp(CellSize, 100.0f, 1000.0f) : 300.0f;
    FloorHeight = FMath::IsFinite(FloorHeight) ? FMath::Clamp(FloorHeight, 100.0f, 1000.0f) : 300.0f;
    FoundationDepth = FMath::IsFinite(FoundationDepth) ? FMath::Clamp(FoundationDepth, 10.0f, 10000.0f) : 80.0f;
    NormalizeCells();
    ValidationMessages.Reset();
    SupportTraceDistance = FMath::IsFinite(SupportTraceDistance) ? FMath::Clamp(SupportTraceDistance,100.0f,50000.0f) : 10000.0f;
    PrepareSupportedCells();
    ConfigureMeshesAndMaterials();
    ConfigureBlockParts();
    // 前18个附件桶是程序白模；其余才是Style自定义整模块。
    for (int32 Index = 18; Index < BlockParts.Num(); ++Index)
        DesertBuildingRules::ApplyModuleMaterialOverrides(Style, BlockParts[Index]);
    for (UInstancedStaticMeshComponent* Component : AwningVariantModules)
        DesertBuildingRules::ApplyModuleMaterialOverrides(Style, Component);
    for(UInstancedStaticMeshComponent* Component:Art09AdditionalTemplates)
        DesertBuildingRules::ApplyModuleMaterialOverrides(Style,Component);
    if (WallOuterCornerVariantPrefix == INDEX_NONE)
        WallOuterCornerVariantPrefix = AwningVariantModules.Num();
    if (StairTemplateVariantPrefix==INDEX_NONE) StairTemplateVariantPrefix=AwningVariantModules.Num();
    if(Art09TemplateVariantPrefix==INDEX_NONE) Art09TemplateVariantPrefix=AwningVariantModules.Num();
    PrepareLayerComponents();
    ResolveAndBuildBlocks();
    ResolveInvalidRoomAppearances(Occupied,StairReservations,InvalidRoomAppearanceIndices,ValidationMessages);
    TArray<FDesertDoorFace> DoorFaces; ResolveDoorFaces(Occupied,StairReservations,DoorFaces);
    // 高层门前的同高露台必须在造屋顶前预留入口。尤其悬挑房间下方为空时，
    // 露台这一边仍被视为外露边，若不预留就会把女儿墙横在门槛外。
    // 仅使用已通过规则的门面；窗和无门墙面不改变。显式开口仍由BuildRoof取最大宽度。
    for (const FDesertDoorFace& Door : DoorFaces)
    {
        if (Door.Cell.Z <= 0 || Door.Side < 0 || Door.Side > 3) continue;
        const FIntVector Terrace = Door.Cell + DesertBuildingRules::Sides[Door.Side] - FIntVector(0, 0, 1);
        if (!Occupied.Contains(Terrace) || Occupied.Contains(Terrace + FIntVector(0, 0, 1))) continue;
        FDesertRoofOpening& Opening = EffectiveRoofOpenings.AddDefaulted_GetRef();
        Opening.Cell = Terrace;
        Opening.Side = (Door.Side + 2) % 4;
        Opening.Width = CellSize * 0.6f;
        RoofDressingBlockedCells.Add(Terrace);
    }
    CellCount = Occupied.Num();
    VisibleWallCount = RoofCount = ParapetCount = 0;
    AwningCount = 0;

    // 优先正面入口，但沿墙楼梯不能把唯一入口封住；必要时尝试其他立面。
    FIntVector DoorCell = FIntVector::ZeroValue;
    int32 DoorSide=-1;
    EntranceCell=FIntVector::ZeroValue;
    EntranceSide=-1;
    FString EntranceOverrideError;
    const bool bHasDoor=ResolveEntranceCandidate(Occupied,StairReservations,DoorCell,DoorSide,&EntranceOverrideError);
    if (bHasDoor) { EntranceCell=DoorCell; EntranceSide=DoorSide; }
    else if (bUseEntranceOverride) ValidationMessages.Add(TEXT("人工入口未生成：")+EntranceOverrideError+TEXT("。保留指定设置，未自动改用其他墙面。"));
    else if (CellCount>0) ValidationMessages.Add(TEXT("当前建筑未生成首层入口：检查无门设置、内墙朝向或楼梯通路。"));

    for (const FIntVector& Cell : NormalizedCells)
    {
        if (!Occupied.Contains(Cell)) continue;
        // 入口和邻接已用完整Occupied求解；输出只归属于待放置房格。
        // 包括自定义地基的直接AddInstance，避免旧地基也出现在绿色预览中。
        if (RoomPlacementPreviewCell.IsSet() && Cell!=RoomPlacementPreviewCell.GetValue()) continue;
        GenerationFloorIndex = Cell.Z + 1;
        const FVector Base = CellBottomCenter(Cell);
        // 地基使用独立组件，固定顶面Z=0向下延伸；上层楼板继续参与整栋淡化。
        if (Cell.Z == 0)
        {
            BuildFoundation(Cell);
        }
        if (RoomCellModules->GetStaticMesh())
        {
            const FBox Bounds = RoomCellModules->GetStaticMesh()->GetBoundingBox();
            const FVector SourceSize = Bounds.GetSize();
            const bool bValidBounds = Bounds.IsValid && !SourceSize.ContainsNaN() && SourceSize.GetMin() > KINDA_SMALL_NUMBER;
            if (bValidBounds)
            {
                const bool bFit = !Style || Style->bFitRoomCellMeshToGrid;
                const FVector Scale = bFit ? FVector(CellSize, CellSize, FloorHeight) / SourceSize :
                    FVector(CellSize / 300.0f, CellSize / 300.0f, FloorHeight / 300.0f);
                const FVector RoomPivotCorrection = bFit ? FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z) * Scale : FVector::ZeroVector;
                AddLayerInstance(RoomCellModules, FTransform(FQuat::Identity, Base - RoomPivotCorrection, Scale));
                if (!Occupied.Contains(Cell + FIntVector(0,0,1))) {++RoofCount;BuildRoofDressing(Cell);}
                continue;
            }
            ValidationMessages.AddUnique(TEXT("整房间模型包围盒为空或尺寸无效，暂用程序白模；请检查导入模型。"));
        }
        if (Cell.Z > 0)
        {
            const float FloorThickness = FloorHeight * (12.0f / 300.0f);
            // 上层房间地板与同高露台齐平；厚度向下，避免门内凭空抬高12cm。
            FVector FloorCenter = Base - FVector(0, 0, FloorThickness * 0.5f);
            FVector FloorSize(CellSize, CellSize, FloorThickness);
            const float WallThickness = CellSize * (20.0f / 300.0f);
            DesertBuildingRules::InsetSlabToWalls(Occupied, Cell, CellSize, WallThickness, FloorCenter, FloorSize);
            AddBox(TrimParts, FloorCenter, FloorSize);
        }

        for (int32 Side = 0; Side < 4; ++Side)
        {
            if (Occupied.Contains(Cell + DesertBuildingRules::Sides[Side])) continue;
            ++VisibleWallCount;
            const bool bDoor = DoorFaces.ContainsByPredicate([&Cell,Side](const FDesertDoorFace& Face){ return Face.Cell==Cell && Face.Side==Side; });
            const FDesertRoomAppearance Appearance=GetRoomAppearance(Cell);
            FString AppearanceError;
            const bool bValidAppearance=ValidateRoomAppearance(Appearance,Occupied,StairReservations,AppearanceError);
            const bool bWindow = bValidAppearance && Appearance.bOverrideWindows ? (Appearance.WindowMask & (1<<Side))!=0 :
                DesertBuildingRules::StableHash(Cell, Side, Seed) % 5u != 0u;
            BuildWall(Cell, Side, bDoor ? EWallType::Door : (bWindow ? EWallType::Window : EWallType::Solid));
        }
        if (!Occupied.Contains(Cell + FIntVector(0, 0, 1)))
        {
            BuildRoof(Cell);
        }
    }
    GenerationFloorIndex = 1;
    BuildManualRoofDecorations();
    GenerationFloorIndex = 1;
    if (RoomCellModules->GetStaticMesh() && !EffectiveRoofOpenings.IsEmpty())
        ValidationMessages.Add(TEXT("使用整房间模型时门窗与屋面入口由模型提供；程序楼梯仍检查占地，但不能自动切开模型里的女儿墙，请准备带入口的房间模型。"));
    if (!RoomPlacementPreviewCell.IsSet() && bHasDoor && Blocks.IsEmpty()) BuildAwning(DoorCell,DoorSide);
    else if (bEnableAwning && !Blocks.IsEmpty()) ValidationMessages.Add(TEXT("已有组合块时停用V1自动门棚；请使用AwningBay组合块，避免与新块规则产生穿插。"));
    // 满显示的编辑状态保留持久材质资产引用，避免关卡中无谓保存动态实例。
    ApplyOcclusionMaterials();
    UpdateBuildingMaterialCoordinates();
}
