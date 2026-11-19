# GPU Driven 缺口审计与实施顺序

审计时间：2026-11-18。审计基线：`ed7cac3`。第一阶段已实现，最新运行方式见下一节；后面的 1～8 节保留实现前的缺口分析，供后续阶段对照。

目标：Device 通过 RHI 提交少量按 Pipeline/绑定状态分组的间接调用，由 GPU 决定可见对象、实例列表和绘制参数。正常帧不回读可见数来决定 CPU 绘制循环。RenderPacket 继续保留为不可变场景快照，Render 构建 RDG，Device 持有和更新 GPU Scene。

## 第一阶段：已实现的基础闭环

```text
Game: RenderPacket（全量候选对象与材质快照）
Render: GpuDrivenPass::build → 编译 RDG，不进行 CPU 视锥剔除
Device: 等待 frame slot fence → GpuScene::prepare → 绑定物理 Buffer
GPU: GpuSceneUpload → ResetDrawCount → CullAndBuildArgs → IndexedIndirect
```

- `engine/function/render/render_system/gpu_scene.*`：Device 管理公共 position/index 几何池、对象 StructuredBuffer 和 View 常量。按源缓冲去重；几何来源列表变化时创建新池版本。对象的 model、local bounds、纯色、索引范围和逻辑 object ID 来自该帧快照。
- `engine/function/render/render_pass/gpu_driven_pass.*`、`assets/shaders/gpu_driven.hlsl`：GPU 变换 AABB 八角点，进行六平面保守视锥测试，原子分配紧凑输出；生成可见实例索引、标准五字段 Args 与 GPU CountBuffer。一次 Indexed Indirect 提交不同网格和对象。
- 实例索引用实例频率的 uint 顶点流传给 VS，`first_instance` 指向紧凑列表的槽位，再索引对象表；不依赖 `SV_InstanceID` 是否包含起始实例偏移。
- 对象与上传缓冲按三个 frame slot 分开，复用前等待该 slot 的 fence。公共几何池为不可变版本；提交持有旧 Buffer，直到 GPU 完成。
- RDG 支持 Buffer 延迟导入/绑定、`indirect_read`、`vertex_read`、`index_read`，以及自定义 Buffer Copy Pass 的 `read_buffer` / `write_buffer` / `execute`。资源依赖使上传和 Args 生产者保持活跃，不靠 `never_cull`。
- RHI 增加 `RHIIndirectDrawInfo`，包含参数缓冲、64 位偏移、最大数量、可选 stride、GPU CountBuffer 和计数偏移。DX12 缓存标准 Signature；即时和延迟命令都保留所引用的 Buffer。

启用方式：`RenderSystem::enable_gpu_driven_ = true`，经 `make_render_packet()` 捕获到 `RenderPacket::gpu_driven`。也可在 Renderer Debug 中选择 **GPU Driven (solid color)**。默认仍使用原有 PBR 路径。GPU 模式默认按 object ID 生成稳定调试色，可关闭 **Object ID colors**（`gpu_driven_debug_colors_` / Packet 的 `gpu_driven_debug_colors`）查看材质 albedo。

```powershell
xmake build -a
xmake run utest "[gpu_driven]"
xmake run sun_temple game/assets/sun_temple/source --gpu-driven --frames 12 --profile --capture build/test-results/sun-temple-gpu-driven.png --profile-output build/test-results/sun-temple-gpu-driven-profile.json
```

功能测试位于 `test/render/test_gpu_driven.cpp`：真实 Render/Device 线程、多帧在途、不同几何与非零索引偏移、对象增删/重排、空列表、六平面剔除、部分越界和多工作组。回读像素与 GPU 可见数量；参数偏移、padded stride、Count=0 和数量上限也执行实际绘制，覆盖即时/延迟命令与 Indirect Dispatch。

本阶段使用静态 GPU_ONLY float3 position / uint32 index，只输出不透明纯色（object ID 调试色，或材质 albedo，缺失时白色），采用双面绘制与深度测试。剔除使用单次一维 SM5 Dispatch，候选数量上限为 65,535 × 64。几何源缓冲内容视为不可变，替换几何时提供新的源缓冲引用；尚未实现细粒度几何分配与增量更新。对象表每帧全量上传，逻辑 object ID 随快照保存，GPU 使用该帧稠密实例索引。

尚未接入贴图、PBR 光照、Alpha Mask、材质状态分组、GPU LOD/HZB、Visibility Buffer 或 shading bins；这些属于 B/C 阶段。Sun Temple 纯色模式用于验证真实场景的几何和提交链路，不能与原有 PBR 作同画质性能对照。正常渲染没有可见数回读；现有 `FrameStats::visible_batches` 仅代表 CPU 路径，GPU 模式填零。

公共池当前复制已有 Mesh 的 GPU 几何缓冲，保留原有路径的源缓冲，因此会增加一份 position/index 几何显存。后续应将静态几何的公共分配前移到资源上传流程，减少这份重复存储。

验证记录（Debug）：`xmake build -a` 成功；`xmake run utest` 通过 45 个用例、734 个断言。Sun Temple 的 1,056 个网格实例完成 12 帧 Game/Render/Device 提交，报告包含 12 张 RDG 图和 12 帧 GPU 记录，退出前 DX12 调试消息检查通过。输出为 `build/test-results/sun-temple-gpu-driven.png` 和 `sun-temple-gpu-driven-profile.json/.html`。

Release 性能复测见 [第一阶段 Profile](../doc/optimization/gpu_driven_stage_a/README.md)：当前尚未测到帧率提升，完整图采集会显著放大开销；非采集吞吐与采集热点分开报告。

## 实现前的审计基线

## 已有基础

| 能力 | 当前状态 |
|---|---|
| Game / Render / Device 队列、场景快照 | 已有，`RenderPacket::instances` 保存 DrawBatch |
| RDG Compute、Structured/Raw Buffer 的 SRV/UAV | 已有，实际 GPU 测试覆盖一般读写与连续 UAV 访问 |
| Indirect Draw / Indexed Draw / Dispatch | RHI、延迟命令列表和 DX12 都有入口 |
| 标准间接参数布局 | `RHIIndexedIndirectCommand` 已包含五个标准字段；普通 Draw 也有四字段布局 |
| 原生 INDIRECT_ARGUMENT 状态 | RHI 枚举及 DX12 映射已有，执行间接命令时会转换参数 Buffer 状态 |
| 静态几何 DEFAULT heap | 已有，暂存资源保留到提交完成 |
| 多帧提交、临时描述符与上传区 fence 保护 | 已有，可作为 GPU Driven 的同步基础 |
| 资源数组绑定 | 已有固定数组与 register space 功能测试；尚不是完整的全局索引资源系统 |
| 对象/材质 ID、全局 Buffer 雏形 | 已有旧路径，未接入当前 GBuffer/Depth Shader，需要重整 |

核心证据：

- `engine/function/render/rhi/rhi.h`：`draw_indirect`、`draw_indexed_indirect`、`dispatch_indirect`。
- `engine/platform/dx12/dx12_copy.cpp:192`：`execute_indirect()` 调用真实的 DX12 ExecuteIndirect；Graphics/Compute 调用前也刷新了绑定。
- `engine/platform/dx12/platform_rhi.cpp`：提交区间和 fence 回收。
- `test/render/test_dx12.cpp`：Structured Buffer、描述符数组、常量快照、多帧资源等功能覆盖。

## 1. 场景路径仍由 CPU 逐对象驱动

`RenderMeshManager::prepare_batches()` 在 Render 线程逐个调用 CPU 视锥测试，产生 `current_batches_`。GBuffer 与 Depth Pass 捕获这份向量，随后在 Device 回调中逐对象：

1. 更新 PerObject 常量。
2. GBuffer 更新材质参数和七个纹理绑定，并选择正面/背面/双面 Pipeline。
3. 绑定该网格的 VB/IB。
4. 调用普通 `draw_indexed()`。

证据：`render_mesh_manager.cpp:56`、`g_buffer_pass.cpp:347`、`depth_pre_pass.cpp:180`。

仅替换最后一个函数不会消除以上 CPU 循环。新路径应将全量候选对象交给 GPU，生成可见列表和 Args；Device 的场景绘制命令数量随 Pipeline/状态分组变化，而不是逐可见对象增长。CPU 路径应保留用于功能对照与暂不支持的材质。

## 2. GPU Scene 有雏形，但数据链没有接通

已有 `ObjectInfo`、`MaterialInfo`、`VertexInfo`，组件也有每个 submesh 的 object ID。然而：

- `allocate_bindless_id()` 只写入 CPU `unordered_map`，没有同步到 Shader 可索引的全局描述符表。已有 texture ID 不等于 DX12 GPU heap 槽位。
- `VertexBuffer::vertex_id_`、`IndexBuffer::index_id_` 在当前代码中只有零初始化，没有完整分配/注册路径；`set_buffer_data()` 的 ID 参数没有被填写。
- 当前 GBuffer Shader 读取 b1/b2 常量和 t0～t6；没有用 object ID 读取全局对象、网格和材质表。
- 旧 `ObjectInfo` 的 bounds 没有在组件更新路径中填写；GPU 剔除应取得当前 DrawBatch 中有效的网格 bounds。
- 旧 array buffer helper 没有设置结构 stride，不能原样当作 `StructuredBuffer<ObjectInfo>` 的已完成实现。

建议在现有 RenderResourceManager 基础上统一整理 Device 侧 GPU Scene，避免维护两套互相不一致的可写对象/材质表。至少需要：

| 数据 | 用途 |
|---|---|
| Instance/Object | 稳定逻辑 ID、变换、bounds、geometry ID、material ID、状态/分组信息 |
| Geometry/Submesh | index count、first index、vertex base 或属性偏移、可访问的几何范围 |
| Material | 完整 PBR 参数、纹理 View 索引、Sampler 索引、材质程序类别 |
| Draw/Bin Metadata | Pipeline/状态分组与对应参数区间；对象到组的映射 |

RenderPacket 第一版可以继续全量快照，增加稳定的几何/材质标识与必要版本；Device 比较/同步到持久 GPU Scene，后续再优化 dirty ranges。当前 DrawMaterial 只有参数快照与纹理引用，尚不足以表达持久材质表的身份。

## 3. 几何绑定和逐绘制对象身份尚需设计

目前每个网格各自保存 position/normal/tangent/UV/index Buffer。标准间接命令只包含绘制参数，当前 CommandSignature 不会在每条命令间改变 VB/IB 或对象常量。因此，一次执行当前类型的多条间接命令，会继承同一套已绑定的几何和描述符。

建议第一版采用公共几何池：统一的属性/索引 Buffer，加上 Geometry 表中的范围和偏移，按组绑定后批量执行标准 Indexed Indirect。这样高层和剔除 Shader 都不需要原生 GPU 地址或 DX12 VBV/IBV 格式。

还必须让 VS 知道当前对象是谁。可选择：

- GPU 输出 instance-rate 对象 ID 流，标准命令的 `first_instance` 指向该流的对应位置；RHI 已有 `use_instance_index` 输入布局支持，需要 Shader 和 Pass 接入。
- 或扩展 RHI 的间接命令布局，让每条命令写入对象 ID root constant。该方案也需要 RootSignature 关联的 CommandSignature 抽象。

两种方案不必同时实现。不能假定直接使用现有 PerObject CB 或仅把 Draw 换成 Indirect 就能区分对象。[DX12 StartInstanceLocation 语义](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawindexedinstanced)

## 4. 间接接口缺少 GPU Count，并且 Signature 每次创建

当前调用是 `ExecuteIndirect(signature, count, args, offset, nullptr, 0)`，其中 count 来自 CPU，CountBuffer 固定为空。每次调用还会创建一个新的 CommandSignature。

建议补充一个 RHI 参数描述或重载，表达：参数 Buffer、参数偏移、最大命令数、stride、可选 GPU CountBuffer 及其偏移；同时覆盖即时与延迟命令列表。DX12 缓存三种标准 CommandSignature，并检查四字节对齐、范围和容量。

注意 GPU Count 并非最小原型的绝对前提：可以固定命令槽数，由 GPU 将不可见命令的 instance_count 写成零。要实现紧凑列表并减少无效命令处理，再使用 CountBuffer。`dispatch_indirect()` 的三个 group count 本来就能由 GPU 填写。[ExecuteIndirect 的 CountBuffer 语义](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect)

普通 ExecuteIndirect 仍继承当前 PSO；不同 Pipeline 要分组提交。首版不依赖 Work Graphs、Mesh Shader 或 GPU 动态选择任意 PSO。

## 5. RDG 缺少间接消费、几何消费和 Buffer 延迟导入语义

这是接入真实 GPU 参数生产链之前必须解决的问题：

- 现有 `output_indirect_draw()` 仅在 Compute 上创建输出边，编译器将其视为写入。它不能代替 Draw/Dispatch 对 Args 的读取声明。
- 普通 `.read()` 把 Buffer 当作 Shader 资源并尝试准备 DescriptorSet，状态也设为 SHADER_RESOURCE，不适合作为纯间接参数消费。
- 回调里调用 `resolve()` 或 DX12 中实际转换为 INDIRECT_ARGUMENT，不会自动告诉 RDG 生产者/消费者关系。
- GPU 生成的 instance-rate ID 流还需要 VERTEX_BUFFER 读取依赖；类似地可能需要 INDEX_BUFFER 访问声明。
- 当前只有外部 Texture 按规格导入和延迟绑定；Buffer import 要直接给 RHIBufferRef，没有 `bind_external_buffer()`。
- RDG Copy Pass 只提供纹理入口；GPU Scene 的 Buffer 区间上传还需要 Buffer copy/transfer 访问与执行支持。
- RDGBufferBuilder 缺少结构 stride 配置，后续创建 Structured Buffer 时要补齐。

建议 API 方向（名称待实现时确定）：

| 层 | 需要表达的能力 |
|---|---|
| Render/Compute Pass Builder | `indirect_read`，Args 和可选 Count 都是读，不生成 Shader 描述符 |
| Render Pass Builder | Vertex/Index 输入读取，或统一的显式 Buffer access 声明 |
| Buffer Builder / RDGBuilder | 按 RHIBufferInfo 声明外部 Buffer，Device 执行前绑定实际对象并校验规格 |
| Buffer Builder | stride、Buffer 用途和容量 |
| Copy Pass | Buffer 区间 copy 与 TRANSFER_SRC/DST 访问 |

延迟导入还需要稳定的逻辑资源标识：同一 GPU Scene Buffer 在图中使用多次时仍应共享别名/依赖分析，不能等 Device 绑定后才发现不同逻辑输入实际指向同一分配。

UAV 写入、间接读取、Shader 读取/顶点读取之间的依赖与状态转换要出现在图中，不能依靠 Pass 创建顺序或 `never_cull()` 掩盖缺失的数据边。

## 6. GPU 剔除、分组与参数生产 Shader 尚不存在

当前没有场景剔除 Compute Shader、命令压缩、Bin counter/reset、GPU 可见实例列表、Args 生成或计数溢出处理。

首版应包含 Reset 与 Cull/BuildArgs：

1. 清空每组计数和所需的间接参数初值。
2. 读取 GPU Scene 和相机数据，做保守视锥剔除。
3. 为可见对象分配输出位置，写入对象 ID/Draw Metadata 和 Indexed Indirect 参数。
4. 写入每组计数或 group count，并保证计数不超容量。

当前反射和 DX12 UAV 创建没有完整的 Append/Consume Structured Buffer、隐式 UAV Counter 支持。首版可以使用已支持的 RWStructuredBuffer/RWByteAddressBuffer 加显式计数和 InterlockedAdd，不必先扩展 Append API。

不要继续用 Render 侧 CPU 视锥结果作为唯一输入来证明 GPU 剔除已完成。HZB 遮挡、层次化 meshlet/cluster 剔除和 GPU LOD 是后续增强项。

## 7. 完整纹理材质路径缺少全局可索引绑定

这不是完成无贴图/单材质 GPU Driven 原型的硬前提，但要批量处理不同材质的真实场景，必须有明确策略。

现有单元素纹理表缓存每个提交区间都会清空；DescriptorSet 绑定还会复制描述符记录。它不是跨帧稳定的资源索引表，也不能把已有 CPU bindless map 自动变成 GPU 资源数组。

建议扩展 RootSignature/DescriptorSet，支持 Device 管理的持久索引资源表、容量、空槽 fallback 和 fence 后槽位回收。Shader 从 Material 表取 View/Sampler 索引。资源仍通过 DescriptorSet 绑定，不恢复逐槽位资源绑定 API。

当前主要是 FXC/D3DCompile、SM5.0 和 D3DReflect。基础剔除/间接执行可继续使用它们；动态纹理描述符数组需要升级相应 Shader 路径。SM5.1 资源数组是可选起点，若选择 SM6.x 则需要 DXC 与对应 DXIL 反射。RHI 还应报告实际设备的 Shader Model、Resource Binding Tier 等能力，不能仅凭支持 DX12 假定支持所有索引方式。[微软 SM5.1 动态索引示例](https://learn.microsoft.com/en-us/windows/win32/direct3d12/dynamic-indexing-using-hlsl-5-1)

## 8. 数据更新、ID 和资源生命周期需要接入 Device fence

现有旧对象表由组件 tick 直接调用 map，帧索引来自 Game 的 `current_frame_index()`；材质表是单份持久 mapped Buffer。它们未被当前场景 Shader 消费，因此不能将其现状当成 GPU Scene 的安全更新方案。

接入前应统一为 Device 在取得可用 frame slot 后上传该帧快照；材质/对象表采用分帧版本或有同步保证的上传和更新。ID allocator 也不能在仍有 RenderPacket 或 GPU 命令引用旧数据时立即把 ID 发给新对象。缓冲扩容、纹理替换、描述符覆盖同样必须保留旧版本到对应 fence 完成。

通用提交 fence 机制已经存在；这里缺的是把 GPU Scene 的数据版本、表槽和逻辑 ID 生命周期纳入它。

## 建议实施阶段

### A. 跑通基础闭环（已实现）

补齐 RDG 访问/导入、标准间接调用与 Signature 缓存；整理 Device GPU Scene、公共几何池和对象 ID 输入。用简单不透明材质完成：

```text
SceneUpload → Reset → GPU Cull/BuildArgs → IndexedIndirect → 输出/读回
```

当前实现直接使用紧凑输出与 CountBuffer。这个阶段不要求 Visibility Buffer 或 Bindless 纹理。

### B. 接入本引擎的场景渲染

加入 GPU Material Buffer 与可索引纹理绑定；Depth、GBuffer 都消费 GPU 结果，Device 只按少量 Pipeline/状态组提交。处理背面/正面/双面与 Alpha Mask 的分组差异。先验证不透明 PBR，暂不支持的材质显式走已有路径；接入 Sun Temple 并保留 CPU 对照入口。

### C. Visibility Buffer 与材质着色分组

在上述数据寻址和间接执行可用后，再实现 Visibility Pass、可见实例/三角形 ID、属性与导数恢复、Material Resolve 和像素/quad 的 shading bins。首版 Resolve 输出现有 GBuffer，继续复用 Deferred Lighting 与 Bloom。Alpha Mask/WPO 等影响可见性的材质工作需要放在可见性阶段；透明路径另行处理。

## 功能验收

第一阶段新增了实际 Indirect GPU 输出测试；完整目标的功能验收清单如下，材质/纹理分组、描述符回收、同画质场景对照仍需后续阶段完成：

- Compute 写 Args，再经 RDG 的 Indirect Draw 绘制并回读像素。
- 固定参数、GPU Count、空列表、超过一个命令、非零参数偏移和最大数量限制。
- 不同几何、不同对象变换、多个实例，证明对象 ID、index/base vertex 正确。
- 不同材质/纹理/渲染状态，验证分组与资源索引。
- 相机移动触发 GPU 剔除，正常帧不依赖 CPU 回读可见数。
- 多帧在途的场景更新、对象删除、ID/描述符回收和 Buffer 扩容。
- 即时与延迟命令列表，启用 DX12 调试层。
- Sun Temple 同配置图像对照和 Profile：CPU 场景录制量、GPU 剔除耗时、有效命令数和整帧吞吐；统计计数异步回读。

第一阶段的具体覆盖与边界以上面的实现说明和功能测试为准。
