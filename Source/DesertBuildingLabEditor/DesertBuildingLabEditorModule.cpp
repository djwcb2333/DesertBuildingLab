#include "Modules/ModuleManager.h"
#include "DesertBuildingEditorLibrary.h"
#include "DesertBuilding.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "ToolMenus.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "DesertBuildingDesigner.h"
#include "DesertBuildingDesign.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "DesertBuildingEditorMenu"

class FDesertBuildingLabEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("DesertBuildingDesigner"),FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
        {
            return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDesertBuildingDesigner)];
        })).SetDisplayName(LOCTEXT("DesignerTitle","沙漠建筑设计器"))
            .SetTooltipText(LOCTEXT("DesignerTooltip","选择美术模型，在独立3D预览中点击格子搭建并保存可编辑建筑素材"));
        FEditorDelegates::OnMapOpened.AddRaw(this,&FDesertBuildingLabEditorModule::OnMapOpened);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDesertBuildingLabEditorModule::RegisterMenus));
    }
    virtual void ShutdownModule() override
    {
        FEditorDelegates::OnMapOpened.RemoveAll(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("DesertBuildingDesigner"));
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }
private:
    void OnMapOpened(const FString&,bool)
    {
        if(!GEditor || GEditor->PlayWorld) return;
        UWorld* World=GEditor->GetEditorWorldContext().World();
        if(!World) return;
        int32 Updated=0;
        for(TActorIterator<ADesertBuilding> It(World);It;++It)
            if(It->DesignAsset && It->AppliedDesignRevision!=It->DesignAsset->Revision)
            {
                It->ApplyLinkedDesign(true);It->MarkPackageDirty();++Updated;
            }
        if(Updated>0) UE_LOG(LogTemp,Display,TEXT("DesertBuilding: updated %d linked buildings to the saved design revision; save this map."),Updated);
    }
    static ADesertBuilding* SelectedBuilding()
    {
        if (!GEditor || GEditor->PlayWorld) return nullptr;
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        {
            AActor* Actor = Cast<AActor>(*It);
            while (Actor)
            {
                if (ADesertBuilding* Building = Cast<ADesertBuilding>(Actor)) return Building;
                Actor = Actor->GetAttachParentActor();
            }
        }
        return nullptr;
    }
    static void Select(AActor* Actor)
    {
        if (!Actor || !GEditor) return;
        GEditor->SelectNone(false, true);
        GEditor->SelectActor(Actor, true, true);
        GEditor->NoteSelectionChange();
        GEditor->RedrawLevelEditingViewports();
    }
    static void AddTypedBlock(EDesertBlockType Type)
    {
        ADesertBuilding* Building = SelectedBuilding();
        if (!Building)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NeedBuilding","先选中一栋建筑，再设置它的 Selected Block.Cell 和 Facing。"));
            return;
        }
        FScopedTransaction Transaction(LOCTEXT("AddBlockUndo", "添加建筑组合块"));
        Building->Modify();
        Building->SelectedBlock.Type=Type;
        if (Type==EDesertBlockType::PotCluster && Building->SelectedBlock.PotPlacement==EDesertPotPlacement::LegacyCentered)
            Building->SelectedBlock.PotPlacement=EDesertPotPlacement::Automatic;
        Building->AddSelectedBlock();
        Select(Building);
    }
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Tools = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section = Tools->FindOrAddSection(TEXT("DesertBuilding"));
        Section.AddSubMenu(TEXT("DesertBuildingTools"), LOCTEXT("Name", "沙漠建筑工具"),
            LOCTEXT("Tip", "编辑阶段搭建并保存建筑，无需运行游戏"),
            FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
            {
                FToolMenuSection& Main = Menu->AddSection(TEXT("Authoring"), LOCTEXT("Authoring", "场景制作"));
                Main.AddMenuEntry(TEXT("OpenDesigner"),LOCTEXT("OpenDesigner","打开建筑设计器（点击格子搭建）"),
                    LOCTEXT("OpenDesignerTip","选模型，在3D预览中点击搭建，保存为可持续编辑的建筑素材"),FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([]{FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FName(TEXT("DesertBuildingDesigner"))));})));
                Main.AddMenuEntry(TEXT("CreateBuilding"), LOCTEXT("CreateBuilding", "新建可编辑建筑"),
                    LOCTEXT("CreateBuildingTip", "在世界原点创建建筑，之后直接移动到场景中"), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([]
                    {
                        ADesertBuilding* Building=UDesertBuildingEditorLibrary::CreateBuilding(FVector::ZeroVector);
                        if (Building) Building->LoadRuleDemo();
                        Select(Building);
                    })));
                auto Add = [&](const FName Name, const FText Label, EDesertBlockType Type)
                {
                    Main.AddMenuEntry(Name, Label, LOCTEXT("ModuleTip", "使用建筑细节中的Selected Block.Cell/Facing，添加后先检查放置规则。无效条目保留并在Validation Messages说明原因。"),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Type]{ AddTypedBlock(Type); })));
                };
                Add(TEXT("Pavilion"), LOCTEXT("Pavilion", "放置屋顶棚亭块"), EDesertBlockType::RoofPavilion);
                Add(TEXT("Dome"), LOCTEXT("Dome", "放置穹顶冠部块"), EDesertBlockType::Dome);
                Add(TEXT("RoofCrown"), LOCTEXT("RoofCrown", "放置斜顶墙冠块"), EDesertBlockType::RoofCrown);
                Add(TEXT("PotCluster"), LOCTEXT("PotCluster", "放置瓦罐组合块"), EDesertBlockType::PotCluster);
                Add(TEXT("AwningBay"), LOCTEXT("AwningBay", "放置棚布支架组合块"), EDesertBlockType::AwningBay);
                Add(TEXT("RubbleCluster"), LOCTEXT("RubbleCluster", "放置外围散石组合块"), EDesertBlockType::RubbleCluster);
                Add(TEXT("Stairs"), LOCTEXT("Stairs", "放置露台楼梯块"), EDesertBlockType::Stairs);
                Main.AddMenuEntry(TEXT("RoofStairs"), LOCTEXT("RoofStairs", "连接 Selected Cell 露台：楼梯 + 开口"),
                    LOCTEXT("RoofStairsTip", "先设置建筑的Selected Cell为现存露台格，Selected Roof Side为外露边；生成后仍需检查楼梯占地和通路"), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([]
                    {
                        ADesertBuilding* Building=SelectedBuilding();
                        if (Building)
                        {
                            Building->SelectedBlock.Cell=Building->SelectedCell+FIntVector(0,0,1);
                            Building->SelectedBlock.Facing=Building->SelectedRoofSide;
                            AddTypedBlock(EDesertBlockType::Stairs);
                        }
                        else FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidRoof", "请选择建筑，设置 Selected Cell 为现存房屋格、Selected Roof Side 为朝外的方向。添加后查看Validation Messages。"));
                    })));
                Main.AddMenuEntry(TEXT("Rebuild"), LOCTEXT("Rebuild", "重新检查规则与地面支撑"),
                    LOCTEXT("RebuildTip", "修改地形或外部地面后点击；不运行游戏"), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([]{ if (ADesertBuilding* B=SelectedBuilding()) { B->Rebuild(); Select(B); } })));
                Main.AddMenuEntry(TEXT("Bake"), LOCTEXT("Bake", "复制为静态实例快照（保留原件）"),
                    LOCTEXT("BakeTip", "在世界X方向偏移1800cm创建独立ISM快照；不合并网格、不删除原件，不复制生成或淡化接口"), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([]
                    {
                        AActor* Frozen = UDesertBuildingEditorLibrary::BakeBuilding(SelectedBuilding(), FVector(1800,0,0));
                        if (Frozen) Select(Frozen);
                        else FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeFailure", "请选中建筑，确认不在运行中，Style已设置持久材质，主体和附件已恢复完全显示并Rebuild。冻结只复制本工具的建筑与附件。"));
                    })));
            }));
    }
};
IMPLEMENT_MODULE(FDesertBuildingLabEditorModule, DesertBuildingLabEditor)
#undef LOCTEXT_NAMESPACE
