#include "DesertBuildingRoofDecorations.h"
#include "DesertBuilding.h"
#include "DesertBuildingStyle.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"

FString DesertRoofDecorationVariantLabel(int32 Index)
{
    static const TCHAR* Names[]={TEXT("木板平台"),TEXT("木板与陶罐"),TEXT("角落罐组"),TEXT("双角罐组"),TEXT("沿边三组"),TEXT("小木台与角罐")};
    return Index>=0 && Index<UE_ARRAY_COUNT(Names)?Names[Index]:TEXT("无效装饰布局");
}

TArray<FDesertRoofDecorationPart> ADesertBuilding::GetRoofDecorationParts(const FDesertRoofDecoration& Decoration) const
{
    TArray<FDesertRoofDecorationPart> Result;
    if(!Style || Decoration.VariantIndex<0 || Decoration.VariantIndex>5 || Decoration.Facing<0 || Decoration.Facing>3) return Result;
    const FVector Unit(CellSize/300.f,CellSize/300.f,FloorHeight/300.f);
    const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(90.f*Decoration.Facing));
    const FVector Center((Decoration.Cell.X+.5f)*CellSize,(Decoration.Cell.Y+.5f)*CellSize,
        Decoration.Cell.Z*FloorHeight+.3f*Unit.Z);
    bool bMissing=false;
    auto Add=[&](bool bPot,int32 ResourceIndex,FVector Offset,float Scale,float Yaw=0.f)
    {
        UStaticMesh* Mesh=bPot ? (Style->ModulePotClusterVariants.IsEmpty()?Style->ModulePotCluster.Get():
            (Style->ModulePotClusterVariants.IsValidIndex(ResourceIndex)?Style->ModulePotClusterVariants[ResourceIndex].Get():nullptr)) :
            (Style->RoofDressingVariants.IsValidIndex(ResourceIndex)?Style->RoofDressingVariants[ResourceIndex].Get():nullptr);
        if(!Mesh) {bMissing=true;return;}
        FDesertRoofDecorationPart& Part=Result.AddDefaulted_GetRef();
        Part.Mesh=Mesh;Part.bPotCluster=bPot;Part.ResourceIndex=bPot && Style->ModulePotClusterVariants.IsEmpty()?INDEX_NONE:ResourceIndex;
        Part.Transform=FTransform(Rotation*FQuat(FVector::UpVector,FMath::DegreesToRadians(Yaw)),
            Center+Rotation.RotateVector(Offset*Unit),Unit*Scale);
    };
    // 只组合已配置的资源，不随机替换空槽。罐组缩至65%，错开放置留出通路。
    const int32 PotCount=Style->ModulePotClusterVariants.Num();
    auto PotIndex=[&](int32 Choice){return PotCount>0?Choice%PotCount:INDEX_NONE;};
    switch(Decoration.VariantIndex)
    {
    case 0:Add(false,0,FVector::ZeroVector,1.f);break;
    case 1:Add(false,1,FVector::ZeroVector,1.f);break;
    case 2:Add(true,PotIndex(0),FVector(-62,63,0),.65f,-12.f);break;
    case 3:Add(true,PotIndex(1),FVector(-62,63,0),.65f,-12.f);Add(true,PotIndex(2),FVector(62,-63,0),.65f,168.f);break;
    case 4:Add(true,PotIndex(0),FVector(-67,70,0),.5f,0);Add(true,PotIndex(1),FVector(7,68,0),.5f,14.f);Add(true,PotIndex(2),FVector(70,65,0),.4f,-10.f);break;
    case 5:Add(false,0,FVector(-35,-30,0),.62f,90.f);Add(true,PotIndex(2),FVector(63,68,0),.65f,24.f);break;
    }
    if(bMissing) Result.Reset(); // 避免缺资源时把半套组合当作完整变体。
    return Result;
}

FDesertRoofDecorationCheck ADesertBuilding::EvaluateRoofDecoration(const FDesertRoofDecoration& Decoration) const
{
    FDesertRoofDecorationCheck Check;
    const TArray<FDesertRoofDecorationPart> Parts=GetRoofDecorationParts(Decoration);
    if(Parts.IsEmpty()) {Check.Reason=TEXT("装饰布局资源为空或参数无效；木板布局需要屋顶数组0/1，罐组使用瓦罐数组或单一组合槽。");return Check;}
    const FVector Unit(CellSize/300.f,CellSize/300.f,FloorHeight/300.f);
    const FVector Center((Decoration.Cell.X+.5f)*CellSize,(Decoration.Cell.Y+.5f)*CellSize,Decoration.Cell.Z*FloorHeight);
    for(const FDesertRoofDecorationPart& Part:Parts)
    {
        const FBox Box=Part.Mesh->GetBoundingBox().TransformBy(Part.Transform);Check.LocalBounds.Add(Box);
        if(!Box.IsValid || Box.Min.X<Center.X-121.f*Unit.X || Box.Max.X>Center.X+121.f*Unit.X ||
            Box.Min.Y<Center.Y-121.f*Unit.Y || Box.Max.Y>Center.Y+121.f*Unit.Y ||
            Box.Min.Z<Center.Z-.5f*Unit.Z || Box.Max.Z>Center.Z+101.f*Unit.Z)
        {Check.Reason=TEXT("装饰组合超出屋顶安全范围（240×240cm、最高100cm）；请检查资源尺寸与底心原点。");return Check;}
    }
    if(!Decoration.bEnabled) {Check.Reason=TEXT("该装饰已关闭，作者设置仍保留。");return Check;}
    const FIntVector Body=Decoration.Cell-FIntVector(0,0,1);
    TSet<FIntVector> Bodies;TArray<FDesertSupportSpan> Supports;TArray<int32> Invalid;TArray<FString> Messages;
    ResolveSupportedCells(Cells,Bodies,Supports,Invalid,Messages);
    if(Decoration.Cell.Z<1 || !Bodies.Contains(Body)) {Check.Reason=TEXT("装饰需要下方一格存在有效房间屋顶。");return Check;}
    if(Bodies.Contains(Decoration.Cell)) {Check.Reason=TEXT("上层房间覆盖此屋顶：装饰暂时隐藏，设置保留。");return Check;}
    for(const FDesertRoofOpening& Opening:RoofOpenings)
        if(Opening.Cell==Body) {Check.Reason=TEXT("屋顶有人工通道开口，装饰暂时隐藏，避免挡路。");return Check;}
    TArray<FDesertResolvedBlock> Resolved;ResolveBlockLayout(Bodies,Supports,Blocks,Resolved);
    for(const FDesertResolvedBlock& Block:Resolved)
    {
        if(!Block.Check.bAllowed || !Blocks.IsValidIndex(Block.Index)) continue;
        if(Blocks[Block.Index].Cell==Decoration.Cell)
        {Check.Reason=TEXT("此屋顶有建筑附件或楼梯出口，装饰暂时隐藏；装饰不占位。");return Check;}
        // 对相邻层楼梯的下端/通道同样避让，不只检查目标格。
        if(Blocks[Block.Index].Type==EDesertBlockType::Stairs)
        {
            const FDesertBlockPlacement& Stair=Blocks[Block.Index];
            if(Stair.StairConnection==EDesertStairConnection::AdjacentFloors && Stair.Cell.Z>1 &&
                Decoration.Cell.Z==Stair.Cell.Z-1)
            {
                const float MinX=Decoration.Cell.X*CellSize,MinY=Decoration.Cell.Y*CellSize;
                for(const FBox& StairBox:Block.Check.LocalBounds)
                    if(StairBox.Min.X<MinX+CellSize-KINDA_SMALL_NUMBER && StairBox.Max.X>MinX+KINDA_SMALL_NUMBER &&
                        StairBox.Min.Y<MinY+CellSize-KINDA_SMALL_NUMBER && StairBox.Max.Y>MinY+KINDA_SMALL_NUMBER)
                    {Check.Reason=TEXT("此屋顶承托相邻层楼梯或下端入口，整格点缀暂时隐藏，设置保留。");return Check;}
            }
            for(const FBox& StairBox:Block.Check.LocalBounds)
                for(const FBox& DecorBox:Check.LocalBounds)
                    if(StairBox.Intersect(DecorBox)) {Check.Reason=TEXT("此处属于楼梯通路，装饰暂时隐藏，设置保留。");return Check;}
        }
    }
    TArray<FBox> StairBounds;
    for(const FDesertResolvedBlock& Block:Resolved)
        if(Block.Check.bAllowed && Blocks.IsValidIndex(Block.Index) && Blocks[Block.Index].Type==EDesertBlockType::Stairs)
            StairBounds.Append(Block.Check.LocalBounds);
    TArray<FDesertDoorFace> Doors;ResolveDoorFaces(Bodies,StairBounds,Doors);
    const FIntVector Sides[]={FIntVector(0,-1,0),FIntVector(1,0,0),FIntVector(0,1,0),FIntVector(-1,0,0)};
    for(const FDesertDoorFace& Door:Doors)
        if(Door.Cell.Z>0 && Door.Side>=0 && Door.Side<4)
        {
            const FIntVector Terrace=Door.Cell+Sides[Door.Side]-FIntVector(0,0,1);
            if(Terrace==Body && Bodies.Contains(Terrace) && !Bodies.Contains(Terrace+FIntVector(0,0,1)))
            {Check.Reason=TEXT("此屋顶是高层门前露台，装饰暂时隐藏以保留门口通路。");return Check;}
        }
    Check.bAllowed=true;Check.Reason=TEXT("可点缀：只写装饰层，不占建筑格；重复点击替换该格装饰，Shift+左键删除。");
    return Check;
}

bool ADesertBuilding::SetRoofDecoration(FDesertRoofDecoration Decoration)
{
    if(!EvaluateRoofDecoration(Decoration).bAllowed) return false;
#if WITH_EDITOR
    Modify();
#endif
    RoofDecorations.RemoveAll([&](const FDesertRoofDecoration& Entry){return Entry.Cell==Decoration.Cell;});
    RoofDecorations.Add(Decoration);Rebuild();return true;
}

bool ADesertBuilding::RemoveRoofDecoration(FIntVector Cell)
{
    if(!RoofDecorations.ContainsByPredicate([&](const FDesertRoofDecoration& Entry){return Entry.Cell==Cell;})) return false;
#if WITH_EDITOR
    Modify();
#endif
    RoofDecorations.RemoveAll([&](const FDesertRoofDecoration& Entry){return Entry.Cell==Cell;});Rebuild();return true;
}

void ADesertBuilding::BuildManualRoofDecorations()
{
    if(RoomPlacementPreviewCell.IsSet()) return; // 房间悬浮预览不能再次输出已放置的屋顶装饰。
    TSet<FIntVector> Built;
    for(const FDesertRoofDecoration& Decoration:RoofDecorations)
    {
        if(Built.Contains(Decoration.Cell)) continue;Built.Add(Decoration.Cell);
        const FDesertRoofDecorationCheck Check=EvaluateRoofDecoration(Decoration);
        const FIntVector Body=Decoration.Cell-FIntVector(0,0,1);
        if(!Check.bAllowed || RoofDressingBlockedCells.Contains(Body))
        {if(Decoration.bEnabled) ValidationMessages.AddUnique(TEXT("屋顶装饰保留但暂未显示：")+(Check.bAllowed?TEXT("该屋顶由楼梯/附件使用。"):Check.Reason));continue;}
        GenerationFloorIndex=Decoration.Cell.Z;
        for(const FDesertRoofDecorationPart& Part:GetRoofDecorationParts(Decoration))
        {
            if(Part.bPotCluster)
            {
                FTransform Pose=Part.Transform;Pose.SetScale3D(FVector::OneVector);
                EmitModule(EDesertModuleKind::PotCluster,Pose,
                    ADesertBuildingModule::GetDefaultDimensions(EDesertModuleKind::PotCluster)*Part.Transform.GetScale3D(),15,false,Part.Mesh,Part.ResourceIndex);
            }
            else if(RoofDressingModules.IsValidIndex(Part.ResourceIndex))
                AddLayerInstance(RoofDressingModules[Part.ResourceIndex],Part.Transform);
        }
    }
}
