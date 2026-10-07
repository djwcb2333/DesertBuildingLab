#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DesertBuildingModule.generated.h"

class UDesertBuildingStyle;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** 建筑规则系统使用的视觉块种类；合法位置由建筑块规则决定。 */
UENUM(BlueprintType)
enum class EDesertModuleKind : uint8
{
    Column UMETA(DisplayName="Column / 支柱"),
    Stairs UMETA(DisplayName="Stairs / 直楼梯"),
    RoofPavilion UMETA(DisplayName="Roof Pavilion / 屋顶棚亭"),
    Dome UMETA(DisplayName="Dome / 穹顶"),
    Pot UMETA(DisplayName="Pot / 瓦罐"),
    Basket UMETA(DisplayName="Basket / 篮筐"),
    Crate UMETA(DisplayName="Crate / 木箱"),
    PotCluster UMETA(DisplayName="Pot Cluster / 瓦罐组"),
    AwningBay UMETA(DisplayName="Awning Bay / 篷布支架组合"),
    WallStairs UMETA(DisplayName="Wall Stairs / 贴墙带平台楼梯"),
    SwitchbackStairs UMETA(DisplayName="Switchback Stairs / 折返楼梯"),
    RubbleCluster UMETA(DisplayName="Rubble Cluster / 外围散石组合"),
    RoofCrown UMETA(DisplayName="Roof Crown / 斜顶墙冠"),
    LShapeStairs UMETA(DisplayName="L Shape Stairs / L形转角梯"),
    UShapeStairs UMETA(DisplayName="U Shape Stairs / U形折返梯")
};

/** 纯数据配方项。与Actor/World无关，Shape:0立方体/1圆柱/2球，MaterialRole:0墙/1压顶/2木/3布/4暗/5陶。 */
struct DESERTBUILDINGLAB_API FDesertModulePart
{
    int32 Shape = 0;
    int32 MaterialRole = 0;
    FTransform Transform = FTransform::Identity;
};

USTRUCT()
struct FDesertModuleFadeSlot
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Component = nullptr;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BaseMaterial = nullptr;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial = nullptr;
    UPROPERTY(Transient) int32 MaterialIndex = 0;
};

/**
 * 建筑块的底层视觉组件：一组瓦罐、一整段楼梯、一套顶棚都只占一个块。
 * 本类只生成视觉和碰撞，不自行判定屋顶、地面、支撑等合法位置；这些由所属建筑管理。
 * 生成结果存在默认子对象 ISM 中，随关卡保存；无 Tick、输入或 BeginPlay 建造逻辑。
 * 普通模块底面中心；旧Stairs低端落地边中心，+Y上升。
 * WallStairs/SwitchbackStairs的原点为屋顶出口在外墙线上的垂直地面投影，出口恒为(0,0,Dimensions.Z)。
 * 新楼梯+Y进建筑、-Y朝外围，非对称占地详见纯配方函数和Style槽说明。
 */
UCLASS(Blueprintable)
class DESERTBUILDINGLAB_API ADesertBuildingModule : public AActor
{
    GENERATED_BODY()
public:
    ADesertBuildingModule();
    // 主建筑的规则系统也调用同一份配方，并将其装入自己预创建的ISM，不需要生成临时Actor。
    static void MakeVisualRecipe(EDesertModuleKind Kind, FVector Size, int32 Steps, bool bWithProps, bool bWithBase, TArray<FDesertModulePart>& OutParts);
    static FVector GetDefaultDimensions(EDesertModuleKind Kind);
    static UStaticMesh* GetStyleMesh(const UDesertBuildingStyle* InStyle, EDesertModuleKind Kind);
    virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditUndo() override;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Shape", meta=(ToolTip="在编辑器切换种类时，重置为该类推荐尺寸和碰撞。已有摆放位置保持不变。"))
    EDesertModuleKind ModuleKind = EDesertModuleKind::Column;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Shape", meta=(Units="cm", ClampMin="1", ClampMax="5000", ToolTip="本地X/Y/Z尺寸，厘米。旧Stairs:Y为直梯进深；WallStairs:X为沿墙总长、Y为向外宽度；SwitchbackStairs:X为两梯总宽、Y为向外进深。Z总爬升。"))
    FVector Dimensions = FVector(60, 60, 300);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Shape", meta=(ClampMin="1", ClampMax="128", EditCondition="ModuleKind == EDesertModuleKind::Stairs || ModuleKind == EDesertModuleKind::WallStairs || ModuleKind == EDesertModuleKind::SwitchbackStairs || ModuleKind == EDesertModuleKind::LShapeStairs || ModuleKind == EDesertModuleKind::UShapeStairs", ToolTip="旧直梯和贴墙梯默认15级；折返梯强制偶数，默认16级，两段各半。平台长度不计入踏步进深。"))
    int32 StepsCount = 15;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Shape", meta=(EditCondition="ModuleKind == EDesertModuleKind::PotCluster", ToolTip="瓦罐组是否带一个浅底托；关闭时三只罐子直接落在该块底面。"))
    bool bClusterBase = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Shape", meta=(EditCondition="ModuleKind == EDesertModuleKind::AwningBay", ToolTip="篷布支架组里增加一小组瓦罐。是组合块内部变体，不额外创建作者Actor。"))
    bool bAwningProps = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Appearance")
    TObjectPtr<UDesertBuildingStyle> Style = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Appearance", meta=(ToolTip="只替换这个附件，优先级高于Style中的同类模块；模型保留自己的材质和碰撞。空值使用Style，再空值使用白模。"))
    TObjectPtr<UStaticMesh> MeshOverride = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Appearance", meta=(Units="cm", ClampMin="1", ClampMax="5000", ToolTip="正式整模块的原始建模尺寸，用Dimensions/ReferenceDimensions计算缩放。默认与该种类推荐尺寸一致，模型必须遵守该种类枢轴。"))
    FVector ReferenceDimensions = FVector(60, 60, 300);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Collision", meta=(ToolTip="启用时所有可见白模部件阻挡所有通道，可查询且有物理碰撞；不会开启刚体模拟。支柱、楼梯默认开启，小物和顶棚默认关闭。"))
    bool bCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Collision", meta=(ToolTip="允许此附件影响导航网格，需要场景自己配置NavMesh。附件不会自动创建导航或验证角色可达。"))
    bool bAffectNavigation = false;

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Module")
    void Rebuild();

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Module", meta=(ToolTip="按当前种类重置尺寸、参考尺寸、楼梯级数和碰撞默认值；不会更改位置、Style或MeshOverride。"))
    void ApplyKindDefaults();

    UFUNCTION(BlueprintCallable, Category="Module|Occlusion")
    void SetOcclusionFade(float VisibleAmount);

    UFUNCTION(BlueprintPure, Category="Module|Occlusion")
    float GetOcclusionFade() const { return CurrentOcclusionFade; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Module|Occlusion", meta=(ToolTip="仅预留与建筑一起淡化的材质接口；材质需实现该参数。没有自动遮挡检测。"))
    FName OcclusionFadeParameter = TEXT("OcclusionFade");

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Module|Statistics")
    int32 InstanceCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Module|Statistics", meta=(ToolTip="开启碰撞的组件里的实例数。正式网格还需要自身带有碰撞体；此值不是角色行走测试。"))
    int32 CollisionInstanceCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Module|Statistics")
    bool bUsingCustomMesh = false;

private:
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<USceneComponent> ModuleRoot;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> WallBoxes;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> TrimBoxes;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> WoodBoxes;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> ClothBoxes;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> DarkBoxes;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> WallCylinders;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> PotCylinders;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> WoodCylinders;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> DarkCylinders;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> WallSpheres;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> PotSpheres;
    UPROPERTY(VisibleAnywhere, Category="Module|Components") TObjectPtr<UInstancedStaticMeshComponent> CustomModule;
    UPROPERTY() TObjectPtr<UMaterialInterface> DefaultBaseMaterial;
    UPROPERTY(Transient) TArray<FDesertModuleFadeSlot> FadeSlots;
    UPROPERTY(Transient) float CurrentOcclusionFade = 1.0f;
    UPROPERTY(Transient) FName CachedOcclusionParameter;

    TArray<UInstancedStaticMeshComponent*> GetModuleComponents() const;
    UStaticMesh* ResolveMesh() const;
    FVector GetKindDimensions() const;
    void ConfigureComponents();
    void RestoreFadeMaterials();
    void RefreshFadeMaterials();
};
