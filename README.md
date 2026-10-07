# Desert Building Lab · 沙漠建筑实验室

用于 **Unreal Engine 编辑阶段**的模块化建筑工具。先选择房间、楼梯、篷布、屋顶和装饰组合，在独立三维视口里点击格子搭建，再保存为可重复使用、可继续修改的建筑素材。

这套工具从风格化沙漠建筑的制作需求出发：艺术家负责模块造型与材质，插件负责邻接、承托、通路、占用、保存和实例同步。同一套生成逻辑可以接入不同的模型与材质。

当前版本：`0.4.7-assets.1`。开发与实际编译验证环境：**Unreal Engine 5.8.2 / Windows 64 位**。这是持续迭代的实验工具，当前未建立其他引擎版本兼容矩阵。

![使用公开套件组合的建筑](Docs/Images/public-kit-hero.png)

上图由公开套件在 Unreal 中实际生成并通过 SceneCapture 渲染。它展示了房间、支撑楼梯、屋顶棚亭、墙冠和篷布组合；不是设计器界面截图。制作该图的脚本见 [Tools/render_example.py](Tools/render_example.py)，不会保存或改写 Unreal 的建筑素材。

## 可以做什么

- 在预览视口里逐格添加房间，按邻接关系生成外墙、门窗、屋顶和女儿墙。
- 添加贴墙直梯、L 形双跑梯、U 形双跑梯，以及完整的“篷布＋支架”组合变体。
- 放置屋顶棚亭、穹顶、斜顶墙冠、瓦罐组和建筑外围散石组。
- 悬浮时显示候选模型和红／绿放置反馈；无效点击不会存下一件以后突然出现的隐藏模块。
- 检查空间冲突、楼梯通路、屋顶承托和地面支撑；悬空房间可自动生成支柱。
- 为每个房间设置门的朝向、是否有门、四面的窗位；为瓦罐组设置自动靠墙或九方向排布。
- 保存建筑数据、独立风格配置和可拖入场景的蓝图；修改素材时同步关联实例；另存可以创建布局变体。
- 在用户选择的目录下分类收纳模型、材质、材质函数和贴图依赖。
- 输出独立的地基与楼层组件，提供整栋／单层淡化接口，便于连接已有的遮挡检测组件。

## 从哪里开始

| 想做的事 | 文档 |
|---|---|
| 安装插件，做并保存第一栋建筑 | [操作指南](Docs/GettingStarted.md) |
| 理解格子、邻接、支撑和放置校验 | [生成逻辑与代码结构](Docs/GenerationLogic.md) |
| 接入自己的 Blender／Painter 资源 | [模型、材质与槽位接入规范](Docs/AssetIntegration.md) |
| 理解保存目录、共享资源、更新和复制变体 | [资产保存与项目管理](Docs/AssetOrganization.md) |
| 离线查看整合图文手册 | [公开插件操作手册 HTML](Docs/Manual.html)（下载仓库后用浏览器打开） |
| 确认公开代码与美术文件的使用许可 | [代码许可](LICENSE) · [美术资源许可说明](ASSET_LICENSE.md) · [制作来源与发布范围](Docs/美术来源与发布范围.md) |

## 安装概要

本仓库根目录是一份插件。把它放到项目的以下位置，确保描述文件没有被多套一层目录：

```text
YourProject/
├─ YourProject.uproject
└─ Plugins/
   └─ DesertBuildingLab/
      ├─ DesertBuildingLab.uplugin
      ├─ Source/
      │  ├─ DesertBuildingLab/
      │  └─ DesertBuildingLabEditor/
      └─ Docs/
```

使用源码版需要项目能够编译 C++ 插件。重新生成项目文件、编译项目的 Editor 目标后，在虚幻中启用 **Desert Building Lab**。打开菜单 **工具 → 沙漠建筑工具 → 打开建筑设计器（点击格子搭建）**。完整步骤和无默认美术资源时的配置见[操作指南](Docs/GettingStarted.md)。

仓库同时提供可选的 **ExampleHouse 建筑样例**与可编辑美术源：52 件模型 FBX、31 张自制／AI 辅助 PNG。运行样例使用 106 件 Unreal 资产，包括 Design、Style、BP 和 103 件美术依赖。公开版木纹采用本项目自制贴图，替换了开发环境中比较过的 Adobe 材质；它的木纹外观与私有开发版有所不同。

安装样例时，先关闭编辑器，将仓库的 `Examples/Content/DesertBuildingLabOpenSource` 整个文件夹复制到项目的 `Content/DesertBuildingLabOpenSource`，保留内部包层级和名称。打开项目后，在设计器的 Style 选择器中载入 `DA_ExampleHouse_Style`，或将 `Buildings/ExampleHouse/Blueprints/BP_ExampleHouse` 拖入关卡。样例详细路径见 [Examples/manifest.json](Examples/manifest.json) 和[操作指南](Docs/GettingStarted.md#2-安装可选建筑样例)。它是可选的项目 Content，插件源码没有强绑定这套美术。

仓库中的美术文件以 `ASSET_LICENSE.md` 为准，代码、文档和本项目公开资源按 MIT 许可声明；AI 辅助贴图已标注制作方式。原开发环境中使用过的第三方下载资产不随公开版再分发。也可以使用自己的 `DesertBuildingStyle` 和美术资源。

## 保存后是什么

例如选择 `Content/MyVillage`，命名 `House01`：

```text
MyVillage/
├─ Buildings/House01/
│  ├─ Data/DA_House01
│  ├─ Styles/DA_House01_Style
│  └─ Blueprints/BP_House01
└─ Resources/
   ├─ Meshes/
   ├─ Materials/
   ├─ Textures/
   ├─ MaterialFunctions/
   └─ Other/
```

`DA_House01` 记录制作输入；`DA_House01_Style` 记录模型与材质槽；`BP_House01` 是拖入关卡的可编辑对象。这里保存的建筑仍需要插件的运行时类型。分类收纳不是把整栋建筑合并成一件静态网格。

## 验证范围

| 验证范围 | 已完成的检查 |
|---|---|
| 原开发配置 | 实际编译；264/264 项保存流程检查，覆盖分类目录、硬／软引用、材质替换表、同目录复用、跨目录复制、冲突拒绝、旧数据更新、保存重开和关联实例同步 |
| 本次公开 ExampleHouse | 204/204 项示例生成与资源检查；103 件美术依赖、30 张实际使用贴图；六张自制木材通道替换，检查依赖、导入记录和公开资产中的私有路径；原插件 Content 哈希未改变 |

公开示例是在原开发项目的独立 Content 目录生成和检查的。这不代替其他新项目接入、最终游戏打包、角色与 NavMesh 通行、目标平台性能，或本轮新增保存流程的人工鼠标操作验收。换用其他资产时，应按接入规范重新检查拼接、洞口、碰撞和楼梯通行。

## 开发结构

- `Source/DesertBuildingLab`：建筑数据、规则、实例组件、分层和淡化接口。
- `Source/DesertBuildingLabEditor`：Slate 设计器、预览、撤销、资产收纳、蓝图保存和关卡同步。
- `Examples/Content/DesertBuildingLabOpenSource`：可选的 Unreal 建筑样例与完整风格配置。
- `SourceArt`：本项目模型 FBX 和贴图源，以及文件清单。
- `Docs`：操作、逻辑和美术接入说明。

![四种篷布支架组合](Docs/Images/public-kit-canopies.png)

四种棚架使用同一套红金布料材质，由模型轮廓、布面松垂和前沿形状区分。模型与材质可替换，放置规则保持由插件处理。

提交问题时，请提供引擎版本、使用的模块、操作步骤、截图，以及界面下方的规则提示。修改规则时，优先让悬浮预览和确认放置继续共用同一套求解结果；修改美术时，优先保护模块连接面和洞口净尺寸。
