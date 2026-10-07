#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DesertBuildingModule.h"
#include "DesertBuildingModulePreviewData.h"
#include "DesertBuildingEditorLibrary.generated.h"
class ADesertBuilding;
class UDesertBuildingDesign;
class UMaterialExpression;
class UMaterialExpressionCustom;
class UStaticMesh;

/** 编辑器工具与游戏模块分开，菜单、撤销、冻结不会进入打包游戏。 */
UCLASS()
class DESERTBUILDINGLABEDITOR_API UDesertBuildingEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Desert Editor|Module Preview")
    static FDesertModulePreviewData GetModulePreviewData(ADesertBuilding* Source, int32 Tool,
        EDesertStairLayout Layout, int32 VariantIndex, int32 Facing, FIntVector ContextCell);
    UFUNCTION(BlueprintPure, Category="Desert Editor|Module Preview")
    static TArray<EDesertStairLayout> GetAuthoringStairLayouts();
    UFUNCTION(BlueprintPure, Category="Desert Editor|Migration")
    static int32 GetLegacySwitchbackCount(ADesertBuilding* Source);
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Migration")
    static int32 MigrateLegacySwitchbackStairs(ADesertBuilding* Source);
    UFUNCTION(BlueprintPure, Category="Desert Editor|Migration")
    static FString GetLastStairMigrationMessage();
    /** 真正打开现有Slate设计器；有未保存草稿时拒绝替换。 */
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Designer")
    static bool OpenBuildingDesignerForDesign(UDesertBuildingDesign* Design);
    UFUNCTION(BlueprintPure, Category="Desert Editor|Designer")
    static FString GetLastDesignerOpenMessage();
    /** 只切换工具展示，不增删作者输入；与侧栏按钮走同一入口。 */
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Designer")
    static bool SetBuildingDesignerPreviewSelection(int32 Tool, EDesertStairLayout Layout, int32 VariantIndex, int32 Facing);
    /** 与瓦罐位置下拉共用入口；已选罐组时会校验并修改，未选择时只改新放置设置。 */
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Designer")
    static bool SetBuildingDesignerPotPlacement(EDesertPotPlacement Position);
    UFUNCTION(BlueprintPure, Category="Desert Editor|Designer")
    static FDesertModulePreviewData GetBuildingDesignerPreviewData();
    UFUNCTION(BlueprintCallable, Category="Desert Editor")
    static ADesertBuilding* CreateBuilding(FVector Location);

    UFUNCTION(BlueprintCallable, Category="Desert Editor")
    static ADesertBuildingModule* AddModule(ADesertBuilding* Parent, EDesertModuleKind Kind, FTransform LocalTransform);

    // 使用Parent.SelectedCell/SelectedRoofSide，自动匹配该层高度并切开150cm露台入口。
    UFUNCTION(BlueprintCallable, Category="Desert Editor")
    static ADesertBuildingModule* AddStairsToSelectedRoof(ADesertBuilding* Parent);

    // 复制当前ISM几何到普通Actor，不删除或隐藏源，Offset为世界空间偏移。
    // 结果仍有独立组件和碰撞，但不再含参数化生成/淡化接口。不是网格合并。
    UFUNCTION(BlueprintCallable, Category="Desert Editor")
    static AActor* BakeBuilding(ADesertBuilding* Source, FVector Offset);

    /** 同时保存建筑信息、独立Style和可拖入BP。新建统一使用分类收纳；Existing更新其已有布局并同步实例。 */
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Design")
    static UDesertBuildingDesign* SaveDesignAsset(ADesertBuilding* Draft, FString ObjectPath, UDesertBuildingDesign* Existing = nullptr);

    /** ObjectPath中父目录是用户选择的根目录；新建时分类保存建筑和全部美术依赖。
     * 更新旧Layout0资产仍保留原位；新Layout1资产更新其原始资源组，不静默移动。
     */
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Design")
    static UDesertBuildingDesign* SaveManagedDesignAsset(ADesertBuilding* Draft, FString ObjectPath, UDesertBuildingDesign* Existing = nullptr);

    UFUNCTION(BlueprintCallable, Category="Desert Editor|Design")
    static int32 RefreshDesignInstances(UDesertBuildingDesign* Design);

    UFUNCTION(BlueprintPure, Category="Desert Editor|Design")
    static FString GetLastDesignSaveMessage();

    /** 编辑器自动化可靠连接Custom真实输入；仅同一材质/材质函数的表达式，失败不部分修改。 */
    UFUNCTION(BlueprintCallable, Category="Desert Editor|Materials")
    static bool ConnectCustomMaterialInputs(UMaterialExpressionCustom* Custom, const TArray<UMaterialExpression*>& Sources);

    /** 只读诊断每个真实Custom输入：名字、来源对象和输出索引；不以图上连线外观判定。 */
    UFUNCTION(BlueprintPure, Category="Desert Editor|Materials")
    static TArray<FString> GetCustomMaterialInputConnections(UMaterialExpressionCustom* Custom);

    /** 只读LOD0真实颜色缓冲。返回13个float：count，Rmin/max/mean，Gmin/max/mean，
     * Bmin/max/mean，Amin/max/mean。RGBA按uint8/255归一化，不做sRGB转换。
     * 空模型、编译未完成或无CPU颜色数据返回13个0；不重建、不改资产。
     */
    UFUNCTION(BlueprintPure, Category="Desert Editor|Meshes")
    static TArray<float> GetMeshVertexColorStatistics(UStaticMesh* Mesh);
};
