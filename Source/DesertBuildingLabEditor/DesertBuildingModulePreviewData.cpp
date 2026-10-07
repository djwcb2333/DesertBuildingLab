#include "DesertBuildingModulePreviewData.h"
#include "DesertBuildingStyle.h"
#include "DesertBuildingModule.h"
#include "PreviewScene.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

const TArray<EDesertStairLayout>& DesertGetAuthoringStairLayouts()
{
    // 不按数组下标强转enum；值2继续用于读取旧Design，不提供新放置入口。
    static const TArray<EDesertStairLayout> Layouts={EDesertStairLayout::AlongWall,
        EDesertStairLayout::LShape,EDesertStairLayout::UShape,EDesertStairLayout::OutwardLegacy};
    return Layouts;
}

FDesertModulePreviewData DesertMakeModulePreviewData(const ADesertBuilding* Source,
    int32 Tool, EDesertStairLayout Layout, int32 VariantIndex, int32 Facing, FIntVector ContextCell)
{
    FDesertModulePreviewData Data;
    if (!Source || Facing<0 || Facing>3) {Data.Description=TEXT("没有建筑草稿，或朝向超出0..3"); return Data;}
    const UDesertBuildingStyle* Style=Source->Style.Get();
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false).SetEditor(true));
    FActorSpawnParameters Spawn; Spawn.ObjectFlags=RF_Transient;
    AActor* Model=nullptr;
    if (Tool==0)
    {
        ADesertBuilding* Room=Scene.GetWorld()->SpawnActor<ADesertBuilding>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
        if (!Room) {Data.Description=TEXT("房间展示世界创建失败"); return Data;}
        Model=Room;
        Room->SetActorEnableCollision(false);
        Room->Cells={FIntVector::ZeroValue}; Room->Blocks.Reset(); Room->RoofOpenings.Reset();
        Room->Style=Source->Style; Room->CellSize=Source->CellSize; Room->FloorHeight=Source->FloorHeight;
        Room->FoundationDepth=Source->FoundationDepth; Room->Seed=Source->Seed;
        Room->bAutoSupportColumns=false; Room->bEnableAwning=false; Room->bShowWoodBeams=Source->bShowWoodBeams;
        Room->bUseEntranceOverride=Source->bUseEntranceOverride && Source->EntranceOverrideCell==ContextCell && ContextCell.Z==0;
        Room->EntranceOverrideCell=FIntVector::ZeroValue; Room->EntranceOverrideSide=Source->EntranceOverrideSide;
        FDesertRoomAppearance Appearance=Source->GetRoomAppearance(ContextCell); Appearance.Cell=FIntVector::ZeroValue;
        Room->RoomAppearanceOverrides={Appearance};
        if(Style && Style->bEnableRoofDressing)
        {
            // 房间临时移到0格展示，随机仍按原作者格求解；副本绝不写回Source的Style。
            UDesertBuildingStyle* PreviewStyle=DuplicateObject<UDesertBuildingStyle>(Style,Room);
            PreviewStyle->RoofDressingVariantIndex=Style->ResolveRoofDressingVariantIndex(ContextCell,Source->Seed);
            PreviewStyle->bRandomizeRoofDressing=false;
            bool bProtected=Source->HasCell(ContextCell+FIntVector(0,0,1));
            for(const FDesertRoofOpening& Opening:Source->RoofOpenings) if(Opening.Cell==ContextCell) bProtected=true;
            for(int32 Index=0;Index<Source->Blocks.Num();++Index)
            {
                const FDesertBlockPlacement& Block=Source->Blocks[Index];
                if(Block.bEnabled && !Source->InvalidBlockIndices.Contains(Index) && Block.Cell==ContextCell+FIntVector(0,0,1)) bProtected=true;
            }
            if(bProtected) PreviewStyle->bEnableRoofDressing=false;
            Room->Style=PreviewStyle;
        }
        Room->Rebuild();
        Data.SelectedMesh=Style ? Style->RoomCellMesh.Get() : nullptr;
        Data.bUsesRecipe=!Data.SelectedMesh;
        Data.Description=Data.SelectedMesh ? TEXT("真实整房资源；门窗由模型提供") : TEXT("单格墙片组合展示；使用当前Style与门窗设置，未展示场景邻接或放置合法性");
    }
    else
    {
        EDesertModuleKind Kind=EDesertModuleKind::PotCluster;
        switch (Tool)
        {
        case 1: Kind=EDesertModuleKind::RoofPavilion; break;
        case 2: Kind=EDesertModuleKind::Dome; break;
        case 3: Kind=EDesertModuleKind::PotCluster; break;
        case 4: Kind=EDesertModuleKind::AwningBay; break;
        case 5:
            switch(Layout)
            {
            case EDesertStairLayout::OutwardLegacy: Kind=EDesertModuleKind::Stairs; break;
            case EDesertStairLayout::AlongWall: Kind=EDesertModuleKind::WallStairs; break;
            case EDesertStairLayout::LShape: Kind=EDesertModuleKind::LShapeStairs; break;
            case EDesertStairLayout::UShape: Kind=EDesertModuleKind::UShapeStairs; break;
            case EDesertStairLayout::Switchback: Kind=EDesertModuleKind::SwitchbackStairs; break; // 只供兼容数据检查。
            default: Data.Description=TEXT("无效梯型"); return Data;
            }
            break;
        case 7: Kind=EDesertModuleKind::Column; break;
        case 8: Kind=EDesertModuleKind::RubbleCluster; break;
        case 9: Kind=EDesertModuleKind::RoofCrown; break;
        default: Data.Description=TEXT("此编号不是可展示的组合块"); return Data;
        }
        UStaticMesh* Mesh=ADesertBuildingModule::GetStyleMesh(Style,Kind);
        if (Tool==4)
        {
            if (VariantIndex < -1) {Data.Description=TEXT("棚架变体下标无效"); return Data;}
            if (Style && !Style->ModuleAwningBayVariants.IsEmpty())
            {
                Data.ResolvedVariantIndex=Style->ResolveAwningBayVariantIndex(VariantIndex,ContextCell,Source->Seed);
                Mesh=Style->ResolveAwningBayMesh(VariantIndex,ContextCell,Source->Seed);
                if (!Mesh) {Data.Description=TEXT("当前棚架变体为空或越界；没有换用其他模型"); return Data;}
            }
            else if (VariantIndex>=0) {Data.Description=TEXT("当前Style没有该棚架变体"); return Data;}
        }
        if(Tool==3)
        {
            if(VariantIndex < -1) {Data.Description=TEXT("瓦罐组变体下标无效");return Data;}
            if(Style && !Style->ModulePotClusterVariants.IsEmpty())
            {
                Data.ResolvedVariantIndex=Style->ResolvePotClusterVariantIndex(VariantIndex,ContextCell,Source->Seed);
                Mesh=Style->ResolvePotClusterMesh(VariantIndex,ContextCell,Source->Seed);
                if(!Mesh) {Data.Description=TEXT("瓦罐整组变体为空或越界；没有换用其他模型");return Data;}
            }
            else if(VariantIndex>=0) {Data.Description=TEXT("当前Style没有该瓦罐组变体");return Data;}
        }
        if (Kind==EDesertModuleKind::RoofCrown && !Mesh)
        {Data.Description=TEXT("斜顶墙冠模型未配置；没有替代模型"); return Data;}
        ADesertBuildingModule* Module=Scene.GetWorld()->SpawnActor<ADesertBuildingModule>(FVector::ZeroVector,
            FRotator(0,Facing*90,0),Spawn);
        if (!Module) {Data.Description=TEXT("模块展示世界创建失败"); return Data;}
        Model=Module;
        Module->SetActorEnableCollision(false); Module->bCollision=false;
        Module->Style=Source->Style; Module->ModuleKind=Kind; Module->MeshOverride=Mesh;
        Module->ReferenceDimensions=ADesertBuildingModule::GetDefaultDimensions(Kind);
        Module->Dimensions=Module->ReferenceDimensions*FVector(Source->CellSize/300.f,Source->CellSize/300.f,Source->FloorHeight/300.f);
        Module->StepsCount=(Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs || Kind==EDesertModuleKind::SwitchbackStairs) ? 16 : 15;
        if (Style && Mesh)
        {
            if (Kind==EDesertModuleKind::WallStairs) Module->StepsCount=Style->WallStairsStepCount;
            if (Kind==EDesertModuleKind::Stairs) Module->StepsCount=Style->StraightStairsStepCount;
            if (Kind==EDesertModuleKind::LShapeStairs) Module->StepsCount=Style->LShapeStairsStepCount;
            if (Kind==EDesertModuleKind::UShapeStairs) Module->StepsCount=Style->UShapeStairsStepCount;
            if (Kind==EDesertModuleKind::SwitchbackStairs) Module->StepsCount=Style->SwitchbackStairsStepCount;
        }
        Module->Rebuild(); Data.SelectedMesh=Mesh; Data.bUsesRecipe=!Mesh;
        Data.Description=Mesh ? TEXT("实际模块网格与当前Style材质；仅展示外形，不代表此处可放") : TEXT("未配置正式网格，正在显示同种类程序配方；不是正式资产缩略图");
        if (Layout==EDesertStairLayout::Switchback && Tool==5) Data.Description+=TEXT("；旧折返仅兼容查看，新放置请用U形");
    }
    // 白模动态材质原Outer为短命展示Actor；复制到TransientPackage，让返回数据/视口持有引用。
    // 持久材质及Style映射资源保留原指针，避免改写素材或丢失精确映射身份。
    TMap<UMaterialInstanceDynamic*,UMaterialInstanceDynamic*> PreviewMaterials;
    TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Model);
    for (UInstancedStaticMeshComponent* Component : Components)
    {
        UStaticMesh* Mesh=Component ? Component->GetStaticMesh() : nullptr;
        if (!Mesh || Mesh->IsCompiling() || !Mesh->GetBoundingBox().IsValid) continue;
        for (int32 Instance=0;Instance<Component->GetInstanceCount();++Instance)
        {
            FDesertModulePreviewPart& Part=Data.Parts.AddDefaulted_GetRef(); Part.Mesh=Mesh;
            Component->GetInstanceTransform(Instance,Part.Transform,true);
            for (int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)
            {
                UMaterialInterface* Material=Component->GetMaterial(Slot);
                if (Style && Tool!=0 && Data.SelectedMesh)
                    if (const TObjectPtr<UMaterialInterface>* Replacement=Style->ModuleMaterialOverrides.Find(TObjectPtr<UMaterialInterface>(Mesh->GetMaterial(Slot))))
                        if (IsValid(Replacement->Get())) Material=Replacement->Get();
                if (UMaterialInstanceDynamic* Dynamic=Cast<UMaterialInstanceDynamic>(Material))
                    if (Dynamic->IsIn(Model))
                    {
                        UMaterialInstanceDynamic*& Copy=PreviewMaterials.FindOrAdd(Dynamic);
                        if(!Copy) Copy=DuplicateObject<UMaterialInstanceDynamic>(Dynamic,GetTransientPackage());
                        Material=Copy;
                    }
                Part.Materials.Add(Material);
            }
            Data.Bounds+=Mesh->GetBoundingBox().TransformBy(Part.Transform);
        }
    }
    if (Data.Parts.IsEmpty()) Data.Description+=TEXT("；当前资源尚未生成可见几何（可能仍在编译）");
    Model->Destroy();
    return Data;
}
