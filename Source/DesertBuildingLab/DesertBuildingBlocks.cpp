#include "DesertBuilding.h"
#include "DesertBuildingStyle.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace DesertBlockRules
{
    static const FIntVector Directions[4] = {FIntVector(0,-1,0),FIntVector(1,0,0),FIntVector(0,1,0),FIntVector(-1,0,0)};
    static EDesertModuleKind Kind(EDesertBlockType Type)
    {
        switch(Type)
        {
        case EDesertBlockType::RoofPavilion: return EDesertModuleKind::RoofPavilion;
        case EDesertBlockType::Dome: return EDesertModuleKind::Dome;
        case EDesertBlockType::AwningBay: return EDesertModuleKind::AwningBay;
        case EDesertBlockType::Stairs: return EDesertModuleKind::Stairs;
        case EDesertBlockType::RubbleCluster: return EDesertModuleKind::RubbleCluster;
        case EDesertBlockType::RoofCrown: return EDesertModuleKind::RoofCrown;
        default: return EDesertModuleKind::PotCluster;
        }
    }
}

void ADesertBuilding::AddSelectedBlock()
{
    TryAddPlacement(false, SelectedBlock);
}

void ADesertBuilding::RemoveLastBlock()
{
    if (Blocks.IsEmpty()) return;
    const FDesertBlockPlacement Proposal=Blocks.Last();
    RemovePlacementAndDependents(false,Proposal);
}

void ADesertBuilding::LoadRuleDemo()
{
    Modify();
    Cells = {FIntVector(-1,-1,0),FIntVector(0,-1,0),FIntVector(1,-1,0),
        FIntVector(-1,0,0),FIntVector(0,0,0),FIntVector(1,0,0),FIntVector(1,0,1),FIntVector(-2,0,1)};
    RoomAppearanceOverrides.Reset();
    bAutoSupportColumns = true;
    bEnableAwning = false; // 棚架现在由有明确放置规则的AwningBay组合块管理。
    RoofOpenings.Reset();
    Blocks.Reset();
    auto Add = [this](EDesertBlockType Type,FIntVector Cell,int32 Facing=0)
    {
        FDesertBlockPlacement& Block = Blocks.AddDefaulted_GetRef();
        Block.Type=Type; Block.Cell=Cell; Block.Facing=Facing;
    };
    Add(EDesertBlockType::RoofPavilion,FIntVector(1,0,2));
    Add(EDesertBlockType::Dome,FIntVector(1,0,3));
    Add(EDesertBlockType::PotCluster,FIntVector(0,0,1));
    Add(EDesertBlockType::AwningBay,FIntVector(2,-1,0),1);
    Add(EDesertBlockType::PotCluster,FIntVector(2,0,0));
    Add(EDesertBlockType::Stairs,FIntVector(1,-1,1),0);
    Rebuild();
}

bool ADesertBuilding::TraceGround(const FVector& LocalTop, float& LocalGroundZ) const
{
    UWorld* World = GetWorld();
    if (!World) return false;
    const FVector Start = GetActorTransform().TransformPosition(LocalTop);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DesertSupport), true, this);
    TArray<AActor*> AttachedActors;
    GetAttachedActors(AttachedActors,true,true);
    Query.AddIgnoredActors(AttachedActors);
    FHitResult Hit;
    if (!World->LineTraceSingleByChannel(Hit,Start,Start-FVector(0,0,SupportTraceDistance),ECC_Visibility,Query)) return false;
    // 只接受可承托的朝上表面，墙的侧面不是地面。
    if (Hit.ImpactNormal.Z < 0.5f) return false;
    LocalGroundZ = GetActorTransform().InverseTransformPosition(Hit.ImpactPoint).Z;
    return FMath::IsFinite(LocalGroundZ);
}

void ADesertBuilding::ResolveSupportedCells(const TArray<FIntVector>& Inputs,
    TSet<FIntVector>& OutOccupied, TArray<FDesertSupportSpan>& OutSupports,
    TArray<int32>& OutInvalidIndices, TArray<FString>& OutMessages) const
{
    OutSupports.Reset(); OutOccupied.Reset(); OutInvalidIndices.Reset();
    TArray<int32> Order;
    for (int32 Index=0; Index<Inputs.Num(); ++Index) Order.Add(Index);
    Order.StableSort([&Inputs](int32 A,int32 B)
    {
        if (Inputs[A].Z!=Inputs[B].Z) return Inputs[A].Z<Inputs[B].Z;
        if (Inputs[A].Y!=Inputs[B].Y) return Inputs[A].Y<Inputs[B].Y;
        return Inputs[A].X<Inputs[B].X;
    });
    TSet<FIntVector> Seen;
    // 只对输入索引排序，下层有效后才能作为上层支撑；原始数组保持不变。
    const bool bUpright = GetActorUpVector().Equals(FVector::UpVector,0.0001f);
    for (int32 Index : Order)
    {
        const FIntVector& Cell=Inputs[Index];
        if (!IsCellInBounds(Cell) || Seen.Contains(Cell))
        {
            OutInvalidIndices.AddUnique(Index);
            OutMessages.Add(FString::Printf(TEXT("Cells[%d] %s 坐标越界或重复；保留输入，暂不生成。"),Index,*Cell.ToString()));
            continue;
        }
        Seen.Add(Cell);
        if (OutOccupied.Contains(Cell-FIntVector(0,0,1)) || (Cell.Z==0 && !bAutoSupportColumns)) { OutOccupied.Add(Cell); continue; }
        if (!bAutoSupportColumns)
        {
            OutInvalidIndices.AddUnique(Index);
            OutMessages.Add(FString::Printf(TEXT("体块 %s 缺少下层支撑且自动支柱已关闭：保留输入，暂不生成。"),*Cell.ToString()));
            continue;
        }
        if (!bUpright)
        {
            OutInvalidIndices.AddUnique(Index);
            OutMessages.Add(FString::Printf(TEXT("体块 %s 无效：自动支撑要求建筑竖直，只允许绕Z旋转。"),*Cell.ToString()));
            continue;
        }
        float LowerRoof=-BIG_NUMBER;
        for (int32 Z=Cell.Z-2; Z>=0; --Z)
            if (OutOccupied.Contains(FIntVector(Cell.X,Cell.Y,Z))) { LowerRoof=(Z+1)*FloorHeight; break; }
        TArray<FDesertSupportSpan> Pending;
        const FVector Center=CellBottomCenter(Cell);
        const float Corner=CellSize*0.34f;
        bool Valid=true;
        for (float X : {-Corner,Corner}) for (float Y : {-Corner,Corner})
        {
            FVector Top=Center+FVector(X,Y,0);
            float BottomZ=LowerRoof;
            const FVector TraceStart=Top+FVector(0,0,Cell.Z==0 ? 30.0f : -1.0f);
            if (LowerRoof == -BIG_NUMBER && !TraceGround(TraceStart,BottomZ)) { Valid=false; continue; }
            if (Cell.Z==0)
            {
                // 首层的地基已覆盖这段高度；只有地基底仍离地时，才向下接柱。
                Top.Z-=FoundationDepth;
                if (BottomZ>=Top.Z-1) { Pending.Add({Top,Top}); continue; }
            }
            if (BottomZ>=Top.Z-1 || Top.Z-BottomZ>5000) { Valid=false; continue; }
            Pending.Add({FVector(Top.X,Top.Y,BottomZ),Top});
        }
        if (!Valid || Pending.Num()!=4)
        {
            OutInvalidIndices.AddUnique(Index);
            OutMessages.Add(FString::Printf(TEXT("体块 %s 无效：四角未全部找到有效支撑表面（最大柱高5000cm）。已保留输入，暂不生成。"),*Cell.ToString()));
            continue;
        }
        for (const FDesertSupportSpan& Span : Pending)
            if (Span.Top.Z>Span.Bottom.Z+1) OutSupports.Add(Span);
        OutOccupied.Add(Cell);
    }
}

void ADesertBuilding::PrepareSupportedCells()
{
    ResolveSupportedCells(Cells,Occupied,SupportSpans,InvalidCellIndices,ValidationMessages);
    SupportColumnCount=SupportSpans.Num();
}

void ADesertBuilding::ConfigureBlockParts()
{
    UMaterialInterface* Materials[6] = {
        Style && Style->WallMaterial ? Style->WallMaterial.Get() : DefaultBaseMaterial.Get(),
        Style && Style->TrimMaterial ? Style->TrimMaterial.Get() : DefaultBaseMaterial.Get(),
        Style && Style->WoodMaterial ? Style->WoodMaterial.Get() : DefaultBaseMaterial.Get(),
        Style && Style->ClothMaterial ? Style->ClothMaterial.Get() : DefaultBaseMaterial.Get(),
        Style && Style->DarkMaterial ? Style->DarkMaterial.Get() : DefaultBaseMaterial.Get(),
        Style && Style->PotMaterial ? Style->PotMaterial.Get() : DefaultBaseMaterial.Get()
    };
    for (int32 Index=0; Index<BlockParts.Num(); ++Index)
    {
        UInstancedStaticMeshComponent* Parts=BlockParts[Index];
        Parts->ClearInstances();
        Parts->EmptyOverrideMaterials();
        if (Index<18) Parts->SetMaterial(0,Materials[Index%6]);
        else Parts->SetStaticMesh(ADesertBuildingModule::GetStyleMesh(Style,static_cast<EDesertModuleKind>(Index-18)));
    }
    const int32 RequiredVariants=Style ? Style->ModuleAwningBayVariants.Num() : 0;
    while (AwningVariantModules.Num()<RequiredVariants)
    {
        const int32 Index=AwningVariantModules.Num();
        UInstancedStaticMeshComponent* Parts=NewObject<UInstancedStaticMeshComponent>(this,
            FName(*FString::Printf(TEXT("AwningBayVariant_%02d"),Index)),RF_Transactional);
        Parts->CreationMethod=EComponentCreationMethod::Instance;
        AddInstanceComponent(Parts);
        Parts->SetMobility(EComponentMobility::Movable);
        Parts->SetupAttachment(FirstFloorRoot);
        // 保留既有AwningBay组合的碰撞策略；独立桶避免不同变体互相覆盖网格。
        Parts->SetCollisionEnabled(BlockParts[26]->GetCollisionEnabled());
        Parts->SetCollisionResponseToChannels(BlockParts[26]->GetCollisionResponseToChannels());
        Parts->SetGenerateOverlapEvents(false);
        Parts->SetCanEverAffectNavigation(false);
        Parts->ComponentTags.Add(TEXT("DesertFloor_1"));
        AwningVariantModules.Add(Parts);
        if(Art09TemplateVariantPrefix!=INDEX_NONE && Index>=Art09TemplateVariantPrefix)
            Art09AdditionalTemplates.Add(Parts);
        if (GetWorld()) Parts->RegisterComponent();
    }
    for (int32 Index=0;Index<AwningVariantModules.Num();++Index)
    {
        UInstancedStaticMeshComponent* Parts=AwningVariantModules[Index];
        Parts->ClearInstances();
        Parts->EmptyOverrideMaterials();
        Parts->SetStaticMesh(Index<RequiredVariants ? Style->ModuleAwningBayVariants[Index].Get() : nullptr);
    }
    auto ConfigureExtraVariants=[this](TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Buckets,
        const TArray<TObjectPtr<UStaticMesh>>& Meshes,const TCHAR* Prefix)
    {
        while(Buckets.Num()<Meshes.Num())
        {
            UInstancedStaticMeshComponent* Parts=NewObject<UInstancedStaticMeshComponent>(this,
                FName(*FString::Printf(TEXT("%s_%02d"),Prefix,Buckets.Num())),RF_Transactional);
            Parts->CreationMethod=EComponentCreationMethod::Instance; AddInstanceComponent(Parts);
            Parts->SetMobility(EComponentMobility::Movable); Parts->SetupAttachment(FirstFloorRoot);
            Parts->SetCollisionEnabled(ECollisionEnabled::NoCollision);Parts->SetCollisionResponseToAllChannels(ECR_Ignore);
            Parts->SetGenerateOverlapEvents(false);Parts->SetCanEverAffectNavigation(false);
            Parts->ComponentTags.Add(TEXT("DesertFloor_1"));Buckets.Add(Parts);Art09AdditionalTemplates.Add(Parts);
            if(GetWorld()) Parts->RegisterComponent();
        }
        for(int32 Index=0;Index<Buckets.Num();++Index)
        {
            Buckets[Index]->ClearInstances();Buckets[Index]->EmptyOverrideMaterials();
            Buckets[Index]->SetStaticMesh(Meshes.IsValidIndex(Index)?Meshes[Index].Get():nullptr);
        }
    };
    const TArray<TObjectPtr<UStaticMesh>> EmptyMeshes;
    ConfigureExtraVariants(PotVariantModules,Style?Style->ModulePotClusterVariants:EmptyMeshes,TEXT("PotClusterVariant"));
    ConfigureExtraVariants(RoofDressingModules,Style?Style->RoofDressingVariants:EmptyMeshes,TEXT("RoofDressingVariant"));
}

void ADesertBuilding::EmitModule(EDesertModuleKind Kind,const FTransform& Placement,const FVector& Dimensions,int32 Steps,bool WithProps,
    UStaticMesh* ResolvedMesh,int32 AwningVariantIndex)
{
    // 房间悬浮预览不重画已放附件或旧支柱。新支柱由EvaluatePlacement返回给Designer。
    if (RoomPlacementPreviewCell.IsSet()) return;
    if (Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs)
    {
        UInstancedStaticMeshComponent* Bucket=Kind==EDesertModuleKind::LShapeStairs ? LShapeStairModules.Get() : UShapeStairModules.Get();
        if (Bucket->GetStaticMesh())
        { AddLayerInstance(Bucket,FTransform(FQuat::Identity,FVector::ZeroVector,Dimensions/ADesertBuildingModule::GetDefaultDimensions(Kind))*Placement); return; }
    }
    if (Kind==EDesertModuleKind::RoofCrown)
    {
        if (ResolvedMesh && RoofCrownModules->GetStaticMesh()==ResolvedMesh)
            AddLayerInstance(RoofCrownModules,FTransform(FQuat::Identity,FVector::ZeroVector,
                Dimensions/ADesertBuildingModule::GetDefaultDimensions(Kind))*Placement);
        return; // 不借其他类型的网格桶或白模假装墙冠已配置。
    }
    if (Kind==EDesertModuleKind::AwningBay && AwningVariantIndex>=0)
    {
        // 选择来自规则结果，不能再从旧ModuleAwningBay重新求解/回退。
        if (AwningVariantModules.IsValidIndex(AwningVariantIndex) && ResolvedMesh &&
            AwningVariantModules[AwningVariantIndex]->GetStaticMesh()==ResolvedMesh)
            AddLayerInstance(AwningVariantModules[AwningVariantIndex],FTransform(FQuat::Identity,FVector::ZeroVector,
                Dimensions/ADesertBuildingModule::GetDefaultDimensions(Kind))*Placement);
        return;
    }
    if(Kind==EDesertModuleKind::PotCluster && AwningVariantIndex>=0)
    {
        if(PotVariantModules.IsValidIndex(AwningVariantIndex) && ResolvedMesh &&
            PotVariantModules[AwningVariantIndex]->GetStaticMesh()==ResolvedMesh)
            AddLayerInstance(PotVariantModules[AwningVariantIndex],FTransform(FQuat::Identity,FVector::ZeroVector,
                Dimensions/ADesertBuildingModule::GetDefaultDimensions(Kind))*Placement);
        return;
    }
    if (Kind==EDesertModuleKind::RubbleCluster)
    {
        if (RubbleModules->GetStaticMesh())
            AddLayerInstance(RubbleModules,FTransform(FQuat::Identity,FVector::ZeroVector,
                Dimensions/ADesertBuildingModule::GetDefaultDimensions(Kind))*Placement);
        else
        {
            TArray<FDesertModulePart> Recipe;
            ADesertBuildingModule::MakeVisualRecipe(Kind,Dimensions,Steps,false,false,Recipe);
            for (const FDesertModulePart& Part : Recipe) AddLayerInstance(RubbleParts,Part.Transform*Placement);
        }
        return; // 散石白模不进入有Pawn碰撞的结构白模桶。
    }
    const int32 CustomIndex=18+static_cast<int32>(Kind);
    if (BlockParts.IsValidIndex(CustomIndex) && BlockParts[CustomIndex]->GetStaticMesh())
    {
        const FVector Reference=ADesertBuildingModule::GetDefaultDimensions(Kind);
        AddLayerInstance(BlockParts[CustomIndex], FTransform(FQuat::Identity,FVector::ZeroVector,Dimensions/Reference)*Placement);
        return;
    }
    TArray<FDesertModulePart> Recipe;
    ADesertBuildingModule::MakeVisualRecipe(Kind,Dimensions,Steps,WithProps,false,Recipe);
    for (const FDesertModulePart& Part : Recipe)
    {
        const int32 Index=Part.Shape*6+Part.MaterialRole;
        if (BlockParts.IsValidIndex(Index)) AddLayerInstance(BlockParts[Index], Part.Transform*Placement);
    }
}

void ADesertBuilding::ResolveBlockLayout(const TSet<FIntVector>& Bodies,
    const TArray<FDesertSupportSpan>& Supports, const TArray<FDesertBlockPlacement>& Inputs,
    TArray<FDesertResolvedBlock>& OutBlocks) const
{
    OutBlocks.Reset();
    TMap<FIntVector,TArray<int32>> Anchors;
    TArray<FBox> Reservations;
    TMap<FIntVector,float> PavilionTops;
    for (const FDesertSupportSpan& Span : Supports)
    {
        const FVector Size(CellSize*0.17f,CellSize*0.17f,Span.Top.Z-Span.Bottom.Z);
        Reservations.Add(FBox(Span.Bottom-FVector(Size.X/2,Size.Y/2,0),Span.Top+FVector(Size.X/2,Size.Y/2,0)).ExpandBy(-0.5f));
    }
    TArray<int32> Order;
    for (int32 Index=0; Index<Inputs.Num(); ++Index) Order.Add(Index);
    // 支撑层先算，再算冠部。相同层保持数组顺序，争用同一空间时先声明者优先。
    Order.StableSort([&Inputs](int32 A,int32 B)
    {
        // 只延后采用新位置策略的罐组，先让已有结构/楼梯确定通路；旧类型之间顺序不变。
        const bool APositionedPot=Inputs[A].Type==EDesertBlockType::PotCluster && Inputs[A].PotPlacement!=EDesertPotPlacement::LegacyCentered;
        const bool BPositionedPot=Inputs[B].Type==EDesertBlockType::PotCluster && Inputs[B].PotPlacement!=EDesertPotPlacement::LegacyCentered;
        if (APositionedPot!=BPositionedPot) return !APositionedPot;
        const bool ARubble=Inputs[A].Type==EDesertBlockType::RubbleCluster;
        const bool BRubble=Inputs[B].Type==EDesertBlockType::RubbleCluster;
        if (ARubble!=BRubble) return !ARubble; // 仅追加装饰后算，不改变旧类型之间的优先顺序。
        return Inputs[A].Cell.Z<Inputs[B].Cell.Z;
    });
    for (int32 Index : Order)
    {
        const FDesertBlockPlacement& Block=Inputs[Index];
        if (!Block.bEnabled) continue;
        FDesertResolvedBlock& Resolved=OutBlocks.AddDefaulted_GetRef();
        Resolved.Index=Index;
        FString Error;
        EDesertModuleKind Kind=DesertBlockRules::Kind(Block.Type);
        if (Block.Type==EDesertBlockType::Stairs && Block.StairLayout!=EDesertStairLayout::OutwardLegacy)
            Kind=Block.StairLayout==EDesertStairLayout::Switchback ? EDesertModuleKind::SwitchbackStairs :
                (Block.StairLayout==EDesertStairLayout::LShape ? EDesertModuleKind::LShapeStairs :
                (Block.StairLayout==EDesertStairLayout::UShape ? EDesertModuleKind::UShapeStairs : EDesertModuleKind::WallStairs));
        UStaticMesh* CustomMesh=ADesertBuildingModule::GetStyleMesh(Style,Kind);
        int32 ResolvedVariantIndex=INDEX_NONE;
        FString VariantError;
        if (Block.Type==EDesertBlockType::AwningBay)
        {
            CustomMesh=Style ? Style->ResolveAwningBayMesh(Block.VariantIndex,Block.Cell,Seed) : nullptr;
            if (Block.VariantIndex < -1) VariantError=TEXT("棚架VariantIndex只允许-1自动或非负变体下标");
            else if (!Style || Style->ModuleAwningBayVariants.IsEmpty())
            {
                if (Block.VariantIndex>=0) VariantError=TEXT("当前Style没有棚架变体数组；请改为自动使用旧棚架槽，或先配置变体");
            }
            else
            {
                ResolvedVariantIndex=Style->ResolveAwningBayVariantIndex(Block.VariantIndex,Block.Cell,Seed);
                if (!Style->ModuleAwningBayVariants.IsValidIndex(ResolvedVariantIndex))
                    VariantError=TEXT("指定的棚架变体下标超出当前Style数组范围；没有自动改用其他模型");
                else if (!CustomMesh)
                    VariantError=FString::Printf(TEXT("棚架变体%d未配置模型；请填写该槽或明确选择另一变体，不会回退旧模型"),ResolvedVariantIndex+1);
            }
        }
        else if(Block.Type==EDesertBlockType::PotCluster)
        {
            CustomMesh=Style?Style->ResolvePotClusterMesh(Block.VariantIndex,Block.Cell,Seed):nullptr;
            if(Block.VariantIndex < -1) VariantError=TEXT("瓦罐组VariantIndex只允许-1自动或非负下标");
            else if(!Style || Style->ModulePotClusterVariants.IsEmpty())
            {
                if(Block.VariantIndex>=0) VariantError=TEXT("当前Style没有瓦罐整组变体数组；请改自动或配置该项");
            }
            else
            {
                ResolvedVariantIndex=Style->ResolvePotClusterVariantIndex(Block.VariantIndex,Block.Cell,Seed);
                if(!Style->ModulePotClusterVariants.IsValidIndex(ResolvedVariantIndex)) VariantError=TEXT("瓦罐组变体下标越界；没有换用其他模型");
                else if(!CustomMesh) VariantError=TEXT("指定瓦罐整组变体未配置；没有回退旧罐组模型");
            }
        }
        FVector Size=ADesertBuildingModule::GetDefaultDimensions(Kind);
        Size.X*=CellSize/300.0f; Size.Y*=CellSize/300.0f; Size.Z*=FloorHeight/300.0f;
        FVector Origin((Block.Cell.X+0.5f)*CellSize,(Block.Cell.Y+0.5f)*CellSize,Block.Cell.Z*FloorHeight);
        FQuat Rotation=FRotator(0,FMath::Clamp(Block.Facing,0,3)*90.0f,0).Quaternion();
        const bool InBounds=Block.Cell.X>=-3 && Block.Cell.X<=3 && Block.Cell.Y>=-3 && Block.Cell.Y<=3 && Block.Cell.Z>=0 && Block.Cell.Z<=6;
        const bool Roof=Block.Cell.Z>0 && Bodies.Contains(Block.Cell-FIntVector(0,0,1)) && !Bodies.Contains(Block.Cell);
        int32 NeighborWallCount=0;
        if (Block.Cell.Z==0 && !Bodies.Contains(Block.Cell))
            for (const FIntVector& Direction : DesertBlockRules::Directions) if (Bodies.Contains(Block.Cell+Direction)) ++NeighborWallCount;
        const bool Perimeter=NeighborWallCount>0;
        int32 Steps=15;
        if (!InBounds || Block.Facing<0 || Block.Facing>3 || (Block.Type==EDesertBlockType::Stairs && static_cast<uint8>(Block.StairLayout)>static_cast<uint8>(EDesertStairLayout::UShape))) Error=TEXT("坐标、Facing或楼梯类型超出允许范围");
        else if (Bodies.Contains(Block.Cell)) Error=TEXT("组合块位置被房屋体块占用");
        else if (const TArray<int32>* Existing=Anchors.Find(Block.Cell))
        {
            // 唯一允许的同格组合：墙外非Legacy楼梯 + 墙内屋顶棚亭。
            // 此处只取消过早的格坐标拒绝，后面的实际BBox/2m头顶区仍严格检查。
            // 两件齐全后不允许第三件，同类型、Legacy梯和墙冠也不共享这个锚点。
            const FDesertBlockPlacement& Other=Inputs[(*Existing)[0]];
            const bool SharedStairPavilion=Existing->Num()==1 &&
                ((Block.Type==EDesertBlockType::Stairs && Block.StairLayout!=EDesertStairLayout::OutwardLegacy &&
                    Other.Type==EDesertBlockType::RoofPavilion) ||
                 (Block.Type==EDesertBlockType::RoofPavilion && Other.Type==EDesertBlockType::Stairs &&
                    Other.StairLayout!=EDesertStairLayout::OutwardLegacy));
            if (!SharedStairPavilion) Error=TEXT("同一表面单元格已经存在建筑组合块；仅无实际重叠的外侧楼梯与内侧屋顶棚亭可共享");
        }
        if (Block.Type==EDesertBlockType::RoofPavilion)
        {
            if (!Roof) Error=TEXT("屋顶棚亭只能放在有效露台表面");
            Size.X=Size.Y=CellSize*(250.0f/300.0f);
        }
        else if (Block.Type==EDesertBlockType::RoofCrown)
        {
            if (!Roof) Error=TEXT("斜顶墙冠只能放在有效外露屋顶，不能直接放地面");
            else if (!CustomMesh) Error=TEXT("当前Style未配置ModuleRoofCrown斜顶墙冠模型；不会替换为穹顶或棚亭");
        }
        else if (Block.Type==EDesertBlockType::Dome)
        {
            const float* Top=PavilionTops.Find(Block.Cell-FIntVector(0,0,1));
            if (Top) Origin.Z=*Top;
            else if (!Roof) Error=TEXT("穹顶需要有效屋面，或正下方的屋顶棚亭");
            Size.X=Size.Y=CellSize*(250.0f/300.0f);
        }
        else if (Block.Type==EDesertBlockType::PotCluster)
        {
            if (!Roof && !Perimeter) Error=TEXT("瓦罐组合只允许屋顶或与首层外墙相邻的地面");
            else if (Perimeter)
            {
                float Ground=0;
                if (!TraceGround(Origin+FVector(0,0,FloorHeight*0.95f),Ground)) Error=TEXT("外围瓦罐位置没有可接触的地面");
                else Origin.Z=Ground;
            }
        }
        else if (Block.Type==EDesertBlockType::RubbleCluster)
        {
            if (Block.Cell.Z!=0 || !Perimeter) Error=TEXT("外围散石只能放在与首层外墙相邻的地面，不能放屋顶或远处空地");
            else
            {
                float Ground=0;
                if (!TraceGround(Origin+FVector(0,0,FloorHeight*.95f),Ground)) Error=TEXT("外围散石位置没有可接触的地面");
                else Origin.Z=Ground;
            }
        }
        else if (Block.Type==EDesertBlockType::AwningBay)
        {
            if (NeighborWallCount!=1 || !Bodies.Contains(Block.Cell-DesertBlockRules::Directions[FMath::Clamp(Block.Facing,0,3)])) Error=TEXT("棚架需要且只能依附一面首层外墙，背面应朝向该墙；检查Facing和墙角占用");
            else
            {
                // 组合格比棚架深；向背墙平移半个余量，让参考后沿真正接触墙面。
                // 探地和下方占用包围盒都使用修正后的同一个Origin。
                const FIntVector FacingDirection=DesertBlockRules::Directions[FMath::Clamp(Block.Facing,0,3)];
                float BackExtent=Size.Y*0.5f;
                if (CustomMesh)
                    BackExtent=CustomMesh->GetBoundingBox().Max.Y*(Size.Y/ADesertBuildingModule::GetDefaultDimensions(Kind).Y);
                Origin-=FVector(FacingDirection.X,FacingDirection.Y,0)*(CellSize*0.5f-BackExtent);
                float Ground=0;
                if (!TraceGround(Origin+FVector(0,0,FloorHeight*0.95f),Ground)) Error=TEXT("棚架下面没有有效地面");
                else
                {
                    // 每件变体的真实最低点对地面，而不是假定所有资产Min.Z都等于0。
                    const float BottomOffset=CustomMesh ? CustomMesh->GetBoundingBox().Min.Z*
                        (Size.Z/ADesertBuildingModule::GetDefaultDimensions(Kind).Z) : 0.f;
                    Origin.Z=Ground-BottomOffset;
                }
            }
        }
        else if (Block.Type==EDesertBlockType::Stairs && Block.StairLayout==EDesertStairLayout::OutwardLegacy)
        {
            if (!Roof && Error.IsEmpty()) Error=TEXT("楼梯目标必须是有效露台表面；请先放房间");
            {
                const FIntVector Direction=DesertBlockRules::Directions[FMath::Clamp(Block.Facing,0,3)];
                const FIntVector TargetBody=Block.Cell-FIntVector(0,0,1);
                if (Bodies.Contains(TargetBody+Direction)) Error=TEXT("楼梯落点不是外露屋顶边");
                const int32 RunCells=Block.Cell.Z*2;
                for (int32 Distance=1; Distance<=RunCells; ++Distance)
                    for (int32 Z=0; Z<=Block.Cell.Z; ++Z)
                        if (Bodies.Contains(FIntVector(Block.Cell.X+Direction.X*Distance,Block.Cell.Y+Direction.Y*Distance,Z))) Error=TEXT("楼梯通路与房屋体块冲突");
                const FVector Outward(Direction.X,Direction.Y,0);
                Size.X=CellSize*0.4f;
                Size.Y=CustomMesh ? 450.0f*(CellSize/300.0f) : CellSize*RunCells;
                const float TopZ=Origin.Z;
                Origin+=Outward*(CellSize*0.5f+Size.Y);
                float Ground=0;
                if (!TraceGround(Origin+FVector(0,0,FloorHeight),Ground)) Error=TEXT("楼梯低端没有检测到地面");
                {
                    Origin.Z=Ground;
                    Size.Z=TopZ-Ground;
                    if (Size.Z<20 || Size.Z>5000) Error=TEXT("楼梯目标与地面高度差不合理");
                    Steps=FMath::Clamp(FMath::CeilToInt(Size.Z/20.0f),1,128);
                }
            }
        }
        else if (Block.Type==EDesertBlockType::Stairs)
        {
            if (!Roof && Error.IsEmpty()) Error=TEXT("楼梯出口必须连接有效露台表面；请先放房间");
            {
                const FIntVector Direction=DesertBlockRules::Directions[FMath::Clamp(Block.Facing,0,3)];
                if (Bodies.Contains(Block.Cell-FIntVector(0,0,1)+Direction)) Error=TEXT("楼梯出口被相邻房间封住，必须选择外露屋顶边");
                const FVector Outward(Direction.X,Direction.Y,0);
                const float TopZ=Origin.Z;
                Origin+=Outward*(CellSize*0.5f); // 枢轴位于出口的墙面基准线上。
                if (!CustomMesh && Kind==EDesertModuleKind::WallStairs) Size.X*=Block.Cell.Z;
                else if (!CustomMesh && Kind==EDesertModuleKind::SwitchbackStairs) Size.Y*=Block.Cell.Z;
                const FVector Entry=Kind==EDesertModuleKind::WallStairs ? FVector(-Size.X*.85f,-Size.Y*.5f,0) :
                    (Kind==EDesertModuleKind::LShapeStairs ? FVector(-375,-435,0)*(CellSize/300.0f) :
                    (Kind==EDesertModuleKind::UShapeStairs ? FVector(-150,-60,0)*(CellSize/300.0f) : FVector(-Size.X*.7f,-Size.Y*.125f,0)));
                float Ground=0;
                if (!TraceGround(Origin+Rotation.RotateVector(Entry)+FVector(0,0,FloorHeight),Ground)) Error=TEXT("楼梯入口下方没有有效地面");
                {
                    Origin.Z=Ground;
                    Size.Z=TopZ-Ground;
                    Steps=FMath::Clamp(FMath::CeilToInt(Size.Z/20.0f),4,128);
                    if (Kind==EDesertModuleKind::SwitchbackStairs || Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs) Steps=((Steps+1)/2)*2;
                    const bool bArt08=Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs;
                    const float ClearWidth=bArt08 ? 120.0f*(CellSize/300.0f) : (Kind==EDesertModuleKind::WallStairs ? Size.Y : Size.X*.4f);
                    const float Tread=bArt08 ? 240.0f*(CellSize/300.0f)/(Steps/2) : (Kind==EDesertModuleKind::WallStairs ? Size.X*.7f/Steps : Size.Y*.5f/(Steps/2));
                    if (Size.Z<20 || Size.Z>5000 || ClearWidth<80 || Tread<22)
                        Error=TEXT("楼梯尺寸无法提供至少80cm通道和22cm踏面；请增大格宽或降低目标露台");
                }
            }
        }
        if (Block.Type==EDesertBlockType::Stairs && CustomMesh)
        {
            float Width=90, Tread=30;
            switch (Kind)
            {
            case EDesertModuleKind::Stairs: Steps=Style->StraightStairsStepCount; Width=Style->StraightStairsClearWidth; Tread=Style->StraightStairsTreadDepth; break;
            case EDesertModuleKind::WallStairs: Steps=Style->WallStairsStepCount; Width=Style->WallStairsClearWidth; Tread=Style->WallStairsTreadDepth; break;
            case EDesertModuleKind::SwitchbackStairs: Steps=Style->SwitchbackStairsStepCount; Width=Style->SwitchbackStairsClearWidth; Tread=Style->SwitchbackStairsTreadDepth; break;
            case EDesertModuleKind::LShapeStairs: Steps=Style->LShapeStairsStepCount; Width=Style->LShapeStairsClearWidth; Tread=Style->LShapeStairsTreadDepth; break;
            case EDesertModuleKind::UShapeStairs: Steps=Style->UShapeStairsStepCount; Width=Style->UShapeStairsClearWidth; Tread=Style->UShapeStairsTreadDepth; break;
            default: break;
            }
            const bool bDual=Kind==EDesertModuleKind::SwitchbackStairs || Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs;
            if (Block.Cell.Z!=1) Error=TEXT("固定整梯模型只允许从地面爬升到首层屋顶；高层需要逐层连接，不能拉伸整模型假装增加踏步");
            else if (Steps<1 || Steps>128 || (bDual && (Steps<2 || Steps%2!=0)) || !FMath::IsFinite(Width) || !FMath::IsFinite(Tread))
                Error=TEXT("Style固定梯的实际级数/净宽/踏深契约无效，双跑必须为偶数级");
            else if (Size.Z/Steps>22.0f+KINDA_SMALL_NUMBER || Width*(CellSize/300.f)<80 || Tread*(CellSize/300.f)<22)
                Error=FString::Printf(TEXT("固定梯实际%d级：抬高%.1fcm，净宽%.1fcm，踏深%.1fcm；要求级高≤22、净宽≥80、踏深≥22，不能只改变假想Steps"),Steps,Size.Z/Steps,Width*(CellSize/300.f),Tread*(CellSize/300.f));
        }
        if (Block.Type==EDesertBlockType::Stairs && (Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs) && Block.Cell.Z!=1)
            Error=TEXT("L/U双跑组合只连接地面与首层屋顶；当前不自动拉长路径到多层");
        if (Size.GetMax()>5000 || Size.GetMin()<1) Error=TEXT("模块尺寸超出白模配方1~5000cm范围；请减小格宽或楼梯目标层数");
        if (UStaticMesh* Mesh=CustomMesh)
        {
            const FBox MeshBounds=Mesh->GetBoundingBox();
            const FVector MeshSize=MeshBounds.GetSize();
            if (!MeshBounds.IsValid || MeshSize.ContainsNaN() || MeshSize.GetMin()<=KINDA_SMALL_NUMBER)
                Error=TEXT("整组合模型的包围盒或尺寸无效；请检查网格导入和推荐建模尺寸");
        }
        FBox LocalBox = Kind==EDesertModuleKind::Stairs ?
            FBox(FVector(-Size.X/2,0,0),FVector(Size.X/2,Size.Y,Size.Z)) :
            FBox(FVector(-Size.X/2,-Size.Y/2,0),FVector(Size.X/2,Size.Y/2,Size.Z));
        if (Kind==EDesertModuleKind::WallStairs)
            LocalBox=FBox(FVector(-Size.X*0.9f,-Size.Y,-15),FVector(Size.X*0.1f,0,Size.Z));
        if (Kind==EDesertModuleKind::LShapeStairs) LocalBox=FBox(FVector(-435,-510,-15),FVector(75,0,300)).TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Size/FVector(510,510,300)));
        if (Kind==EDesertModuleKind::UShapeStairs) LocalBox=FBox(FVector(-225,-480,-15),FVector(75,0,300)).TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Size/FVector(300,480,300)));
        if (Kind==EDesertModuleKind::SwitchbackStairs)
            LocalBox=FBox(FVector(-Size.X*0.75f,-Size.Y,-15),FVector(Size.X*0.25f,0,Size.Z));
        if (UStaticMesh* Mesh=CustomMesh)
            LocalBox=Mesh->GetBoundingBox().TransformBy(FTransform(FQuat::Identity,FVector::ZeroVector,Size/ADesertBuildingModule::GetDefaultDimensions(Kind)));
        if (Block.Type==EDesertBlockType::PotCluster && Block.PotPlacement!=EDesertPotPlacement::LegacyCentered && Error.IsEmpty())
        {
            // 预览、TryAddPlacement、保存实例重建共用这里，不在Slate另算一套偏移。
            const FBox RotatedBox=LocalBox.TransformBy(FTransform(Rotation));
            const float Gap=6.f*(CellSize/300.f);
            const float RoofInset=Roof ? 20.f*(CellSize/300.f) : 0.f;
            const float Inset=Gap+RoofInset;
            const FVector CellMin(Block.Cell.X*CellSize,Block.Cell.Y*CellSize,Origin.Z);
            const FVector RangeMin=CellMin+FVector(Inset-RotatedBox.Min.X,Inset-RotatedBox.Min.Y,-RotatedBox.Min.Z);
            const FVector RangeMax=CellMin+FVector(CellSize-Inset-RotatedBox.Max.X,CellSize-Inset-RotatedBox.Max.Y,-RotatedBox.Min.Z);
            TArray<FBox> ProtectedPassages;
            TArray<FBox> AcceptedStairs;
            for (const FDesertResolvedBlock& ExistingBlock : OutBlocks)
                if (ExistingBlock.Index!=Index && ExistingBlock.Check.bAllowed && Inputs[ExistingBlock.Index].Type==EDesertBlockType::Stairs)
                {
                    AcceptedStairs.Append(ExistingBlock.Check.LocalBounds);
                    // 顶部出口向屋顶内侧再保留150cm落脚区，避免罐虽不碰梯身却堵住出口。
                    const FDesertBlockPlacement& Stair=Inputs[ExistingBlock.Index];
                    const FIntVector D=DesertBlockRules::Directions[Stair.Facing];
                    const FVector Edge((Stair.Cell.X+.5f)*CellSize+D.X*CellSize*.5f,
                        (Stair.Cell.Y+.5f)*CellSize+D.Y*CellSize*.5f,Stair.Cell.Z*FloorHeight);
                    ProtectedPassages.Add(FBox(FVector(-CellSize*.3f,0,-1),FVector(CellSize*.3f,CellSize*.5f,200))
                        .TransformBy(FTransform(FRotator(0,Stair.Facing*90,0),Edge)));
                }
            TArray<FDesertDoorFace> PotDoorFaces; ResolveDoorFaces(Bodies,AcceptedStairs,PotDoorFaces);
            for (const FDesertDoorFace& Face : PotDoorFaces)
            {
                const FIntVector D=DesertBlockRules::Directions[Face.Side];
                const FVector Edge=CellBottomCenter(Face.Cell)+FVector(D.X,D.Y,0)*CellSize*.5f;
                ProtectedPassages.Add(FBox(FVector(-CellSize*.3f,-CellSize*.9f,0),FVector(CellSize*.3f,0,FloorHeight*.9f))
                    .TransformBy(FTransform(FRotator(0,Face.Side*90,0),Edge)));
            }
            for (const FDesertRoofOpening& Opening : RoofOpenings)
                if (Opening.Cell==Block.Cell-FIntVector(0,0,1) && Opening.Side>=0 && Opening.Side<4)
                {
                    const FIntVector D=DesertBlockRules::Directions[Opening.Side];
                    const FVector Edge((Block.Cell.X+.5f)*CellSize+D.X*CellSize*.5f,
                        (Block.Cell.Y+.5f)*CellSize+D.Y*CellSize*.5f,Origin.Z);
                    ProtectedPassages.Add(FBox(FVector(-Opening.Width*.5f,0,-1),FVector(Opening.Width*.5f,CellSize*.5f,200))
                        .TransformBy(FTransform(FRotator(0,Opening.Side*90,0),Edge)));
                }
            auto CandidateOrigin=[&](FVector Direction)
            {
                return FVector(Direction.X<-.5f ? RangeMin.X : (Direction.X>.5f ? RangeMax.X : (RangeMin.X+RangeMax.X)*.5f),
                    Direction.Y<-.5f ? RangeMin.Y : (Direction.Y>.5f ? RangeMax.Y : (RangeMin.Y+RangeMax.Y)*.5f),RangeMin.Z);
            };
            auto CandidateAllowed=[&](const FVector& Candidate)
            {
                const FBox Bounds=LocalBox.TransformBy(FTransform(Rotation,Candidate)).ExpandBy(-.5f);
                for (const FBox& Reserved : Reservations) if (Bounds.Intersect(Reserved)) return false;
                for (const FBox& Passage : ProtectedPassages) if (Bounds.Intersect(Passage)) return false;
                for (const FIntVector& Body : Bodies)
                {
                    const FVector Low(Body.X*CellSize,Body.Y*CellSize,Body.Z*FloorHeight);
                    if (Bounds.Intersect(FBox(Low,Low+FVector(CellSize,CellSize,FloorHeight)))) return false;
                }
                if (Perimeter)
                {
                    // 四角承托也用平移后的整组边界，避免只在原格心探地。
                    for (float X : {Bounds.Min.X,Bounds.Max.X}) for (float Y : {Bounds.Min.Y,Bounds.Max.Y})
                    {
                        float Ground=0;
                        if (!TraceGround(FVector(X,Y,Candidate.Z+FloorHeight),Ground) || FMath::Abs(Ground-(Candidate.Z+RotatedBox.Min.Z))>10.f) return false;
                    }
                }
                return true;
            };
            TArray<FVector> Candidates;
            if (Block.PotPlacement==EDesertPotPlacement::Automatic)
            {
                // 固定Facing优先次序；先选本层实体墙，再选裸露屋顶外围，不按随机数跳位置。
                for (int32 Pass=0;Pass<2;++Pass) for (int32 Offset=0;Offset<4;++Offset)
                {
                    const int32 Side=(Block.Facing+Offset)%4;
                    const FIntVector D=DesertBlockRules::Directions[Side];
                    const bool Wall=Bodies.Contains(Block.Cell+D);
                    const bool RoofEdge=Roof && !Bodies.Contains(Block.Cell-FIntVector(0,0,1)+D);
                    if ((Pass==0 && !Wall) || (Pass==1 && (Wall || !RoofEdge))) continue;
                    const FVector Toward(D.X,D.Y,0),Across(-D.Y,D.X,0);
                    Candidates.Add(CandidateOrigin(Toward));
                    Candidates.Add(CandidateOrigin(Toward+Across));
                    Candidates.Add(CandidateOrigin(Toward-Across));
                }
            }
            else if (Block.PotPlacement>=EDesertPotPlacement::Front && Block.PotPlacement<=EDesertPotPlacement::Center)
            {
                static const FVector LocalDirections[]={FVector(0,-1,0),FVector(1,-1,0),FVector(1,0,0),FVector(1,1,0),
                    FVector(0,1,0),FVector(-1,1,0),FVector(-1,0,0),FVector(-1,-1,0),FVector::ZeroVector};
                Candidates.Add(CandidateOrigin(Rotation.RotateVector(LocalDirections[static_cast<uint8>(Block.PotPlacement)-static_cast<uint8>(EDesertPotPlacement::Front)])));
            }
            bool Found=false;
            if (RangeMin.X<=RangeMax.X && RangeMin.Y<=RangeMax.Y)
                for (const FVector& Candidate : Candidates) if (CandidateAllowed(Candidate)) {Origin=Candidate;Found=true;break;}
            if (!Found) Error=TEXT("瓦罐摆放位置没有安全空间：检查整组尺寸、门前通路、屋顶出口及地面承托；自动模式只靠有效墙或屋顶外缘，不会悄悄居中");
        }
        const FTransform Placement(Rotation,Origin);
        TArray<FBox> Footprints; Footprints.Add(LocalBox);
        if (Kind==EDesertModuleKind::LShapeStairs)
        {
            const FVector Scale=Size/FVector(510,510,300);
            const FTransform LocalScale(FQuat::Identity,FVector::ZeroVector,Scale);
            Footprints={FBox(FVector(-435,-510,-15),FVector(75,-360,150)).TransformBy(LocalScale),
                FBox(FVector(-75,-360,-15),FVector(75,0,300)).TransformBy(LocalScale)};
            if (CustomMesh && (!LocalBox.Min.Equals(FVector(-435,-510,-15)*Scale,2.f) || !LocalBox.Max.Equals(FVector(75,0,300)*Scale,2.f)))
                Error=TEXT("L形模型包围盒不符合专用路径契约，不能保证内空区和出口；请检查510×510基准和原点");
        }
        if (Kind==EDesertModuleKind::UShapeStairs && CustomMesh)
        {
            const FVector Scale=Size/FVector(300,480,300);
            if (!LocalBox.Min.Equals(FVector(-225,-480,-15)*Scale,2.f) || !LocalBox.Max.Equals(FVector(75,0,300)*Scale,2.f))
                Error=TEXT("U形模型包围盒不符合专用双跑契约；请检查300×480基准和原点");
        }
        TArray<FBox> ActualBounds;
        for (const FBox& Footprint : Footprints)
        {
            FBox Bounds=Footprint.TransformBy(Placement).ExpandBy(-.5f);
            if (Block.Type==EDesertBlockType::Stairs && Block.StairLayout!=EDesertStairLayout::OutwardLegacy) Bounds.Max.Z+=200;
            ActualBounds.Add(Bounds);
        }
        if (Error.IsEmpty())
        {
            for (const FBox& Bounds : ActualBounds)
                for (const FIntVector& Body : Bodies)
                {
                    const FVector Minimum(Body.X*CellSize,Body.Y*CellSize,Body.Z*FloorHeight);
                    if (Bounds.Intersect(FBox(Minimum,Minimum+FVector(CellSize,CellSize,FloorHeight))))
                    { Error=TEXT("模块实际通路、平台或楼梯2m头顶空间与房屋体块冲突"); break; }
                }
            if (Block.Type==EDesertBlockType::Stairs && Block.StairLayout!=EDesertStairLayout::OutwardLegacy)
                for (const FBox& Footprint : Footprints)
                    for (float X : {Footprint.Min.X+2,Footprint.Max.X-2})
                        for (float Y : {Footprint.Min.Y+2,Footprint.Max.Y-2})
                        {
                            float Height=0;
                            if (!TraceGround(Placement.TransformPosition(FVector(X,Y,Size.Z+FloorHeight)),Height) || FMath::Abs(Height-Origin.Z)>10)
                                Error=TEXT("楼梯每段通路/平台四角需要地面支撑且高差≤10cm；L形内空区不误作支撑要求");
                        }
        }
        if (Error.IsEmpty() && (Block.Type==EDesertBlockType::AwningBay || Block.Type==EDesertBlockType::RubbleCluster))
        {
            // 中心落地不足以证明四根柱子都被承托；以实际整模块包围盒四角检测。
            // 平台容差与楼梯一致，避免棚架跨在坑洞或明显斜坡上。
            for (float X : {LocalBox.Min.X+2,LocalBox.Max.X-2})
                for (float Y : {LocalBox.Min.Y+2,LocalBox.Max.Y-2})
                {
                    const FVector Probe=Placement.TransformPosition(FVector(X,Y,Size.Z+FloorHeight));
                    float Height=0;
                    const float BaseZ=Block.Type==EDesertBlockType::AwningBay ? Origin.Z+LocalBox.Min.Z : Origin.Z;
                    if (!TraceGround(Probe,Height) || FMath::Abs(Height-BaseZ)>10)
                        Error=Block.Type==EDesertBlockType::AwningBay
                            ? TEXT("棚架四角需要同一近似平面的地面支撑，高差不能超过10cm；请整理地面或换位置")
                            : TEXT("散石整组四角需要近似平整地面，高差不能超过10cm；首版不自动逐石适配坡地");
                }
        }
        if (Error.IsEmpty())
            for (const FBox& Bounds : ActualBounds)
                for (const FBox& Other : Reservations) if (Bounds.Intersect(Other)) { Error=TEXT("与其他组合块或自动支柱的占用空间冲突"); break; }
        if (!VariantError.IsEmpty()) Error=VariantError;
        Resolved.Check.bAllowed=Error.IsEmpty();
        Resolved.Check.Reason=Error;
        Resolved.Check.LocalBounds=ActualBounds;
        Resolved.Check.bHasVisualRecipe=VariantError.IsEmpty() && !(Block.Type==EDesertBlockType::RoofCrown && !CustomMesh);
        Resolved.Check.ModuleKind=Kind;
        Resolved.Check.Placement=Placement;
        Resolved.Check.Dimensions=Size;
        Resolved.Check.Steps=Steps;
        Resolved.Check.CustomMesh=CustomMesh;
        Resolved.Check.ResolvedVariantIndex=ResolvedVariantIndex;
        Resolved.FloorIndex=FMath::Max(1,Block.Cell.Z);
        if (Block.Type==EDesertBlockType::RubbleCluster) Resolved.FloorIndex=0;
        if (Block.Type==EDesertBlockType::Dome && PavilionTops.Contains(Block.Cell-FIntVector(0,0,1)))
            Resolved.FloorIndex=FMath::Max(1,Block.Cell.Z-1);
        if (!Error.IsEmpty()) continue;
        Anchors.FindOrAdd(Block.Cell).Add(Index);
        Reservations.Append(ActualBounds);
        if (Block.Type==EDesertBlockType::RoofPavilion) PavilionTops.Add(Block.Cell,Origin.Z+Size.Z);
    }
    TArray<FBox> ActualStairs;
    for (const FDesertResolvedBlock& Block : OutBlocks)
        if (Block.Check.bAllowed && Inputs[Block.Index].Type==EDesertBlockType::Stairs)
            ActualStairs.Append(Block.Check.LocalBounds);
    TArray<FDesertDoorFace> DoorFaces; ResolveDoorFaces(Bodies,ActualStairs,DoorFaces);
    TArray<FBox> DoorPassages;
    for (const FDesertDoorFace& Face : DoorFaces)
    {
        const FIntVector Direction=DesertBlockRules::Directions[Face.Side];
        const FVector Edge=CellBottomCenter(Face.Cell)+FVector(Direction.X,Direction.Y,0)*CellSize*.5f;
        DoorPassages.Add(FBox(FVector(-CellSize*.3f,-CellSize*.9f,0),FVector(CellSize*.3f,0,FloorHeight*.9f))
            .TransformBy(FTransform(FRotator(0,Face.Side*90,0),Edge)).ExpandBy(-.5f));
    }
    for (FDesertResolvedBlock& Block : OutBlocks)
        if (Block.Check.bAllowed && Inputs[Block.Index].Type==EDesertBlockType::RubbleCluster)
            for (const FBox& Passage : DoorPassages)
                if (Block.Check.LocalBounds[0].Intersect(Passage))
                { Block.Check.bAllowed=false; Block.Check.Reason=TEXT("外围散石与某房间实际门前通路冲突；请换到其他外墙外围格"); break; }

}

void ADesertBuilding::ResolveAndBuildBlocks()
{
    ValidBlockCount=InvalidBlockCount=0;
    InvalidBlockIndices.Reset();
    EffectiveRoofOpenings=RoofOpenings;
    RoofCrownRoofCells.Reset();
    RoofDressingBlockedCells.Reset();
    StairReservations.Reset();
    for (const FDesertSupportSpan& Span : SupportSpans)
    {
        float BottomZ=Span.Bottom.Z;
        while (BottomZ<Span.Top.Z-KINDA_SMALL_NUMBER)
        {
            GenerationFloorIndex=BottomZ<0 ? 0 : FMath::FloorToInt((BottomZ+0.01f)/FloorHeight)+1;
            const float BoundaryZ=GenerationFloorIndex==0 ? 0.0f : GenerationFloorIndex*FloorHeight;
            const float TopZ=FMath::Min(Span.Top.Z,BoundaryZ);
            EmitModule(EDesertModuleKind::Column,FTransform(FVector(Span.Bottom.X,Span.Bottom.Y,BottomZ)),
                FVector(CellSize*0.17f,CellSize*0.17f,TopZ-BottomZ));
            BottomZ=TopZ;
        }
    }
    TArray<FDesertResolvedBlock> Resolved;
    ResolveBlockLayout(Occupied,SupportSpans,Blocks,Resolved);
    for (const FDesertResolvedBlock& Block : Resolved)
    {
        const FDesertBlockPlacement& Input=Blocks[Block.Index];
        if (!Block.Check.bAllowed)
        {
            ++InvalidBlockCount;
            InvalidBlockIndices.Add(Block.Index);
            ValidationMessages.Add(FString::Printf(TEXT("Blocks[%d] %s 无效：%s。保留数据但不生成；请显式清理或删除。"),
                Block.Index,*Input.Cell.ToString(),*Block.Check.Reason));
            continue;
        }
        ++ValidBlockCount;
        if(Input.Cell.Z>0) RoofDressingBlockedCells.Add(Input.Cell-FIntVector(0,0,1));
        if (Input.Type==EDesertBlockType::RoofCrown)
            RoofCrownRoofCells.Add(Input.Cell-FIntVector(0,0,1));
        GenerationFloorIndex=Block.FloorIndex;
        EmitModule(Block.Check.ModuleKind,Block.Check.Placement,Block.Check.Dimensions,Block.Check.Steps,
            Block.Check.ModuleKind==EDesertModuleKind::AwningBay,Block.Check.CustomMesh,Block.Check.ResolvedVariantIndex);
        if (Input.Type==EDesertBlockType::Stairs)
        {
            StairReservations.Append(Block.Check.LocalBounds);
            FDesertRoofOpening& Opening=EffectiveRoofOpenings.AddDefaulted_GetRef();
            Opening.Cell=Input.Cell-FIntVector(0,0,1);
            Opening.Side=Input.Facing;
            Opening.Width=CellSize*0.5f;
        }
    }
    ValidationMessages.Insert(FString::Printf(TEXT("有效体块 %d；无效房间输入 %d；有效组合块 %d；无效组合块 %d；自动支柱 %d。修改地形后请Rebuild重新检测。"),
        Occupied.Num(),InvalidCellIndices.Num(),ValidBlockCount,InvalidBlockCount,SupportColumnCount),0);
}

FDesertPlacementCheck ADesertBuilding::EvaluatePlacement(bool bRoom, const FDesertBlockPlacement& Proposal) const
{
    if (!FMath::IsFinite(CellSize) || !FMath::IsFinite(FloorHeight) || CellSize<100 || CellSize>1000 || FloorHeight<100 || FloorHeight>1000 ||
        !FMath::IsFinite(FoundationDepth) || FoundationDepth<10 || FoundationDepth>10000 ||
        !FMath::IsFinite(SupportTraceDistance) || SupportTraceDistance<100 || SupportTraceDistance>50000)
    {
        FDesertPlacementCheck Invalid;
        Invalid.Reason=TEXT("建筑尺寸或地面检测距离无效；请先Rebuild恢复允许范围，再放置");
        return Invalid;
    }
    // 全部使用局部结果，绝不改写输入、组件、材质、撤销记录或上一轮的诊断。
    TSet<FIntVector> BeforeBodies,AfterBodies;
    TArray<FDesertSupportSpan> BeforeSupports,AfterSupports;
    TArray<int32> BeforeInvalidCells,AfterInvalidCells;
    TArray<FString> BeforeMessages,AfterMessages;
    TArray<FDesertResolvedBlock> BeforeBlocks,AfterBlocks;
    ResolveSupportedCells(Cells,BeforeBodies,BeforeSupports,BeforeInvalidCells,BeforeMessages);
    ResolveBlockLayout(BeforeBodies,BeforeSupports,Blocks,BeforeBlocks);
    TArray<FIntVector> CandidateCells=Cells;
    TArray<FDesertBlockPlacement> CandidateBlocks=Blocks;
    if (bRoom) CandidateCells.Add(Proposal.Cell);
    else CandidateBlocks.Add(Proposal);
    ResolveSupportedCells(CandidateCells,AfterBodies,AfterSupports,AfterInvalidCells,AfterMessages);
    ResolveBlockLayout(AfterBodies,AfterSupports,CandidateBlocks,AfterBlocks);

    FDesertPlacementCheck Result;
    if (bRoom)
    {
        const FVector Min(Proposal.Cell.X*CellSize,Proposal.Cell.Y*CellSize,Proposal.Cell.Z*FloorHeight);
        Result.LocalBounds.Add(FBox(Min,Min+FVector(CellSize,CellSize,FloorHeight)));
        Result.Placement=FTransform(CellBottomCenter(Proposal.Cell));
        Result.Dimensions=FVector(CellSize,CellSize,FloorHeight);
        Result.bAllowed=IsCellInBounds(Proposal.Cell) && !Cells.Contains(Proposal.Cell) &&
            AfterBodies.Contains(Proposal.Cell) && !AfterInvalidCells.Contains(Cells.Num());
        if (!IsCellInBounds(Proposal.Cell)) Result.Reason=TEXT("房间坐标超出允许范围");
        else if (Cells.Contains(Proposal.Cell)) Result.Reason=TEXT("该单元格已有房间；请先删除或换一个格子");
        else if (!Result.bAllowed) Result.Reason=TEXT("房间下方缺少有效承托，且无法生成四角支柱；请检查地面或支柱选项");
        for (const FDesertSupportSpan& Span : AfterSupports)
        {
            const bool bExisting=BeforeSupports.ContainsByPredicate([&Span](const FDesertSupportSpan& Other)
            { return Other.Bottom.Equals(Span.Bottom,0.01f) && Other.Top.Equals(Span.Top,0.01f); });
            if (bExisting) continue;
            FDesertPlacementSupport& Support=Result.Supports.AddDefaulted_GetRef();
            Support.Bottom=Span.Bottom; Support.Top=Span.Top;
            const FVector Extent(CellSize*0.085f,CellSize*0.085f,0);
            Result.LocalBounds.Add(FBox(Span.Bottom-Extent,Span.Top+Extent));
        }
    }
    else
    {
        const FDesertResolvedBlock* Candidate=AfterBlocks.FindByPredicate([this](const FDesertResolvedBlock& Block)
            { return Block.Index==Blocks.Num(); });
        if (Candidate) Result=Candidate->Check;
        else Result.Reason=TEXT("不能放置已禁用的条目");
    }
    TArray<FBox> BeforeRoomStairs,AfterRoomStairs;
    for (const FDesertResolvedBlock& Block : BeforeBlocks)
        if (Block.Check.bAllowed && Blocks[Block.Index].Type==EDesertBlockType::Stairs) BeforeRoomStairs.Append(Block.Check.LocalBounds);
    for (const FDesertResolvedBlock& Block : AfterBlocks)
        if (Block.Check.bAllowed && CandidateBlocks[Block.Index].Type==EDesertBlockType::Stairs) AfterRoomStairs.Append(Block.Check.LocalBounds);
    TArray<int32> BeforeInvalidAppearances,AfterInvalidAppearances;
    ResolveInvalidRoomAppearances(BeforeBodies,BeforeRoomStairs,BeforeInvalidAppearances,BeforeMessages);
    ResolveInvalidRoomAppearances(AfterBodies,AfterRoomStairs,AfterInvalidAppearances,AfterMessages);
    int32 OldInvalidBlocks=0;
    for (const FDesertResolvedBlock& Block : BeforeBlocks) if (!Block.Check.bAllowed) ++OldInvalidBlocks;
    if (!BeforeInvalidCells.IsEmpty() || OldInvalidBlocks>0 || !BeforeInvalidAppearances.IsEmpty())
    {
        Result.bAllowed=false;
        Result.Reason=FString::Printf(TEXT("存在旧无效条目（房间%d、组合块%d、门窗%d）；请先清理或删除，再放置，避免隐藏条目突然出现。"),
            BeforeInvalidCells.Num(),OldInvalidBlocks,BeforeInvalidAppearances.Num());
        return Result;
    }
    if (!Result.bAllowed) return Result;
    if (!AfterInvalidAppearances.IsEmpty())
    { Result.bAllowed=false; Result.Reason=TEXT("放置会使已有房间的门窗配置失效（内部门或门前被梯堵）；请先调整该房间门向或放置位置"); return Result; }
    if (bUseEntranceOverride)
    {
        TArray<FBox> BeforeStairs,AfterStairs;
        for (const FDesertResolvedBlock& Block : BeforeBlocks)
            if (Block.Check.bAllowed && Blocks[Block.Index].Type==EDesertBlockType::Stairs)
                BeforeStairs.Append(Block.Check.LocalBounds);
        for (const FDesertResolvedBlock& Block : AfterBlocks)
            if (Block.Check.bAllowed && CandidateBlocks[Block.Index].Type==EDesertBlockType::Stairs)
                AfterStairs.Append(Block.Check.LocalBounds);
        FIntVector EntryCell;
        int32 EntrySide=-1;
        const bool bHadValidEntry=ResolveEntranceCandidate(BeforeBodies,BeforeStairs,EntryCell,EntrySide);
        FString OverrideError;
        const bool bStillValidEntry=ResolveEntranceCandidate(AfterBodies,AfterStairs,EntryCell,EntrySide,&OverrideError);
        if (bHadValidEntry && !bStillValidEntry)
        {
            Result.bAllowed=false;
            Result.Reason=TEXT("放置会使已经指定的人工入口失效：")+OverrideError+TEXT("；请先调整入口设置或放置位置");
            return Result;
        }
    }
    for (const FIntVector& Cell : BeforeBodies)
        if (!AfterBodies.Contains(Cell))
        {
            Result.bAllowed=false;
            Result.Reason=FString::Printf(TEXT("放置会使已有房间 %s 失去有效支撑；请先调整已有房间。"),*Cell.ToString());
            return Result;
        }
    for (const FDesertResolvedBlock& Existing : BeforeBlocks)
    {
        if (!Existing.Check.bAllowed) continue;
        const FDesertResolvedBlock* After=AfterBlocks.FindByPredicate([&Existing](const FDesertResolvedBlock& Block)
            {return Block.Index==Existing.Index;});
        if (!After || !After->Check.bAllowed)
        {
            Result.bAllowed=false;
            Result.Reason=FString::Printf(TEXT("此放置会遮挡或挤占已有 Blocks[%d]，导致它失效：%s。请先删除或移动已有块。"),
                Existing.Index,After ? *After->Check.Reason : TEXT("支撑变化"));
            return Result;
        }
    }
    Result.Reason=TEXT("可以放置；左键确认");
    return Result;
}

bool ADesertBuilding::TryAddPlacement(bool bRoom, const FDesertBlockPlacement& Proposal)
{
    const FDesertPlacementCheck Check=EvaluatePlacement(bRoom,Proposal);
    if (!Check.bAllowed)
    {
        // 失败仅给出可见反馈，绝不在数组里留下以后会突然生成的待定条目。
        ValidationMessages.AddUnique(FString::Printf(TEXT("放置未提交：%s"),*Check.Reason));
        return false;
    }
    Modify();
    if (bRoom) Cells.Add(Proposal.Cell);
    else Blocks.Add(Proposal);
    Rebuild();
    return true;
}

int32 ADesertBuilding::RemoveInvalidAuthoringEntries()
{
    TSet<FIntVector> Bodies;
    TArray<FDesertSupportSpan> Supports;
    TArray<int32> BadCells,BadBlocks;
    TArray<FString> Messages;
    TArray<FDesertResolvedBlock> Resolved;
    ResolveSupportedCells(Cells,Bodies,Supports,BadCells,Messages);
    ResolveBlockLayout(Bodies,Supports,Blocks,Resolved);
    for (const FDesertResolvedBlock& Block : Resolved) if (!Block.Check.bAllowed) BadBlocks.Add(Block.Index);
    TArray<FBox> Stairs;
    for (const FDesertResolvedBlock& Block : Resolved)
        if (Block.Check.bAllowed && Blocks[Block.Index].Type==EDesertBlockType::Stairs) Stairs.Append(Block.Check.LocalBounds);
    TArray<int32> BadAppearances;
    ResolveInvalidRoomAppearances(Bodies,Stairs,BadAppearances,Messages);
    const int32 Count=BadCells.Num()+BadBlocks.Num()+BadAppearances.Num();
    if (Count==0) return 0;
    Modify();
    BadCells.Sort([](int32 A,int32 B){return A>B;});
    BadBlocks.Sort([](int32 A,int32 B){return A>B;});
    for (int32 Index : BadCells) Cells.RemoveAt(Index);
    for (int32 Index : BadBlocks) Blocks.RemoveAt(Index);
    BadAppearances.Sort([](int32 A,int32 B){return A>B;});
    for (int32 Index : BadAppearances) RoomAppearanceOverrides.RemoveAt(Index);
    Rebuild();
    return Count;
}

int32 ADesertBuilding::RemovePlacementAndDependents(bool bRoom, const FDesertBlockPlacement& Proposal)
{
    TSet<FIntVector> BeforeBodies;
    TArray<FDesertSupportSpan> Supports;
    TArray<int32> BadCells;
    TArray<FString> Messages;
    TArray<FDesertResolvedBlock> BeforeBlocks;
    ResolveSupportedCells(Cells,BeforeBodies,Supports,BadCells,Messages);
    ResolveBlockLayout(BeforeBodies,Supports,Blocks,BeforeBlocks);
    TSet<int32> PreviouslyValid;
    for (const FDesertResolvedBlock& Block : BeforeBlocks) if (Block.Check.bAllowed) PreviouslyValid.Add(Block.Index);
    TArray<FIntVector> WorkingCells=Cells;
    TArray<int32> OriginalCellIndices;
    TSet<int32> PreviouslyValidCells;
    for (int32 Index=0;Index<Cells.Num();++Index)
    {
        OriginalCellIndices.Add(Index);
        if (!BadCells.Contains(Index) && BeforeBodies.Contains(Cells[Index])) PreviouslyValidCells.Add(Index);
    }
    TArray<FDesertBlockPlacement> WorkingBlocks=Blocks;
    TArray<int32> OriginalIndices;
    for (int32 Index=0;Index<Blocks.Num();++Index) OriginalIndices.Add(Index);
    int32 Count=0;
    if (bRoom)
    {
        for (int32 Index=WorkingCells.Num()-1;Index>=0;--Index)
            if (WorkingCells[Index]==Proposal.Cell)
            {WorkingCells.RemoveAt(Index);OriginalCellIndices.RemoveAt(Index);++Count;}
    }
    else
    {
        for (int32 Index=WorkingBlocks.Num()-1;Index>=0;--Index)
            if (WorkingBlocks[Index].Cell==Proposal.Cell && WorkingBlocks[Index].Type==Proposal.Type)
            {WorkingBlocks.RemoveAt(Index);OriginalIndices.RemoveAt(Index);++Count;}
        // 穹顶在棚亭上面的单元格上；删除承托棚亭时不会重新解释为其他屋面的穹顶。
        if (Count>0 && Proposal.Type==EDesertBlockType::RoofPavilion)
            for (int32 Index=WorkingBlocks.Num()-1;Index>=0;--Index)
                if (WorkingBlocks[Index].Type==EDesertBlockType::Dome &&
                    WorkingBlocks[Index].Cell==Proposal.Cell+FIntVector(0,0,1) && PreviouslyValid.Contains(OriginalIndices[Index]))
                {WorkingBlocks.RemoveAt(Index);OriginalIndices.RemoveAt(Index);++Count;}
    }
    if (Count==0) return 0;
    bool bChanged=true;
    while (bChanged)
    {
        bChanged=false;
        TSet<FIntVector> Bodies;
        TArray<FDesertResolvedBlock> Resolved;
        ResolveSupportedCells(WorkingCells,Bodies,Supports,BadCells,Messages);
        ResolveBlockLayout(Bodies,Supports,WorkingBlocks,Resolved);
        TArray<int32> NewlyInvalid;
        for (const FDesertResolvedBlock& Block : Resolved)
            if (!Block.Check.bAllowed && PreviouslyValid.Contains(OriginalIndices[Block.Index])) NewlyInvalid.Add(Block.Index);
        NewlyInvalid.Sort([](int32 A,int32 B){return A>B;});
        for (int32 Index : NewlyInvalid)
        {WorkingBlocks.RemoveAt(Index);OriginalIndices.RemoveAt(Index);++Count;bChanged=true;}
        // 只移除此动作导致失去支撑的旧有效房间，不顺手删除加载时就无效的输入。
        BadCells.Sort([](int32 A,int32 B){return A>B;});
        for (int32 Index : BadCells)
            if (PreviouslyValidCells.Contains(OriginalCellIndices[Index]))
            {WorkingCells.RemoveAt(Index);OriginalCellIndices.RemoveAt(Index);++Count;bChanged=true;}
    }
    Modify();
    TSet<FIntVector> RemovedRooms;
    for (const FIntVector& Cell : Cells) if (!WorkingCells.Contains(Cell)) RemovedRooms.Add(Cell);
    Count+=RoomAppearanceOverrides.RemoveAll([&RemovedRooms](const FDesertRoomAppearance& Appearance){return RemovedRooms.Contains(Appearance.Cell);});
    Cells=MoveTemp(WorkingCells); Blocks=MoveTemp(WorkingBlocks);
    Rebuild();
    ValidationMessages.Add(FString::Printf(TEXT("删除了%d个输入条目（包含依赖它的楼梯/屋顶附件）；此操作可撤销。"),Count));
    return Count;
}
