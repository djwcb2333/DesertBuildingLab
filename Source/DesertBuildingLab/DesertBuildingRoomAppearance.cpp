#include "DesertBuilding.h"
#include "DesertBuildingStyle.h"

namespace DesertRoomFaces
{
    static const FIntVector Sides[4] = { FIntVector(0,-1,0), FIntVector(1,0,0), FIntVector(0,1,0), FIntVector(-1,0,0) };
}

FDesertRoomAppearance ADesertBuilding::GetRoomAppearance(FIntVector Cell) const
{
    for (const FDesertRoomAppearance& Appearance : RoomAppearanceOverrides)
        if (Appearance.Cell == Cell) return Appearance;
    FDesertRoomAppearance Default; Default.Cell=Cell; return Default;
}

bool ADesertBuilding::ValidateRoomAppearance(const FDesertRoomAppearance& Appearance,
    const TSet<FIntVector>& Bodies, const TArray<FBox>& Stairs, FString& Error) const
{
    Error.Reset();
    if (!Bodies.Contains(Appearance.Cell)) Error=TEXT("门窗配置必须属于已有且有效的房间格");
    else if (static_cast<uint8>(Appearance.DoorMode)>static_cast<uint8>(EDesertRoomDoorMode::Left)) Error=TEXT("门朝向枚举无效");
    else if (Appearance.WindowMask<0 || Appearance.WindowMask>15) Error=TEXT("窗位掩码必须为0到15");
    else if (Style && Style->RoomCellMesh && (Appearance.DoorMode!=EDesertRoomDoorMode::Automatic || Appearance.bOverrideWindows))
        Error=TEXT("整房间模型自带门窗，不能自动切洞；请改用墙片套件，或恢复继承自动配置");
    else if (Appearance.DoorMode>=EDesertRoomDoorMode::Front)
    {
        const int32 Side=static_cast<int32>(Appearance.DoorMode)-2;
        const FIntVector Direction=DesertRoomFaces::Sides[Side];
        if (Bodies.Contains(Appearance.Cell+Direction)) Error=TEXT("门不能朝向相邻房间的共享面；内部自动连通，不生成隔墙或门墙");
        else
        {
            const FVector Edge=CellBottomCenter(Appearance.Cell)+FVector(Direction.X,Direction.Y,0)*CellSize*.5f;
            const FBox Entry=FBox(FVector(-CellSize*.2f,-80,0),FVector(CellSize*.2f,0,220))
                .TransformBy(FTransform(FRotator(0,Side*90,0),Edge)).ExpandBy(-.5f);
            for (const FBox& Stair : Stairs)
                if (Entry.Intersect(Stair)) { Error=TEXT("指定门前通路被楼梯实际占位或2m净高空间遮挡"); break; }
        }
    }
    return Error.IsEmpty();
}

void ADesertBuilding::ResolveInvalidRoomAppearances(const TSet<FIntVector>& Bodies, const TArray<FBox>& Stairs,
    TArray<int32>& Indices, TArray<FString>& Messages) const
{
    Indices.Reset();
    TSet<FIntVector> Seen;
    for (int32 Index=0;Index<RoomAppearanceOverrides.Num();++Index)
    {
        const FDesertRoomAppearance& Appearance=RoomAppearanceOverrides[Index];
        FString Error;
        if (Seen.Contains(Appearance.Cell)) Error=TEXT("同一房间格有重复门窗配置");
        else ValidateRoomAppearance(Appearance,Bodies,Stairs,Error);
        Seen.Add(Appearance.Cell);
        if (!Error.IsEmpty())
        {
            Indices.Add(Index);
            Messages.Add(FString::Printf(TEXT("RoomAppearanceOverrides[%d] %s 无效：%s。保留原数据；请直接修正或显式清理。"),Index,*Appearance.Cell.ToString(),*Error));
        }
    }
}

void ADesertBuilding::ResolveDoorFaces(const TSet<FIntVector>& Bodies, const TArray<FBox>& Stairs, TArray<FDesertDoorFace>& Faces) const
{
    Faces.Reset();
    if (Style && Style->RoomCellMesh) return;
    FIntVector Entrance; int32 Side=-1;
    if (ResolveEntranceCandidate(Bodies,Stairs,Entrance,Side)) Faces.Add({Entrance,Side});
    TSet<FIntVector> Seen;
    for (const FDesertRoomAppearance& Appearance : RoomAppearanceOverrides)
    {
        if (Seen.Contains(Appearance.Cell)) continue;
        Seen.Add(Appearance.Cell);
        FString Error;
        if (Appearance.DoorMode>=EDesertRoomDoorMode::Front && ValidateRoomAppearance(Appearance,Bodies,Stairs,Error) &&
            !Faces.ContainsByPredicate([&Appearance](const FDesertDoorFace& Face){return Face.Cell==Appearance.Cell;}))
            Faces.Add({Appearance.Cell,static_cast<int32>(Appearance.DoorMode)-2});
    }
}

int32 ADesertBuilding::GetRoomDoorSide(FIntVector Cell) const
{
    TSet<FIntVector> Bodies; TArray<FDesertSupportSpan> Supports; TArray<int32> Invalid; TArray<FString> Messages;
    TArray<FDesertResolvedBlock> Resolved; TArray<FBox> Stairs;
    ResolveSupportedCells(Cells,Bodies,Supports,Invalid,Messages);
    ResolveBlockLayout(Bodies,Supports,Blocks,Resolved);
    for (const FDesertResolvedBlock& Block : Resolved)
        if (Block.Check.bAllowed && Blocks[Block.Index].Type==EDesertBlockType::Stairs) Stairs.Append(Block.Check.LocalBounds);
    TArray<FDesertDoorFace> Faces; ResolveDoorFaces(Bodies,Stairs,Faces);
    for (const FDesertDoorFace& Face : Faces) if (Face.Cell==Cell) return Face.Side;
    return -1;
}

bool ADesertBuilding::SetRoomAppearance(FDesertRoomAppearance Appearance)
{
    const bool bReset=Appearance.DoorMode==EDesertRoomDoorMode::Automatic && !Appearance.bOverrideWindows;
    TSet<FIntVector> Bodies; TArray<FDesertSupportSpan> Supports; TArray<int32> Invalid; TArray<FString> Messages;
    TArray<FDesertResolvedBlock> BeforeBlocks; TArray<FBox> Stairs;
    ResolveSupportedCells(Cells,Bodies,Supports,Invalid,Messages);
    ResolveBlockLayout(Bodies,Supports,Blocks,BeforeBlocks);
    for (const FDesertResolvedBlock& Block : BeforeBlocks)
        if (Block.Check.bAllowed && Blocks[Block.Index].Type==EDesertBlockType::Stairs) Stairs.Append(Block.Check.LocalBounds);
    if (!bReset)
    {
        if (!ValidateRoomAppearance(Appearance,Bodies,Stairs,LastRoomAppearanceMessage)) return false;
    }
    TArray<FDesertRoomAppearance> Candidate=RoomAppearanceOverrides;
    Candidate.RemoveAll([&Appearance](const FDesertRoomAppearance& Existing){return Existing.Cell==Appearance.Cell;});
    if (!bReset) Candidate.Add(Appearance);
    {
        // 只交换作者配置供纯规则求解，不重建组件；作用域退出即恢复，再决定是否正式提交。
        TGuardValue<TArray<FDesertRoomAppearance>> CandidateScope(RoomAppearanceOverrides,Candidate);
        TArray<FDesertResolvedBlock> AfterBlocks; ResolveBlockLayout(Bodies,Supports,Blocks,AfterBlocks);
        for (const FDesertResolvedBlock& Before : BeforeBlocks)
        {
            if (!Before.Check.bAllowed) continue;
            const FDesertResolvedBlock* After=AfterBlocks.FindByPredicate([&Before](const FDesertResolvedBlock& Block){return Block.Index==Before.Index;});
            if (!After || !After->Check.bAllowed)
            { LastRoomAppearanceMessage=TEXT("门窗修改会使已有组合块失效：")+(After ? After->Check.Reason : TEXT("支撑变化")); return false; }
        }
    }
    Modify();
    RoomAppearanceOverrides=MoveTemp(Candidate);
    LastRoomAppearanceMessage=bReset ? TEXT("已恢复该房间继承自动门窗") : TEXT("已更新选中房间门窗；共享面自动隐藏，门面优先于窗位");
    Rebuild();
    return true;
}
