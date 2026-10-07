#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DesertBuildingModule.h"
#include "DesertBuilding.generated.h"

class UDesertBuildingStyle;
class UDesertBuildingDesign;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class USceneComponent;
class UPrimitiveComponent;

/** 手动指定露台入口：只切开屋顶矮墙，不破坏下面承重外墙。 */
USTRUCT(BlueprintType)
struct FDesertRoofOpening
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof")
    FIntVector Cell = FIntVector::ZeroValue;
    // 0前(-Y)，1右(+X)，2后(+Y)，3左(-X)。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="0", ClampMax="3"))
    int32 Side = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="60", Units="cm"))
    float Width = 150.0f;
};

/** 缺少新字段的旧素材保留0，不在升级时悄悄移动罐组。方向相对Facing。 */
UENUM(BlueprintType)
enum class EDesertPotPlacement : uint8
{
    LegacyCentered = 0 UMETA(DisplayName="旧版居中（兼容读取）"),
    Automatic UMETA(DisplayName="自动靠墙 / 屋顶边"),
    Front UMETA(DisplayName="前"),
    FrontRight UMETA(DisplayName="右前"),
    Right UMETA(DisplayName="右"),
    BackRight UMETA(DisplayName="右后"),
    Back UMETA(DisplayName="后"),
    BackLeft UMETA(DisplayName="左后"),
    Left UMETA(DisplayName="左"),
    FrontLeft UMETA(DisplayName="左前"),
    Center UMETA(DisplayName="中心")
};

/** 作者放置的是有语义和规则的组合块，网格零件由配方派生。 */
UENUM(BlueprintType)
enum class EDesertBlockType : uint8
{
    RoofPavilion UMETA(DisplayName="屋顶棚亭块"),
    Dome UMETA(DisplayName="穹顶冠部块"),
    PotCluster UMETA(DisplayName="瓦罐组合块"),
    AwningBay UMETA(DisplayName="棚布支架组合块"),
    Stairs UMETA(DisplayName="露台楼梯块"),
    RubbleCluster UMETA(DisplayName="外围散石组合块"),
    RoofCrown UMETA(DisplayName="斜顶墙冠块")
};

UENUM(BlueprintType)
enum class EDesertStairLayout : uint8
{
    OutwardLegacy UMETA(DisplayName="旧版朝外直梯（对照用）"),
    AlongWall UMETA(DisplayName="贴墙直梯 + 平台"),
    Switchback UMETA(DisplayName="旧折返梯（保留）"),
    LShape UMETA(DisplayName="L形转角双跑 + 中平台"),
    UShape UMETA(DisplayName="U形折返双跑 + 中平台")
};

UENUM(BlueprintType)
enum class EDesertRoomDoorMode : uint8
{
    Automatic UMETA(DisplayName="继承自动入口"),
    NoDoor UMETA(DisplayName="无门"),
    Front UMETA(DisplayName="前 -Y"),
    Right UMETA(DisplayName="右 +X"),
    Back UMETA(DisplayName="后 +Y"),
    Left UMETA(DisplayName="左 -X")
};

/** 外观属于这个已放房格；共享面仍内部连通，不创建两堵互相覆盖的隔墙。 */
USTRUCT(BlueprintType)
struct FDesertRoomAppearance
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room") FIntVector Cell = FIntVector::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room") EDesertRoomDoorMode DoorMode = EDesertRoomDoorMode::Automatic;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room") bool bOverrideWindows = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room", meta=(ClampMin="0", ClampMax="15", ToolTip="四面窗位：bit0前-Y、bit1右+X、bit2后+Y、bit3左-X。只在外露面生成；门所在面优先使用门墙。")) int32 WindowMask = 15;
};

struct FDesertDoorFace
{
    FIntVector Cell;
    int32 Side = -1;
};

USTRUCT(BlueprintType)
struct FDesertBlockPlacement
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block")
    EDesertBlockType Type = EDesertBlockType::PotCluster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ToolTip="组合块底部的格坐标。Z=0为外围地面；Z=1为首层屋顶表面。楼梯使用目标露台表面的格坐标。"))
    FIntVector Cell = FIntVector(0,-2,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="0", ClampMax="3", ToolTip="朝外方向：0前-Y，1右+X，2后+Y，3左-X；楼梯从这个方向的外围向露台爬升。"))
    int32 Facing = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(EditCondition="Type == EDesertBlockType::Stairs", EditConditionHides))
    EDesertStairLayout StairLayout = EDesertStairLayout::AlongWall;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(ClampMin="-1", EditCondition="Type == EDesertBlockType::AwningBay || Type == EDesertBlockType::PotCluster", EditConditionHides, ToolTip="棚架或瓦罐整组：-1按Seed和格坐标稳定选择；0、1、2等指定对应Style变体数组下标。空数组保留旧模型/白模；空项或越界明确拒绝，不换用别件。"))
    int32 VariantIndex = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block", meta=(EditCondition="Type == EDesertBlockType::PotCluster", EditConditionHides, ToolTip="整组平移：自动靠外墙或裸露屋顶边；九方向相对Facing。按实际包围盒留缝并避开门前通路和楼梯出口。旧素材未存此项时维持原居中。"))
    EDesertPotPlacement PotPlacement = EDesertPotPlacement::LegacyCentered;
};

struct FDesertSupportSpan
{
    FVector Bottom;
    FVector Top;
};

/** 预览与提交共享同一次规则求解；坐标和尺寸都是建筑Actor本地空间、厘米。 */
USTRUCT(BlueprintType)
struct FDesertPlacementSupport
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Placement") FVector Bottom = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Placement") FVector Top = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FDesertPlacementCheck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Placement") bool bAllowed = false;
    UPROPERTY(BlueprintReadOnly, Category="Placement") FString Reason;
    UPROPERTY(BlueprintReadOnly, Category="Placement") TArray<FBox> LocalBounds;
    UPROPERTY(BlueprintReadOnly, Category="Placement") TArray<FDesertPlacementSupport> Supports;
    UPROPERTY(BlueprintReadOnly, Category="Placement") bool bHasVisualRecipe = false;
    UPROPERTY(BlueprintReadOnly, Category="Placement") EDesertModuleKind ModuleKind = EDesertModuleKind::PotCluster;
    UPROPERTY(BlueprintReadOnly, Category="Placement") FTransform Placement = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly, Category="Placement") FVector Dimensions = FVector(140,110,85);
    UPROPERTY(BlueprintReadOnly, Category="Placement") int32 Steps = 15;
    // 规则求解选择的同一网格直接供预览和生成使用，不在界面重复随机。
    UPROPERTY(BlueprintReadOnly, Category="Placement") TObjectPtr<UStaticMesh> CustomMesh = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Placement") int32 ResolvedVariantIndex = INDEX_NONE;
};

struct FDesertResolvedBlock
{
    int32 Index = INDEX_NONE;
    FDesertPlacementCheck Check;
    int32 FloorIndex = 1;
};

/** 持久化楼层组件；同一层中同种网格/材质共享ISM，但不同层有独立材质与碰撞组件。 */
USTRUCT()
struct FDesertFloorComponentGroup
{
    GENERATED_BODY()
    UPROPERTY() int32 FloorIndex = 1;
    UPROPERTY() TObjectPtr<USceneComponent> Root = nullptr;
    // 顺序与基础组件模板一致，不依赖易变的组件名称猜测楼层。
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
};

/** 一栋建筑自己的材质槽缓存；不修改共享材质资源。 */
USTRUCT()
struct FDesertBuildingFadeSlot
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UInstancedStaticMeshComponent> Component = nullptr;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> BaseMaterial = nullptr;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial = nullptr;
    UPROPERTY(Transient)
    int32 MaterialIndex = 0;
};

/** 可编辑规则建筑：配置资产、地基和独立楼层结构由同一Actor管理。 */
UCLASS(Blueprintable)
class DESERTBUILDINGLAB_API ADesertBuilding : public AActor
{
    GENERATED_BODY()

public:
    ADesertBuilding();
    virtual void PostActorCreated() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
#if WITH_EDITOR
    virtual void PostEditUndo() override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
#endif

    // 格子不是墙模型。它记录“这里占了一间建筑体块”；同层邻居之间不生成墙。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Layout")
    TArray<FIntVector> Cells;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Room Appearance", meta=(ToolTip="逐房格门窗配置。空数组保留旧自动入口/随机窗；已有房间可在设计器点击选中后直接编辑。共享面不生成隔墙，内部门明确拒绝。"))
    TArray<FDesertRoomAppearance> RoomAppearanceOverrides;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Building|Room Appearance")
    TArray<int32> InvalidRoomAppearanceIndices;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Building|Room Appearance")
    FString LastRoomAppearanceMessage;
    UFUNCTION(BlueprintCallable, Category="Building|Room Appearance")
    bool SetRoomAppearance(FDesertRoomAppearance Appearance);
    UFUNCTION(BlueprintPure, Category="Building|Room Appearance")
    FDesertRoomAppearance GetRoomAppearance(FIntVector Cell) const;
    UFUNCTION(BlueprintPure, Category="Building|Room Appearance")
    int32 GetRoomDoorSide(FIntVector Cell) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Authoring", meta=(ToolTip="V3保存的建筑Blueprint启用：第一次拖入关卡时从配置生成。加载已经保存的关卡始终保留其现成实例，避免加载时地面未就绪导致丢失。V2 Actor默认关闭。"))
    bool bGenerateOnPlacement = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Authoring")
    TObjectPtr<UDesertBuildingDesign> DesignAsset = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Authoring")
    int32 AppliedDesignRevision = 0;
    UFUNCTION(BlueprintCallable, Category="Building|Authoring")
    void ApplyLinkedDesign(bool bRebuild = true);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Layout", meta=(ClampMin="100", ClampMax="1000", Units="cm"))
    float CellSize = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Layout", meta=(ClampMin="100", ClampMax="1000", Units="cm"))
    float FloorHeight = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Layout", meta=(ClampMin="10", Units="cm", ToolTip="地基从Z=0向下延伸的深度。手动下插进坡地；不会自动探测或贴合地形。"))
    float FoundationDepth = 80.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Appearance")
    TObjectPtr<UDesertBuildingStyle> Style = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Appearance")
    int32 Seed = 17;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Appearance")
    bool bShowWoodBeams = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Appearance")
    bool bEnableAwning = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Entrance", meta=(ToolTip="启用后只使用指定的一层外墙入口；坐标/外露墙/楼梯通路无效时明确显示原因，不自动换到另一面。关闭时保留原自动入口策略。"))
    bool bUseEntranceOverride = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Entrance", meta=(EditCondition="bUseEntranceOverride", ToolTip="指定入口所属首层房间格，Z必须为0且该格有有效房间。"))
    FIntVector EntranceOverrideCell = FIntVector::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Entrance", meta=(EditCondition="bUseEntranceOverride", ClampMin="0", ClampMax="3", ToolTip="指定外墙：0前-Y、1右+X、2后+Y、3左-X；相邻有房间或楼梯挡住门前通路时无效。"))
    int32 EntranceOverrideSide = 0;

    // 也可以不运行游戏，在详情面板修改坐标后点击下面两个按钮。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Edit Cell")
    FIntVector SelectedCell = FIntVector(1, 0, 1);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Roof Access", meta=(ClampMin="0", ClampMax="3", ToolTip="0前(-Y)，1右(+X)，2后(+Y)，3左(-X)。编辑器工具按Selected Cell和此方向放置直楼梯。"))
    int32 SelectedRoofSide = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Roof Access", meta=(TitleProperty="Cell"))
    TArray<FDesertRoofOpening> RoofOpenings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Blocks", meta=(TitleProperty="Cell"))
    TArray<FDesertBlockPlacement> Blocks;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Blocks")
    FDesertBlockPlacement SelectedBlock;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Rules", meta=(ToolTip="悬空体块自动生成四角支柱；先找本栋下层屋面，否则逐柱向下检测场景地面。找不到支撑的体块不显示，并报告原因。"))
    bool bAutoSupportColumns = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Rules", meta=(ClampMin="100", ClampMax="50000", Units="cm"))
    float SupportTraceDistance = 10000.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Rules")
    TArray<FString> ValidationMessages;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Rules")
    int32 ValidBlockCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Rules")
    int32 InvalidBlockCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Building|Rules")
    TArray<int32> InvalidCellIndices;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Building|Rules")
    TArray<int32> InvalidBlockIndices;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Rules")
    int32 SupportColumnCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Rules")
    FIntVector EntranceCell = FIntVector::ZeroValue;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Rules")
    int32 EntranceSide = -1;

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building|Blocks", meta=(DisplayName="添加 Selected Block 并检查规则"))
    void AddSelectedBlock();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building|Blocks", meta=(DisplayName="移除最后一个组合块"))
    void RemoveLastBlock();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building|Blocks", meta=(DisplayName="载入组合块规则示例（重置此建筑）"))
    void LoadRuleDemo();

    UFUNCTION(BlueprintPure, Category="Building|Authoring")
    FDesertPlacementCheck EvaluatePlacement(bool bRoom, const FDesertBlockPlacement& Proposal) const;
    UFUNCTION(BlueprintCallable, Category="Building|Authoring")
    bool TryAddPlacement(bool bRoom, const FDesertBlockPlacement& Proposal);
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building|Authoring", meta=(DisplayName="清理旧的无效输入条目"))
    int32 RemoveInvalidAuthoringEntries();
    UFUNCTION(BlueprintCallable, Category="Building|Authoring")
    int32 RemovePlacementAndDependents(bool bRoom, const FDesertBlockPlacement& Proposal);

    UFUNCTION(BlueprintCallable, Category="Building|Roof Access")
    bool SetRoofOpening(FIntVector Cell, int32 Side, float Width);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building")
    void Rebuild();

    // 临时预览Actor专用：完整布局仍参与规则求解，仅输出目标房间与它的地基。
    // 支柱由放置检查结果另行显示；不改变Cells，普通Rebuild仍生成整栋建筑。
    UFUNCTION(BlueprintCallable, Category="Building|Preview")
    void RebuildRoomPlacementPreview(FIntVector Cell);

    // CPD 0..15由本工具保留：共同建筑原点与世界转建筑局部坐标的三个轴。
    // 运行时主动移动建筑后可调用；编辑器构造、拖动和重建会自动刷新。
    UFUNCTION(BlueprintCallable, Category="Building|Materials")
    void UpdateBuildingMaterialCoordinates();

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building")
    void ResetDemo();

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building")
    void AddSelectedCell();

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Building")
    void RemoveSelectedCell();

    UFUNCTION(BlueprintCallable, Category="Building")
    bool AddCell(FIntVector Cell);

    UFUNCTION(BlueprintCallable, Category="Building")
    bool RemoveCell(FIntVector Cell);

    UFUNCTION(BlueprintPure, Category="Building")
    bool HasCell(FIntVector Cell) const;

    UFUNCTION(BlueprintPure, Category="Building")
    bool IsCellInBounds(FIntVector Cell) const;

    // 1完全显示，0完全淡出；材质须自行实现对应标量参数。地基不参与。
    UFUNCTION(BlueprintCallable, Category="Building|Occlusion")
    void SetOcclusionFade(float VisibleAmount);

    // 对接遮挡组件：第一层索引为1。返回的是实际独立组件，地基永不包含在楼层数组中。
    UFUNCTION(BlueprintPure, Category="Building|Structure")
    TArray<UInstancedStaticMeshComponent*> GetFoundationComponents() const;

    UFUNCTION(BlueprintPure, Category="Building|Structure")
    TArray<UInstancedStaticMeshComponent*> GetFloorComponents(int32 FloorIndex) const;

    UFUNCTION(BlueprintPure, Category="Building|Structure")
    int32 GetFloorCount() const;

    // 命中组件反查归属：0=地基，1..=楼层，-1=不属于本栋有效模型组件。
    UFUNCTION(BlueprintPure, Category="Building|Structure")
    int32 GetFloorIndexForComponent(UPrimitiveComponent* Component) const;

    UFUNCTION(BlueprintCallable, Category="Building|Occlusion")
    void SetFloorOcclusionFade(int32 FloorIndex, float VisibleAmount);

    UFUNCTION(BlueprintCallable, Category="Building|Occlusion")
    void RestoreAllOcclusionFade();

    UFUNCTION(BlueprintPure, Category="Building|Occlusion")
    float GetOcclusionFade() const { return CurrentOcclusionFade; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Occlusion")
    FName OcclusionFadeParameter = TEXT("OcclusionFade");

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Occlusion")
    float CurrentOcclusionFade = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Statistics")
    int32 CellCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Statistics")
    int32 VisibleWallCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Statistics")
    int32 RoofCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Statistics")
    int32 ParapetCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building|Statistics")
    int32 AwningCount = 0;

protected:
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<USceneComponent> BuildingRoot;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<USceneComponent> FoundationRoot;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<USceneComponent> FirstFloorRoot;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> RoomCellModules;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> WallParts;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> TrimParts;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> WoodParts;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> DarkParts;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> ClothParts;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> SolidWallModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> WindowWallModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> DoorWallModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> RoofModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> ParapetModules;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> AwningModules;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> FoundationParts;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> FoundationModules;

    // 第一层：前18桶 = 3基本形状×6材质角色，后11桶 = 正式组合模型；随关卡保存。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> BlockParts;

    // V0.4.2新增9桶：按实/窗/门×左/右/两端排列。楼层模板必须在旧41桶后追加。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> WallEndModules;

    // 必须排在旧41桶及端版9桶之后，不改变任何既有楼层模板索引。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> RubbleModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> RubbleParts;

    // V0.4.3固定新桶，原52模板保持原序号。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TObjectPtr<UInstancedStaticMeshComponent> RoofCrownModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components") TObjectPtr<UInstancedStaticMeshComponent> LShapeStairModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components") TObjectPtr<UInstancedStaticMeshComponent> UShapeStairModules;

    // 原动态变体模板排在墙冠之后；Art07以后新增变体排在新角桶之后。
    // 缩减Style数组时保留空桶，避免旧层索引移位。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> AwningVariantModules;

    // Art09动态资源只追加到稳定模板表，数组缩减时保留空桶，旧楼层下标不移位。
    UPROPERTY(VisibleAnywhere, Category="Building|Components") TArray<TObjectPtr<UInstancedStaticMeshComponent>> PotVariantModules;
    UPROPERTY(VisibleAnywhere, Category="Building|Components") TArray<TObjectPtr<UInstancedStaticMeshComponent>> RoofDressingModules;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Art09AdditionalTemplates;

    // Art07追加9桶：实/窗/门×左/右/两凸外角；不能插入旧楼层模板序列。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> WallOuterCornerModules;

    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TArray<FDesertFloorComponentGroup> AdditionalFloorGroups;
    // 地基以下的自动支柱也必须独立，不随上部楼层淡化。
    UPROPERTY(VisibleAnywhere, Category="Building|Components")
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> FoundationBlockParts;

private:
    enum class EWallType : uint8 { Solid, Window, Door };

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> DefaultBaseMaterial;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> FallbackMaterials;

    UPROPERTY(Transient)
    TArray<FDesertBuildingFadeSlot> OcclusionMaterialSlots;
    FName CachedOcclusionParameter = NAME_None;
    UPROPERTY(Transient) TMap<int32, float> FloorOcclusionAmounts;
    UPROPERTY(Transient) bool bPendingPlacementGeneration = false;
    int32 GenerationFloorIndex = 1;
    TOptional<FIntVector> RoomPlacementPreviewCell;

    TSet<FIntVector> Occupied;
    TArray<FIntVector> NormalizedCells;
    TArray<FDesertSupportSpan> SupportSpans;
    TArray<FBox> StairReservations;
    TArray<FDesertRoofOpening> EffectiveRoofOpenings;
    TSet<FIntVector> RoofCrownRoofCells;
    TSet<FIntVector> RoofDressingBlockedCells;

    // 首次重建时记录原棚架模板高水位。后续扩变体库只能追加在9个角桶之后。
    UPROPERTY()
    int32 WallOuterCornerVariantPrefix = INDEX_NONE;
    UPROPERTY() int32 StairTemplateVariantPrefix = INDEX_NONE;
    UPROPERTY() int32 Art09TemplateVariantPrefix = INDEX_NONE;

    void SetDemoCells();
    void NormalizeCells();
    void ConfigureMeshesAndMaterials();
    void BuildWall(const FIntVector& Cell, int32 Side, EWallType Type);
    int32 ResolveWallOuterCornerMask(const FIntVector& Cell, int32 Side) const;
    bool ResolveEntranceCandidate(const TSet<FIntVector>& Bodies, const TArray<FBox>& Stairs,
        FIntVector& OutCell, int32& OutSide, FString* OverrideError = nullptr) const;
    bool ValidateRoomAppearance(const FDesertRoomAppearance& Appearance, const TSet<FIntVector>& Bodies,
        const TArray<FBox>& Stairs, FString& Error) const;
    void ResolveDoorFaces(const TSet<FIntVector>& Bodies, const TArray<FBox>& Stairs, TArray<FDesertDoorFace>& Faces) const;
    void ResolveInvalidRoomAppearances(const TSet<FIntVector>& Bodies, const TArray<FBox>& Stairs,
        TArray<int32>& Indices, TArray<FString>& Messages) const;
    void BuildRoof(const FIntVector& Cell);
    void BuildRoofDressing(const FIntVector& Cell);
    void BuildAwning(const FIntVector& DoorCell, int32 DoorSide);
    void BuildFoundation(const FIntVector& Cell);
    void RestoreOcclusionMaterials();
    void RefreshOcclusionMaterials();
    void ApplyOcclusionMaterials();
    float GetComponentOcclusionAmount(const UInstancedStaticMeshComponent* Component) const;
    TArray<UInstancedStaticMeshComponent*> GetFloorTemplateComponents() const;
    UInstancedStaticMeshComponent* ResolveLayerComponent(UInstancedStaticMeshComponent* Template);
    void PrepareLayerComponents();
    void AddLayerInstance(UInstancedStaticMeshComponent* Template, const FTransform& Transform);
    void PrepareSupportedCells();
    void ResolveSupportedCells(const TArray<FIntVector>& Inputs, TSet<FIntVector>& OutOccupied,
        TArray<FDesertSupportSpan>& OutSupports, TArray<int32>& OutInvalidIndices, TArray<FString>& OutMessages) const;
    bool TraceGround(const FVector& LocalTop, float& LocalGroundZ) const;
    void ConfigureBlockParts();
    void ResolveAndBuildBlocks();
    void ResolveBlockLayout(const TSet<FIntVector>& Bodies, const TArray<FDesertSupportSpan>& Supports,
        const TArray<FDesertBlockPlacement>& Inputs, TArray<FDesertResolvedBlock>& OutBlocks) const;
    void EmitModule(EDesertModuleKind Kind, const FTransform& Placement, const FVector& Dimensions, int32 Steps = 15, bool WithProps = false,
        UStaticMesh* ResolvedMesh = nullptr, int32 AwningVariantIndex = INDEX_NONE);
    FVector CellBottomCenter(const FIntVector& Cell) const;
    FVector FaceOrigin(const FIntVector& Cell, int32 Side) const;
    void AddBox(UInstancedStaticMeshComponent* Component, const FVector& Center,
        const FVector& Size, const FQuat& Rotation = FQuat::Identity);
    void AddFaceBox(UInstancedStaticMeshComponent* Component, const FVector& Origin,
        const FQuat& Rotation, const FVector& LocalCenter, const FVector& Size);
};
