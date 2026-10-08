# 沙漠建筑实验室 0.4.9-ui.1

面向Unreal **编辑阶段**的规则驱动模块化建筑工具：选择模型组合，在网格预览中搭建，再保存可持续修改的Design／Style／Blueprint。C++／Slate设计器处理邻接、承托、占用、通路与保存，美术可替换；不是玩家局内建造系统，也不是UE PCG Graph。

本次累积收录0.4.8-stairs.1功能，重点修复自动支柱设置不明显、新草稿继承关闭状态，整理设计器操作。实际编译环境为 **UE5.8.2／Win64**。代码、文档和公开资源按[MIT许可](https://github.com/djwcb2333/DesertBuildingLab/blob/main/LICENSE)与[美术许可](https://github.com/djwcb2333/DesertBuildingLab/blob/main/ASSET_LICENSE.md)使用；AI辅助范围和第三方排除项见[来源说明](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/美术来源与发布范围.md)。

## 0.4.9：设计器与自动支柱

- 右栏固定“建筑规则 → 自动生成四角支柱”；关闭时黄色提示和恢复按钮。新建默认开、载入保留作者值。
- 关闭前试算已有房间、附件和门窗；新增失效依赖就拒绝，不改草稿。实际关卡仍检查四角有效承托，不借用设计器制作平面。
- 明确放置／选择／删除。选择自动识别已有房间、附件或点缀，共存优先当前类型，点击空单元格只清除选择。0／1／2是内部编号，不是新增数字快捷键。
- 右栏区分“正在编辑”与“新放置默认”。附件可原位换变体、梯型、朝向，点缀可换配方与旋转；非法修改保留原件。回放置清所有选择。
- 撤销／重做限定当前素材连续草稿编辑，不跨载入／新建或场景操作，不清空其他UE窗口的全局历史；恢复后同步参数与小预览。
- 顶栏管理文件与场景，左栏模块分类与组合预览，中央编辑工具，右栏规则与参数，高级资源槽折叠，底部统一反馈。原右键导航和Shift＋点击／Delete保留。

## 累积0.4.8：结构与点缀

**相邻楼层楼梯：**新梯以目标屋顶Z=k为锚点，跨一层连接Z=k−1 → Z=k。高层每跑、平台、后半跑与最低入口前80 cm需本栋同高连续屋顶；检查头顶与出口，不自动补悬空平台。首层查实际地面。旧`GroundLegacy`保留，旧梯不升级后自动移楼。

**独立屋顶点缀：**木板平台、木板与陶罐、角落罐组、双角罐组、沿边三组、小木台与角罐六种配方，复用现有资源。单独保存、不占建筑格、同格替换。已有配置被房间、合法附件、开口、门前露台或梯路覆盖时隐藏并保留，可在恢复安全屋顶后重现；首次非法点击不存未来条目。手工组优先同格全屋自动组。

**上层门槛：**有效外门自动补与本层楼板同高、厚度向下的可碰撞门槛。有同高露台时处理女儿墙开口、墙厚桥接与点缀避让，无露台不生成悬空平台。

**连续墙／Houdini仅评估、未实施：**没有新增任意长度连续墙、焊接或Houdini集成。建议先同机位对照材质层，再决定是否开发连续段控制或局部改几何。

## 当前界面

![0.4.9实际Slate界面分区](https://raw.githubusercontent.com/djwcb2333/DesertBuildingLab/main/Docs/Images/ue-designer-overview-v0.4.9.png)

![0.4.9选择屋顶点缀并原位编辑](https://raw.githubusercontent.com/djwcb2333/DesertBuildingLab/main/Docs/Images/ue-designer-decoration-v0.4.9.png)

2026-10-09（香港时间），由`FSlateApplication::TakeScreenshot`捕获。它们证明截图时界面状态，不是物理鼠标逐项测试。使用原工程Art09美术，木纹等可能与公开ExampleHouse自制替代资源不同；本次未复制或替换资源套件。README保留的0.4.1／V3截图继续标历史，0.4.7 SceneCapture图也不充当新版UI。

## 保存与升级

1. 先保存建筑草稿、材质、关卡并关闭编辑器；更换源码后重新编译项目Editor目标。未建立其他UE版本兼容矩阵。
2. 载入保留旧支柱设置和楼梯连接，旧点缀数组为空；需要新连接时明确在新的目标表面重放。
3. 首次“保存…”或“另存变体…”选择根目录。`Buildings`存Design／Style／BP，`Resources`分类收纳整份Style依赖。
4. 保存更新同步同Design实例，另存新名字才独立；同根同来源美术共享、异根复制。材质与关卡分别保存。旧目录原位更新，另存才采用新分类。

本次未新建UE工程、迁移用户建筑或修改可选公开样例模型／材质。公开适配保留基础回退与可替换Style，避免依赖开发环境的私人资产路径。

## 已验证与未验证

| 范围 | 实际结果 |
|---|---|
| 本轮编译／同步 | UE5.8.2 Editor目标实际编译成功、源码同步核对；Content未由同步改动 |
| 本轮设计器 | **168/168**真实Slate API：支柱开关和柱链、拒绝隐藏输入、三模式、原位点缀编辑、撤销边界、保存再载入、实际关卡承托射线 |
| 本轮界面记录 | 两张原生Slate截图，未声称物理鼠标测试 |
| 0.4.8历史楼梯 | **250/250**：相邻层规则、实际模型与原生碰撞、归层、保存绑定 |
| 0.4.8历史门槛 | **491/491**：地板射线、胶囊扫描与源资源保持 |
| 0.4.8历史点缀 | **128/128**：配方、安全范围、避让、预览、保存和关联实例 |

[本次验证摘要与31份源码哈希](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/Verification_0.4.9-ui.1.json)。0.4.8三组合计869，**本轮未声明重跑**；0.4.7的264保存／204样例也是历史范围，不相加称为0.4.9全流程通过。

未验证物理鼠标逐项操作、0.4.9磁盘卸载／编辑器重启持久性、PIE CharacterMovement／实际角色尺寸、NavMesh、最终游戏打包、性能、新项目完整接入、其他引擎版本与最终美术接受。原生胶囊扫描不是角色实走，截图不是操作覆盖。

[操作指南](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/GettingStarted.md) · [生成逻辑](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/GenerationLogic.md) · [美术接入](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/AssetIntegration.md) · [资源管理](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/AssetOrganization.md) · [离线手册](https://github.com/djwcb2333/DesertBuildingLab/blob/main/Docs/Manual.html)
