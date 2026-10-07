#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DesertBuildingStyle.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * 建筑的“美术皮肤”。不保存格子和相邻关系：换一份 Style 就能复用同一套生成规则。
 * 单位为厘米。所有墙模块本地 +X 向右，+Z 向上，外侧朝 -Y。
 * 墙：300×20×300，原点在底边中心；屋顶：300×300×16，原点在底面中心。
 * 女儿墙：300×20×45，原点在底边中心。模块为空时使用自带的积木白模。
 */
UCLASS(BlueprintType)
class DESERTBUILDINGLAB_API UDesertBuildingStyle : public UDataAsset
{
    GENERATED_BODY()

public:
    // V3 整房间入口：便于美术直接选择一个完成建模的房间，无需先拆墙片。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room Module", meta=(ToolTip="可选整房间：300×300×300cm，原点在底面中心，X/Y范围-150到150，Z范围0到300。模型自带门窗、楼板及屋面；填写后替代程序墙/楼板/屋顶，地基和附件仍独立。保留模型自身材质和碰撞。"))
    TObjectPtr<UStaticMesh> RoomCellMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room Module", meta=(ToolTip="开启时按模型实际包围盒自动对齐底面中心，并缩放到格宽×格宽×层高；适合首次导入模型。关闭时遵守300×300×300cm、底面中心建模规范，可保留超出房体的装饰。"))
    bool bFitRoomCellMeshToGrid = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="实墙：300宽×20厚×300高，底边中心，外侧-Y。"))
    TObjectPtr<UStaticMesh> WallSolid = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="带真实窗洞的墙，外壳和原点与实墙一致。"))
    TObjectPtr<UStaticMesh> WallWindow = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="带真实门洞的墙，外壳和原点与实墙一致。"))
    TObjectPtr<UStaticMesh> WallDoor = nullptr;

    // V0.4.2：按原白模平接规则选择端部让位资产；旧皮肤默认关闭，保持既有生成行为。
    // 不重心对齐、不压缩门窗：仍使用名义300cm墙片的底边中心与缩放比例。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="启用自定义墙端平接。完整墙仍使用WallSolid/WallWindow/WallDoor；左右墙根据前后邻接选择让位20cm的端版。需要的端版为空时显示警告并生成按原规则让位的白模，避免回退到已知重叠的完整墙。整房RoomCellMesh填写时此套墙片不参与生成。"))
    bool bUseWallEndVariants = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="实墙左端让位：X[-130,150]、Y[-10,10]、Z[0,300]cm；原点仍为名义300cm墙底边中心。"))
    TObjectPtr<UStaticMesh> WallSolidTrimLeft = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="实墙右端让位：X[-150,130]、Y[-10,10]、Z[0,300]cm；保持原点。"))
    TObjectPtr<UStaticMesh> WallSolidTrimRight = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="实墙两端让位：X[-130,130]、Y[-10,10]、Z[0,300]cm；保持原点。"))
    TObjectPtr<UStaticMesh> WallSolidTrimBoth = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="窗墙左端让位20cm；窗洞的位置、宽度和高度与完整墙完全相同，不随端部变短缩放。"))
    TObjectPtr<UStaticMesh> WallWindowTrimLeft = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="窗墙右端让位20cm；保留中心窗洞和名义300cm墙原点。"))
    TObjectPtr<UStaticMesh> WallWindowTrimRight = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="窗墙两端各让位20cm；保留中心窗洞和名义300cm墙原点。"))
    TObjectPtr<UStaticMesh> WallWindowTrimBoth = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="门墙左端让位20cm；门洞的位置、净宽和净高与完整墙完全相同，不缩放洞口。"))
    TObjectPtr<UStaticMesh> WallDoorTrimLeft = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="门墙右端让位20cm；保留中心门洞和名义300cm墙原点。"))
    TObjectPtr<UStaticMesh> WallDoorTrimRight = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall End Modules", meta=(ToolTip="门墙两端各让位20cm；保留中心门洞和名义300cm墙原点。"))
    TObjectPtr<UStaticMesh> WallDoorTrimBoth = nullptr;

    /** WallType: 0实墙、1窗墙、2门墙；EndMask: 0完整、1左端、2右端、3两端。
     * 关闭新规则时始终返回完整墙；开启时缺端版返回空，由生成器明确警告并走白模让位。
     */
    UFUNCTION(BlueprintPure, Category="Wall End Modules")
    UStaticMesh* ResolveWallMesh(int32 WallType, int32 EndMask) const
    {
        if (WallType < 0 || WallType > 2 || EndMask < 0 || EndMask > 3) return nullptr;
        if (!bUseWallEndVariants || EndMask == 0)
            return WallType == 0 ? WallSolid.Get() : (WallType == 1 ? WallWindow.Get() : WallDoor.Get());
        if (WallType == 0)
            return EndMask == 1 ? WallSolidTrimLeft.Get() : (EndMask == 2 ? WallSolidTrimRight.Get() : WallSolidTrimBoth.Get());
        if (WallType == 1)
            return EndMask == 1 ? WallWindowTrimLeft.Get() : (EndMask == 2 ? WallWindowTrimRight.Get() : WallWindowTrimBoth.Get());
        return EndMask == 1 ? WallDoorTrimLeft.Get() : (EndMask == 2 ? WallDoorTrimRight.Get() : WallDoorTrimBoth.Get());
    }

    // Art07：仅前/后完整墙拥有的凸外角软化；不改侧墙20cm让位、直拼接面或洞口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners", meta=(ToolTip="可选外露凸角小圆角。须同时启用Wall End Variants；仅角点同层四格中恰好本格占用时使用Full墙软角版。直拼、凹角、对角接触保留原接面。缺少软角版时警告并保留合法完整墙；旧Style默认关闭。"))
    bool bUseWallOuterCornerVariants = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners", meta=(ToolTip="完整实墙仅本地左外角(-150,-10)做3cm小圆角；300×20×300底边中心，其他接面不动。"))
    TObjectPtr<UStaticMesh> WallSolidOuterSoftLeft = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners")
    TObjectPtr<UStaticMesh> WallSolidOuterSoftRight = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners")
    TObjectPtr<UStaticMesh> WallSolidOuterSoftBoth = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners", meta=(ToolTip="完整窗墙外角小圆角；窗洞中心、内接面、名义300cm原点和缩放不动。"))
    TObjectPtr<UStaticMesh> WallWindowOuterSoftLeft = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners")
    TObjectPtr<UStaticMesh> WallWindowOuterSoftRight = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners")
    TObjectPtr<UStaticMesh> WallWindowOuterSoftBoth = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners", meta=(ToolTip="完整门墙外角小圆角；门净宽、净高、中心、内接面和原点不动。"))
    TObjectPtr<UStaticMesh> WallDoorOuterSoftLeft = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners")
    TObjectPtr<UStaticMesh> WallDoorOuterSoftRight = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wall Outer Corners")
    TObjectPtr<UStaticMesh> WallDoorOuterSoftBoth = nullptr;

    /** CornerMask: 1左外角、2右外角、3两外角。与原EndMask的20cm让位是不同规则。 */
    UFUNCTION(BlueprintPure, Category="Wall Outer Corners")
    UStaticMesh* ResolveWallOuterCornerMesh(int32 WallType, int32 CornerMask) const
    {
        if (!bUseWallOuterCornerVariants || !bUseWallEndVariants ||
            WallType < 0 || WallType > 2 || CornerMask < 1 || CornerMask > 3) return nullptr;
        if (WallType == 0)
            return CornerMask == 1 ? WallSolidOuterSoftLeft.Get() :
                (CornerMask == 2 ? WallSolidOuterSoftRight.Get() : WallSolidOuterSoftBoth.Get());
        if (WallType == 1)
            return CornerMask == 1 ? WallWindowOuterSoftLeft.Get() :
                (CornerMask == 2 ? WallWindowOuterSoftRight.Get() : WallWindowOuterSoftBoth.Get());
        return CornerMask == 1 ? WallDoorOuterSoftLeft.Get() :
            (CornerMask == 2 ? WallDoorOuterSoftRight.Get() : WallDoorOuterSoftBoth.Get());
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="屋顶板：300×300×16，底面中心，不包含四周女儿墙。"))
    TObjectPtr<UStaticMesh> RoofTile = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="开启后正式屋顶板使用现有邻接内收规则：外露边缩入墙厚，内部相邻边保留300cm对接；防止屋顶外侧与外墙共面。适用于不带边沿装饰的300×300×16cm基础板。旧Style默认关闭，保留既有资产行为。"))
    bool bInsetCustomRoofToWalls = false;

    // Art09：屋顶装饰不替代结构楼板；默认关闭，旧Style外观保持不变。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roof Dressing", meta=(DisplayName="启用屋顶木板/木板与陶罐", ToolTip="只在无组合块和露台开口的外露顶格生成装饰，避免堵住楼梯出口。原楼板与女儿墙仍存在。"))
    bool bEnableRoofDressing = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roof Dressing", meta=(DisplayName="屋顶装饰按格随机", EditCondition="bEnableRoofDressing", ToolTip="按建筑Seed和格坐标稳定选择整组；关闭时全部使用下方指定下标，不随重建或添加别格跳动。"))
    bool bRandomizeRoofDressing = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roof Dressing", meta=(DisplayName="固定屋顶装饰下标", ClampMin="0", EditCondition="bEnableRoofDressing && !bRandomizeRoofDressing"))
    int32 RoofDressingVariantIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roof Dressing", meta=(DisplayName="屋顶装饰整组资源", ToolTip="木板 / 木板+陶罐等整组，240×240cm范围内，底面中心Z0，高≤100cm。按作者单位缩放，保留每件实际轮廓，不按BBox拉成方格。空项或越界明确提示，不换用其他项。"))
    TArray<TObjectPtr<UStaticMesh>> RoofDressingVariants;
    UFUNCTION(BlueprintPure, Category="Roof Dressing")
    int32 ResolveRoofDressingVariantIndex(FIntVector Cell,int32 BuildingSeed) const
    {
        if(RoofDressingVariants.IsEmpty()) return INDEX_NONE;
        if(!bRandomizeRoofDressing) return RoofDressingVariants.IsValidIndex(RoofDressingVariantIndex)?RoofDressingVariantIndex:INDEX_NONE;
        uint32 Hash=static_cast<uint32>(BuildingSeed)^0xD83A91F5u;
        Hash=Hash*1664525u+static_cast<uint32>(Cell.X)*73856093u;
        Hash=Hash*1664525u+static_cast<uint32>(Cell.Y)*19349663u;
        Hash=Hash*1664525u+static_cast<uint32>(Cell.Z)*83492791u;
        Hash^=Hash>>16;
        return static_cast<int32>(Hash%static_cast<uint32>(RoofDressingVariants.Num()));
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="女儿墙直段：300×20×45，底边中心，外侧-Y。角部按平接缩短。"))
    TObjectPtr<UStaticMesh> Parapet = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="门口篷布与木架组合：宽240、向-Y伸出180、高250；原点在贴墙底边中心。保留模型材质槽。"))
    TObjectPtr<UStaticMesh> AwningModule = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Modules", meta=(ToolTip="地基：300×300×100，原点在上表面中心，Z范围-100到0。地基不参与遮挡淡化。"))
    TObjectPtr<UStaticMesh> Foundation = nullptr;

    // V2：独立附件的整模块网格。空槽使用程序白模，填写后保留模型自己的全部材质槽。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="支柱：60×60×300厘米，底面中心；含柱脚与柱头。"))
    TObjectPtr<UStaticMesh> ModuleColumn = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="楼梯：120宽×450进深×300高；原点在低端落地边中心，沿+Y上升。"))
    TObjectPtr<UStaticMesh> ModuleStairs = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="屋顶棚亭：280×280×220厘米，底面中心，包含支架与顶棚。"))
    TObjectPtr<UStaticMesh> ModuleRoofPavilion = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="穹顶与檐座：280×280×180厘米，底面中心。"))
    TObjectPtr<UStaticMesh> ModuleDome = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="瓦罐：55×55×80厘米，底面中心；正式模型建议制作真实罐口。"))
    TObjectPtr<UStaticMesh> ModulePot = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="篮筐：65×65×48厘米，底面中心。"))
    TObjectPtr<UStaticMesh> ModuleBasket = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="木箱：70×60×60厘米，底面中心。"))
    TObjectPtr<UStaticMesh> ModuleCrate = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="瓦罐组：140×110×85厘米，底面中心。用一组有大小变化的罐子替换程序组合。"))
    TObjectPtr<UStaticMesh> ModulePotCluster = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(DisplayName="瓦罐整组变体", ToolTip="每项为140×110×85cm基准的完整摆放组合；原点底面中心。Block.VariantIndex=-1按Seed和格坐标稳定选择，其余指定下标。数组为空时保留旧ModulePotCluster；非空数组空项/越界明确拒绝，不换用别件。"))
    TArray<TObjectPtr<UStaticMesh>> ModulePotClusterVariants;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(DisplayName="独立陶罐造型资源库存", ToolTip="供美术制作/替换组合时复用的独立造型网格。不自动生成独立可拾取物，也不把每只罐当建筑占用格。"))
    TArray<TObjectPtr<UStaticMesh>> PotShapeMeshes;
    UFUNCTION(BlueprintPure, Category="Attachment Modules")
    int32 ResolvePotClusterVariantIndex(int32 RequestedVariantIndex,FIntVector Cell,int32 BuildingSeed) const
    {
        if(RequestedVariantIndex < -1 || ModulePotClusterVariants.IsEmpty()) return INDEX_NONE;
        if(RequestedVariantIndex>=0) return ModulePotClusterVariants.IsValidIndex(RequestedVariantIndex)?RequestedVariantIndex:INDEX_NONE;
        uint32 Hash=static_cast<uint32>(BuildingSeed)^0xC71A0B39u;
        Hash=Hash*1664525u+static_cast<uint32>(Cell.X)*73856093u;
        Hash=Hash*1664525u+static_cast<uint32>(Cell.Y)*19349663u;
        Hash=Hash*1664525u+static_cast<uint32>(Cell.Z)*83492791u;
        Hash^=Hash>>16;
        return static_cast<int32>(Hash%static_cast<uint32>(ModulePotClusterVariants.Num()));
    }
    UFUNCTION(BlueprintPure, Category="Attachment Modules")
    UStaticMesh* ResolvePotClusterMesh(int32 RequestedVariantIndex,FIntVector Cell,int32 BuildingSeed) const
    {
        if(RequestedVariantIndex < -1) return nullptr;
        if(ModulePotClusterVariants.IsEmpty()) return RequestedVariantIndex==-1?ModulePotCluster.Get():nullptr;
        const int32 Index=ResolvePotClusterVariantIndex(RequestedVariantIndex,Cell,BuildingSeed);
        return ModulePotClusterVariants.IsValidIndex(Index)?ModulePotClusterVariants[Index].Get():nullptr;
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="棚架块：280×200×240厘米，底面中心；包括棚布、四根柱子和梁，前侧为-Y。"))
    TObjectPtr<UStaticMesh> ModuleAwningBay = nullptr;

    // V0.4.3：每项是一整套篷布、梁、杆和柱脚，不只是附加装饰。
    // 空数组保留旧ModuleAwningBay/白模；非空数组不跳过空槽或回退旧模型。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="篷布+支架整组合变体。名义280×200×240cm，前-Y，背部挂墙平面为实际Max.Y。每项按同一格宽/层高比例缩放，保留各自形状与材质。空数组使用旧ModuleAwningBay；数组非空时，空项或越界索引会拒绝放置，不悄悄换模型。自动选择由建筑Seed和格坐标决定。"))
    TArray<TObjectPtr<UStaticMesh>> ModuleAwningBayVariants;

    UFUNCTION(BlueprintPure, Category="Attachment Modules")
    int32 ResolveAwningBayVariantIndex(int32 RequestedVariantIndex, FIntVector Cell, int32 BuildingSeed) const
    {
        if (RequestedVariantIndex < -1 || ModuleAwningBayVariants.IsEmpty()) return INDEX_NONE;
        if (RequestedVariantIndex >= 0)
            return ModuleAwningBayVariants.IsValidIndex(RequestedVariantIndex) ? RequestedVariantIndex : INDEX_NONE;
        // 不依赖Blocks数组下标、朝向或添加顺序，旋转/删别的模块不会换外形。
        uint32 Hash = static_cast<uint32>(BuildingSeed) ^ 0xA6B4C921u;
        Hash = Hash * 1664525u + static_cast<uint32>(Cell.X) * 73856093u;
        Hash = Hash * 1664525u + static_cast<uint32>(Cell.Y) * 19349663u;
        Hash = Hash * 1664525u + static_cast<uint32>(Cell.Z) * 83492791u;
        Hash ^= Hash >> 16;
        return static_cast<int32>(Hash % static_cast<uint32>(ModuleAwningBayVariants.Num()));
    }

    UFUNCTION(BlueprintPure, Category="Attachment Modules")
    UStaticMesh* ResolveAwningBayMesh(int32 RequestedVariantIndex, FIntVector Cell, int32 BuildingSeed) const
    {
        if (RequestedVariantIndex < -1) return nullptr;
        if (ModuleAwningBayVariants.IsEmpty())
            return RequestedVariantIndex == -1 ? ModuleAwningBay.Get() : nullptr;
        const int32 Index = ResolveAwningBayVariantIndex(RequestedVariantIndex, Cell, BuildingSeed);
        return ModuleAwningBayVariants.IsValidIndex(Index) ? ModuleAwningBayVariants[Index].Get() : nullptr;
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="贴墙楼梯组合：600沿墙长×120向外宽×300高。原点为屋顶出口的地面投影，+Y进建筑，出口(0,0,300)。包围盒X[-540,60]、Y[-120,0]；包含底平台、沿+X直梯、顶部转角平台。"))
    TObjectPtr<UStaticMesh> ModuleWallStairs = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="折返楼梯组合：300宽×400深×300高。原点为屋顶出口的地面投影，+Y进建筑，出口(0,0,300)。包围盒X[-225,75]、Y[-400,0]、Z[-15,300]；底部从左侧(-225,-50,0)进入。含底/中/顶三平台，默认16级。"))
    TObjectPtr<UStaticMesh> ModuleSwitchbackStairs = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="L形双跑：名义510×510×300cm；原点为屋顶出口地面投影，+Y入屋。实际X[-435,75]/Y[-510,0]/Z[-15,300]。16级，各跑8级；净宽120、踏深30、中平台150×150。左前下入口(-375,-435,0)，右后出口(0,0,300)。内空矩形X[-435,-75]/Y[-360,0]必须无几何/碰撞。"))
    TObjectPtr<UStaticMesh> ModuleLShapeStairs = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="U形双跑：名义300×480×300cm；实际X[-225,75]/Y[-480,0]/Z[-15,300]。16级，各跑8级；净宽120、踏深30、中平台120深。左近墙下入口(-150,-60,0)，右近墙上出口(0,0,300)。"))
    TObjectPtr<UStaticMesh> ModuleUShapeStairs = nullptr;
    // 固定整梯模型不能从假想Steps推断通行；这些数值必须与模型实际踏步一致。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", ClampMax="128")) int32 StraightStairsStepCount = 15;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float StraightStairsClearWidth = 90;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float StraightStairsTreadDepth = 30;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", ClampMax="128", ToolTip="旧Art07贴墙梯15级；Art08净宽120的新直梯16级，踏深22.5cm。改变此值不会重建静态模型踏步。")) int32 WallStairsStepCount = 15;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float WallStairsClearWidth = 90;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float WallStairsTreadDepth = 28;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="2", ClampMax="128")) int32 SwitchbackStairsStepCount = 16;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float SwitchbackStairsClearWidth = 120;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float SwitchbackStairsTreadDepth = 25;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="2", ClampMax="128")) int32 LShapeStairsStepCount = 16;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float LShapeStairsClearWidth = 120;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float LShapeStairsTreadDepth = 30;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="2", ClampMax="128")) int32 UShapeStairsStepCount = 16;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float UShapeStairsClearWidth = 120;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules|Stair Contract", meta=(ClampMin="1", Units="cm")) float UShapeStairsTreadDepth = 30;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="外围散石组合：220×160×65cm，底面中心，X±110/Y±80/Z0..65。整组手动放置，仅允许首层建筑外围地面、避开门和楼梯通路；默认无碰撞，不阻挡Pawn，归地基环境组不随楼层淡化。不是自动随机散布。"))
    TObjectPtr<UStaticMesh> ModuleRubbleCluster = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attachment Modules", meta=(ToolTip="斜顶墙冠：名义300×300×180cm，底面Z=0，XY底面中心。仅有效外露屋顶；与棚亭/穹顶不同，是永久墙壳。真实包围盒参与占用并归对应楼层淡化。此槽缺失时明确拒绝，不换成穹顶或棚亭。"))
    TObjectPtr<UStaticMesh> ModuleRoofCrown = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials")
    TObjectPtr<UMaterialInterface> WallMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials")
    TObjectPtr<UMaterialInterface> TrimMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials")
    TObjectPtr<UMaterialInterface> WoodMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials")
    TObjectPtr<UMaterialInterface> DarkMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials")
    TObjectPtr<UMaterialInterface> ClothMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials")
    TObjectPtr<UMaterialInterface> FoundationMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials", meta=(ToolTip="独立瓦罐白模的陶土材质。自定义整模块使用模型自带材质。"))
    TObjectPtr<UMaterialInterface> PotMaterial = nullptr;

    // 只覆盖组件，不改StaticMesh资产。键是模型材质槽里的原始材质资产；
    // 值是本风格使用的替换材质。不按名字/槽序号猜家族，也不递归追踪MI父材质。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Materials", meta=(DisplayName="模块材质替换（原材质 → 新材质）", ToolTip="用于正式自定义模型：选择模型材质槽实际引用的原材质，再指定此建筑风格使用的新材质或材质实例。按资产精确匹配，未配置或新材质为空的槽保留原样；不修改模型资产，不影响其他建筑风格。不做链式替换。墙、屋顶、地基、楼梯及棚架等自定义模块都适用；程序白模仍使用上方材质设置。"))
    TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> ModuleMaterialOverrides;
};
