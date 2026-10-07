# 资产保存与项目管理

`0.4.7-assets.1` 的新建保存以用户选择的 **Content 文件夹**为管理根目录。工具自动在该目录内整理建筑管理资产与美术依赖。本文的 `MyVillage` 是示例名称，可以换成项目自己的目录。

## 一次保存创建哪些文件

在设计器点击“另存新素材／复制变体…”，选 `Content/MyVillage`，输入 `House01`：

```text
Content/MyVillage/
├─ Buildings/
│  ├─ House01/
│  │  ├─ Data/DA_House01
│  │  ├─ Styles/DA_House01_Style
│  │  └─ Blueprints/BP_House01
│  └─ House01_Variant/
│     ├─ Data/DA_House01_Variant
│     ├─ Styles/DA_House01_Variant_Style
│     └─ Blueprints/BP_House01_Variant
└─ Resources/
   ├─ Meshes/
   ├─ Materials/
   ├─ Textures/
   ├─ MaterialFunctions/
   └─ Other/
```

Design 保存布局；Style 保存模型、材质和变体配置；Blueprint 是可拖入关卡、继续关联 Design 的对象。选中的根目录不必名为 `A`，目录分类也不需要人工输入到资产名称里。

名称输入 `House01`、`DA_House01` 或 `BP_House01` 会规范化成相同正文。重复前导 `DA_`／`BP_` 被去除，之后统一加一份前缀。只有前缀而没有正文、已有建筑组冲突、配套 Style／BP 不属于目标建筑等情况会拒绝，不覆盖别人的资产。

## 收纳整份 Style 的依赖

收纳范围是 **当前 Style 及其引用依赖**，不仅是视口里已经放出的几个部件。未使用但仍配置在 Style 里的门窗、楼梯、棚架、陶罐和屋顶变体也需要随建筑保持可编辑。

例如：

```text
Style → Static Mesh → Material Instance → Material
                                      → Material Function → Texture
```

工具沿硬／软引用检查持久资源，分类复制后重映射引用。材质替换表左右两端也需要重映射：复制后的模型槽所引用的原材质，必须仍能命中替换表的键。

`/Engine` 和 `/Script` 保留共享引用。它们是引擎资源与类型代码；不会复制到用户目录。其余受支持的真实美术依赖应在根目录内闭合。未知资产类别、缺失引用、临时对象或不合法依赖会明确失败，工具不是任意 Unreal 资产的迁移器。

美术依赖本地化后，建筑蓝图仍使用插件的运行时类，项目仍需安装插件。这里收纳 Unreal 资产，不自动复制 FBX、Blender、Painter 或 Designer 制作源。

资源目标名称包含来源标识。同名但来源目录不同的两件模型不会因名字相同被合并。工具依据来源身份、资源类型及收纳记录判断复用，不用显示名猜测。

## 更新与变体共享

| 操作 | 管理资产 | 美术依赖 |
|---|---|---|
| 保存更新 | 原 Design、Style、BP 原位更新，绑定实例按修订同步 | 复用已收纳资源，保留本地编辑 |
| 同一根目录另存新名字 | 新 Design、Style、BP，布局独立 | 同来源模型／材质共用 Resources 中一份 |
| 另存到另一根目录 | 新目录内建立新的管理组 | 按当前引用复制到新根目录，不继续引用旧目录的美术依赖 |
| 名称或外来资源冲突 | 拒绝并提示 | 不覆盖或接管冲突对象 |

复制布局并不等于复制一套完全独立的材质。若两个变体共享一份 MI，修改 MI 会影响两个变体。希望只有一个变体换色时，先复制 MI，再在该变体 Style 的材质替换表中指定新资源。

**单独改材质后先保存材质，再保存建筑。** 建筑保存处理自己的制作输入与新收纳资源，不自动把所有有改动的材质写盘。内存里显示出的参数不代表已经保存。关卡摆放和同步后的实例也需要保存关卡。

## 旧资产及移动目录

`AssetLayoutVersion=0` 表示旧保存布局，可能将 Design、Style、BP 放在同级，美术引用仍在别处。更新旧资产会保留原位置；插件升级不自动移动旧建筑。

要采用新规范，载入旧建筑后使用“另存”，选择根目录并给新名称。新 Design 的 `AssetLayoutVersion=1`，记录 `AssetRoot` 和 `CollectedAssets`。

移动已规范保存的建筑时，应在虚幻内容浏览器里移动**整个管理根目录**，处理引用并保存。仅移动 Data、任意改名其中一份 BP／Style 或用资源管理器搬 `.uasset`，容易破坏约定。更新时根据 Design 实际位于 `Buildings/<名称>/Data` 的路径确定根目录；不盲信旧路径字段。

历史 CollectionRoot 元数据不同，不一定表示资源需要再复制；来源身份、类型和当前规范目标路径吻合时可复用。实现测试覆盖历史记录复用，不能因此宣称人工内容浏览器移动目录已完整验收。

## 程序调用

Editor 库提供 `SaveManagedDesignAsset(Draft, ObjectPath, Existing)`。原 `SaveDesignAsset` 仍可调用；新建同样走规范收纳，已有旧资产才走旧原位更新。

例如输入 `/Game/MyVillage/House01` 或 `/Game/MyVillage/House01.House01`，最终返回 Design 实际位于 `/Game/MyVillage/Buildings/House01/Data/DA_House01`。程序应使用返回对象的路径，不假设输出仍在输入路径的同一级。

传 `Existing` 表示更新该已有对象，以其当前路径为准。程序可读取 `GetLastDesignSaveMessage()` 获取具体失败或保存结果；不能只看返回之前目录是否出现了文件。

## 验证记录怎么理解

本版本在原开发项目中完成 264/264 项实际保存检查，包含最小模型→材质→函数→贴图链、中文来源目录、共享材质、跨根目录、冲突、旧格式、保存重开和实例同步；完整开发 Style 的依赖收纳涉及 103 件资源。

公开版另已在独立 `/Game/DesertBuildingLabOpenSource` 目录完成 204/204 项示例生成与资源检查。ExampleHouse 包含三件管理资产和 103 件美术依赖，共 106 件 Unreal 资产；30 张实际使用贴图从本项目自制／AI 来源重新导入，包含六张自制木材替代。具体公开文件、路径与哈希见 [Examples/manifest.json](../Examples/manifest.json)。

两组结果的范围不同。这些证据证明对应测试配置中的原生 API 与资源行为，不能替代新项目安装、GUI 鼠标操作、最终游戏打包、角色／导航或性能测量。示例在原开发项目的独立目录生成，原插件 Content 哈希核对保持一致。
