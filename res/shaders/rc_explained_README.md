# Radiance Cascades 辐射度级联着色器详解

## 📚 学习目标

通过阅读本文档，你将能够：
- 理解 Radiance Cascades（辐射度级联）算法的核心思想
- 掌握多尺度探针系统的工作原理
- 学会光线步进（Ray Marching）在距离场中的应用
- 理解级联合并（Cascade Merging）的技术细节
- 能够独立阅读和修改 GLSL 片段着色器代码

**前置知识要求：**
- 基本的 GLSL 语法
- 向量数学基础（点积、归一化等）
- 纹理采样概念
- [WIP_NEED_PIC] 建议补充：2D 全局光照效果对比图（有/无 RC 算法）

---

## 🎯 核心概念

### 什么是 Radiance Cascades？

Radiance Cascades（辐射度级联）是一种高效的 2D 全局光照算法。它的核心思想是：

> **用多层不同精度的"探针网格"来模拟光线传播，就像俄罗斯套娃一样，外层粗糙但覆盖远，内层精细但覆盖近。**

**关键特性：**
1. **多尺度采样**：使用多个层级（Cascade），每层级的探针密度不同
2. **距离分段**：每个层级负责不同的距离区间，避免重复计算
3. **级联合并**：当精细层级没有数据时，从粗糙层级"借用"信息
4. **实时性能**：通过智能的层级划分，在保证质量的同时实现实时渲染

[WIP_NEED_PIC] 建议补充：级联层级示意图（显示不同层级的探针网格密度对比）

### 探针（Probe）是什么？

可以把探针想象成**屏幕上的采样点网络**：

```
第 0 级（最粗糙）：    第 1 级：              第 2 级（最精细）：
┌──────────┐          ┌────┬────┐           ┌──┬──┬──┬──┐
│          │          │    │    │           ├──┼──┼──┼──┤
│  1个探针  │          │ 4个 │    │           │  │  │  │  │
│ 覆盖全屏  │          │ 探针 │    │           │16个│  │  │
│          │          │    │    │           │ 探针│  │  │
└──────────┘          └────┴────┘           └──┴──┴──┴──┘
```

**探针的关键属性：**
- **spacing**：每维度的探针数量（如 1, 2, 4, 16...）
- **size**：每个探针在屏幕上占据的大小（归一化坐标）
- **position**：当前像素在探针网格中的相对位置
- **rayCount**：该层级发射的光线总数（角度分辨率）
- **intervalStart/End**：光线追踪的距离区间

[WIP_NEED_PIC] 建议补充：探针网格在实际场景中的可视化截图

### 级联的指数规律

**越往高层级，探针越密集，覆盖范围越小，光线越多：**

| 级联索引 | 探针数量 | 探针尺寸 | 光线数量 | 距离区间 |
|---------|---------|---------|---------|---------|
| 0 | 1 | 1.0×1.0 | base | 0 ~ base/minRes |
| 1 | 4 | 0.5×0.5 | base² | base/minRes ~ base²/minRes |
| 2 | 16 | 0.25×0.25 | base³ | base²/minRes ~ base³/minRes |
| 3 | 256 | 0.0625×0.0625 | base⁴ | base³/minRes ~ base⁴/minRes |

**规律总结：**
- 探针数量 = `baseRayCount^index`（指数增长）
- 探针尺寸 = `1 / sqrt(探针数量)`（指数缩小）
- 光线数量 = `baseRayCount^(index+1)`（比探针多一级）
- 距离区间 = 随层级指数扩展（覆盖更远的区域）

---

## 💻 代码结构总览

### 文件基本信息

- **文件名**: `rc_explained.frag`
- **着色器类型**: Fragment Shader（片段着色器）
- **GLSL 版本**: 330 core（OpenGL 3.3）
- **主要功能**: 计算单个级联层的辐射度贡献

### 代码模块划分

```
rc_explained.frag
├── 1. 常量定义 (Constants)
│   ├── TWO_PI - 圆周率两倍
│   ├── EPS - 命中判断阈值
│   ├── FIRST/LAST_LEVEL - 层级判断宏
│   └── MAX_RAY_STEPS - 最大光线步进次数
│
├── 2. 输出与均匀变量 (Outputs & Uniforms)
│   ├── fragColor - 最终颜色输出
│   ├── 纹理采样器 (DistanceField, SceneMap, etc.)
│   └── 控制参数 (Resolution, CascadeIndex, etc.)
│
├── 3. 数据结构 (Structs)
│   └── probe - 探针信息结构体
│
├── 4. 辅助函数 (Helper Functions)
│   ├── get_probe_info() - 获取探针属性
│   ├── lin_to_srgb() - 线性转 sRGB
│   └── srgb_to_lin() - sRGB 转线性
│
├── 5. 核心算法 (Core Algorithm)
│   └── radiance_interval() - 光线步进函数
│
└── 6. 主函数 (Main Function)
    └── main() - 辐射度累积与输出
```

---

## 🔍 逐段代码详解

### 1️⃣ 常量与宏定义

```glsl
#define TWO_PI 6.2831853071795864769252867665590
// 定义圆周率的两倍 (2π)，用于角度计算，表示完整的圆周

#define EPS 0.0005
// 定义极小值ε，用于判断光线是否击中表面（距离小于此值视为命中）

#define FIRST_LEVEL uCascadeIndex == 0
// 宏定义：判断是否为第一层级联（索引为 0 表示最基础层级）

#define LAST_LEVEL uCascadeIndex == uCascadeAmount
// 宏定义：判断是否为最后层级联（达到级联总数上限）

#define MAX_RAY_STEPS 128
// 定义最大光线步进次数，限制光线追踪的最大迭代次数防止无限循环
```

**设计意图：**
- `TWO_PI`: 将光线角度均匀分布在 360° 范围内
- `EPS`: 浮点数精度容差，避免因精度问题导致漏检碰撞
- `FIRST/LAST_LEVEL`: 简化条件判断，提高代码可读性
- `MAX_RAY_STEPS`: 性能保护机制，防止光线陷入死循环

**[WIP_NEED_PIC]** 建议补充：EPS 阈值对碰撞检测的影响对比图

---

### 2️⃣ 均匀变量（Uniform Variables）

#### 纹理资源

```glsl
uniform sampler2D uDistanceField;
// 距离场纹理：存储每个像素到最近表面的距离
// 这是 JFA（Jump Flooding Algorithm）算法的输出结果
// [WIP_NEED_PIC] 建议补充：距离场可视化效果图（灰度图，越亮距离越远）

uniform sampler2D uSceneMap;
// 场景地图纹理：存储物体的基础颜色/材质信息
// 通常是用户绘制的场景或加载的图像

uniform sampler2D uDirectLighting;
// 直接光照纹理：存储光源直接照射产生的亮度
// 包括点光源、方向光等传统光照计算结果

uniform sampler2D uLastPass;
// 上一遍渲染结果：用于级联之间的数据传递
// 高级联可以从低级联"借用"辐射度数据
```

#### 控制参数

```glsl
uniform vec2  uResolution;
// 屏幕分辨率（宽度 x 高度），用于坐标归一化

uniform int   uBaseRayCount;
// 基础光线数量，决定每个探针发射的光线数目
// 常见值：4, 8, 16（数值越大质量越高，性能开销越大）

uniform int   uCascadeIndex;
// 当前处理的级联层级索引
// 0 = 第一级（最粗糙），数值越大级别越高（越精细）

uniform int   uCascadeAmount;
// 级联总层数，通常为 3-5 层
// 层数越多，远距离光照质量越好，但性能开销呈指数增长

uniform float uBaseInterval;
// 基础间隔距离（单位：像素），光线追踪的起始步长
// 控制光线开始采样的最小距离

uniform float uPropagationRate;
// 传播速率，控制间接光照的扩散强度
// 值越大，光的"渗透"效果越明显

uniform float uMixFactor;
// 混合因子（0~1），控制直接光照与间接光照的混合比例
// 0 = 纯间接光照，1 = 纯直接光照
```

**[WIP_NEED_PIC]** 建议补充：不同参数对最终效果的影响对比图（如 uBaseRayCount=4 vs 16）

---

### 3️⃣ 探针结构体（Probe Struct）

```glsl
struct probe {
  float spacing;       // 探针间距 = 每维度的探针数量，例如 1, 2, 4, 16
                       // 级联层级越高，探针越稀疏（spacing 越大）
  
  vec2 size;           // 探针尺寸 = 在屏幕空间中每个探针占据的大小
                       // 例如 1.0x1.0(全屏), 0.5x0.5(四分之一屏)
                       // 级联层级越高，每个探针覆盖的区域越小
  
  vec2 position;       // 探针位置 = 在当前层级探针网格内的相对坐标
                       // 范围：0 ~ spacing
  
  vec2 rayPosition;    // 光线位置 = 标识这是哪个探针发出的光线
                       // 整数网格坐标，用于区分不同的探针
  
  float intervalStart; // 追踪区间起点 = 光线开始追踪的距离阈值
                       // 离相机较近的距离边界
  
  float intervalEnd;   // 追踪区间终点 = 光线结束追踪的距离阈值
                       // 离相机较远的距离边界
  
  float rayCount;      // 光线总数 = 该层级探针向周围发射的总光线数目
                       // 角度分辨率，级联层级越高，光线越多
};
```

**核心理解：**

想象你在一个房间里放置传感器：
- **第 0 级**：房间中央放 1 个大传感器，监测整个房间
- **第 1 级**：把房间分成 4 块，每块放 1 个中等传感器
- **第 2 级**：把房间分成 16 块，每块放 1 个小传感器

**越往里层，传感器越多越密集，监测越精细！**

[WIP_NEED_PIC] 建议补充：探针结构各字段的可视化标注图

---

### 4️⃣ 探针信息获取函数 `get_probe_info()`

这是整个着色器的**核心配置函数**，负责根据级联索引计算探针的所有属性。

```glsl
probe get_probe_info(int index) {
  probe p;
  // 创建探针信息结构体实例
  
  vec2 fragCoord = gl_FragCoord.xy/uResolution;
  // 【坐标归一化】将当前片段的屏幕坐标转换为归一化坐标 (0~1 范围)
  // gl_FragCoord 是 OpenGL 内置变量，表示当前像素的屏幕坐标（以像素为单位）
  // 除以分辨率后，得到 0~1 的相对坐标，便于后续计算
```

#### 计算探针数量

```glsl
  float probeAmount = pow(uBaseRayCount, index);
  // 【指数增长】当前级联层的探针总数 = baseRayCount 的 index 次幂
  
  // 举例说明（假设 uBaseRayCount = 4）：
  // 第 0 级：4^0 = 1 个探针（全覆盖）
  // 第 1 级：4^1 = 4 个探针（2×2 网格）
  // 第 2 级：4^2 = 16 个探针（4×4 网格）
  // 第 3 级：4^3 = 64 个探针（8×8 网格）
  
  p.spacing = sqrt(probeAmount); 
  // 【网格密度】计算每维度的探针数量（即网格边长）
  // 例如：probeAmount=16 → spacing=4（表示 4×4 的网格）
```

**[WIP_NEED_PIC]** 建议补充：不同 index 下的探针网格分布示意图

#### 计算探针尺寸

```glsl
  p.size = 1.0/vec2(p.spacing);
  // 【尺寸反比】当前级联中单个探针的屏幕尺寸
  
  // 举例说明：
  // 第 0 级：spacing=1 → size=1.0×1.0（一个探针覆盖整个屏幕）
  // 第 1 级：spacing=2 → size=0.5×0.5（一个探针覆盖屏幕的四分之一）
  // 第 2 级：spacing=4 → size=0.25×0.25（一个探针覆盖屏幕的十六分之一）
  
  // 规律：级联层级越高，每个探针覆盖的区域越小，采样越密集
```

#### 计算探针内相对位置

```glsl
  p.position = mod(fragCoord, p.size) * p.spacing;
  // 【取模定位】计算当前片段在探针网格中的相对位置
  
  // 工作原理：
  // 1. mod(fragCoord, p.size) 将屏幕分割成 spacing×spacing 个小格子
  // 2. 每个格子内的坐标会在 0~p.size 范围内循环
  // 3. 乘以 p.spacing 将坐标映射到 0~spacing 范围
  
  // 举例（spacing=4, size=0.25）：
  // 如果 fragCoord = (0.3, 0.6)
  // mod((0.3, 0.6), 0.25) = (0.05, 0.1)
  // position = (0.05, 0.1) * 4 = (0.2, 0.4)
```

**[WIP_NEED_PIC]** 建议补充：mod 运算在探针网格中的可视化演示

#### 计算光线数量

```glsl
  p.rayCount = pow(uBaseRayCount, index+1); 
  // 【角度分辨率】计算该层级的光线总数
  
  // 为什么是 index+1？
  // 因为每个探针需要向多个方向发射光线
  // 第 0 级：1 个探针 × 4 条光线 = 4 条总光线
  // 第 1 级：4 个探针 × 4 条光线 = 16 条总光线
  // 第 2 级：16 个探针 × 4 条光线 = 64 条总光线
  
  // 级联层级越高，每个探针发射的光线越多，角度采样越精细
```

#### 计算探针网格坐标

```glsl
  p.rayPosition = floor(fragCoord / p.size);
  // 【网格索引】计算当前片段属于哪个探针组（整数网格坐标）
  
  // floor 向下取整：将屏幕划分成多个探针区域
  // 例如：fragCoord=(0.3, 0.6), size=0.25
  // floor((0.3, 0.6) / 0.25) = floor((1.2, 2.4)) = (1, 2)
  // 表示当前像素位于第 1 列、第 2 行的探针区域内
```

#### 计算距离区间

```glsl
  float a = uBaseInterval; // px
  // 基础间隔距离（单位：像素），光线追踪的基准步长
  
  p.intervalStart = (FIRST_LEVEL) ? 0.0 : a * pow(uBaseRayCount, index) / min(uResolution.x, uResolution.y);
  // 【区间起点】第一级从 0 开始，其他级别从前一级的终点开始
  
  p.intervalEnd = a * pow(uBaseRayCount, index+1) / min(uResolution.x, uResolution.y);
  // 【区间终点】随级联层级指数增长
  
  // 举例（假设 baseInterval=10, baseRayCount=4, minRes=512）：
  // 第 0 级：intervalStart=0, intervalEnd=10*4/512 ≈ 0.078
  // 第 1 级：intervalStart=0.078, intervalEnd=10*16/512 ≈ 0.312
  // 第 2 级：intervalStart=0.312, intervalEnd=10*64/512 ≈ 1.25
  
  // 意义：每一级负责不同的距离段，避免重复计算
  // 低级联处理近距离（精细），高级联处理远距离（粗糙）
  
  return p;
}
```

**[WIP_NEED_PIC]** 建议补充：距离区间划分的示意图（显示各级联覆盖的距离范围）

---

### 5️⃣ 色彩空间转换函数

#### 线性转 sRGB

```glsl
vec3 lin_to_srgb(vec3 rgb)
{
  // 【伽马校正】线性色彩空间转换到 sRGB 色彩空间
  
  // 原理：人眼对暗部更敏感，sRGB 通过伽马校正压缩亮部、扩展暗部
  // 公式分为两段：
  // 1. 当 rgb <= 0.0031308 时，使用线性段：rgb * 12.92
  // 2. 当 rgb > 0.0031308 时，使用幂函数段：1.055 * rgb^(1/2.4) - 0.055
  
  return mix(1.055 * pow(rgb, vec3(1.0 / 2.4)) - 0.055,
             rgb * 12.92,
             lessThanEqual(rgb, vec3(0.0031308)));
  // mix(A, B, condition) 的作用：
  // condition 为 true 时返回 B，否则返回 A
  // 这里 condition 是 "rgb <= 0.0031308"
}
```

**为什么要转换？**
- GPU 内部计算使用**线性空间**（物理正确）
- 显示器期望接收**sRGB 信号**（符合人眼感知）
- 如果不转换，画面会显得过暗或过亮

[WIP_NEED_PIC] 建议补充：线性空间 vs sRGB 空间的曲线对比图

#### sRGB 转线性

```glsl
vec3 srgb_to_lin(vec3 rgb)
{
  // 【逆伽马校正】sRGB 色彩空间转换回线性色彩空间
  // 这是 lin_to_srgb 的逆运算，用于读取纹理时转换
  
  return mix(pow((rgb + 0.055) * (1.0 / 1.055), vec3(2.4)),
             rgb * (1.0/12.92),
             lessThanEqual(rgb, vec3(0.04045)));
}
```

---

### 6️⃣ 核心算法：光线步进函数 `radiance_interval()`

这是整个 RC 算法的**心脏**，负责模拟光线在指定距离区间内的传播。

```glsl
vec4 radiance_interval(vec2 uv, vec2 dir, float a, float b) {
  // 【函数签名】
  // uv: 起始归一化坐标（光线起点）
  // dir: 光线方向向量（单位向量）
  // a: 起点距离（区间下界）
  // b: 终点距离（区间上界）
  // 返回值：累积的辐射度（RGBA）
  
  uv += a * dir;
  // 【跳跃到起点】将光线起点沿方向移动距离 a
  // 跳过不需要计算的近距离区域（由低级联负责）
  
  float travelledDist = a;
  // 【距离累加器】记录光线已经走过的总距离，初始为区间起点
```

#### 光线步进循环

```glsl
  for (int i = 0; i < MAX_RAY_STEPS; i++) {
    // 【步进循环】最多迭代 MAX_RAY_STEPS 次（128 次）
    // 这是一个安全限制，防止光线陷入死循环
    
    float dist = texture(uDistanceField, uv).r;
    // 【采样距离场】从距离场纹理中采样当前 UV 位置的最近表面距离
    // .r 表示取红色通道的值（距离值存储在 R 通道）
    // 距离场的含义：当前位置到最近障碍物有多远？
    
    uv += (dir * dist);
    // 【光线步进】沿光线方向前进距离 dist
    // 这就是"光线步进"的核心思想：
    // 不是固定步长，而是根据距离场动态调整步长
    // 距离远就大步跳，距离近就小步走
    
    // 【边界检查】如果 UV 坐标超出 [0,1] 范围（跳出屏幕），立即终止
    if (uv.xy != clamp(uv.xy, 0.0, 1.0))
      break;
    // clamp 函数将 UV 限制在 0~1 之间
    // 如果超出说明光线射出屏幕了，无需继续追踪
```

**[WIP_NEED_PIC]** 建议补充：光线步进过程的动态示意图（显示每次跳跃的距离）

#### 表面命中检测

```glsl
    // 【碰撞检测】当采样距离小于极小值 EPS 时，认为光线击中了物体表面
    if (dist < EPS) {
      
      if (uMixFactor != 0) {
        // 【混合模式】如果启用了混合模式（需要结合直接光照和间接光照）
        
        return vec4(
            mix(
              texture(uSceneMap, uv).rgb,
              // 场景本身的颜色（物体表面颜色）
              
              max(
                texture(uDirectLighting, vec2(uv.x, -uv.y)).rgb,
                // 直接光照亮度（注意 Y 坐标翻转，可能是镜像对称处理）
                
                texture(uDirectLighting, vec2(uv.x, -uv.y) - (dir * (1.0/uResolution))).rgb * uPropagationRate
                // 【光传播】相邻像素的直接光照 × 传播速率
                // 模拟光的扩散效果：光线不仅来自正前方，还会从周围渗透
              ),
              uMixFactor
              // 混合因子：控制直接光照和间接光照的权重
            ),
            1.0);
        // 返回混合后的颜色，透明度为 1.0（完全不透明）
      }
      
      return vec4(texture(uSceneMap, uv).rgb, 1.0); 
      // 【简单模式】直接返回物体表面颜色
      // 注释说虽然有些冗余但去掉会损失 2-3 FPS（性能优化妥协）
    }
```

**关键点解析：**

1. **Y 坐标翻转**：`vec2(uv.x, -uv.y)` 
   - 可能是因为纹理坐标系与屏幕坐标系不一致
   - 或者是为了实现某种对称效果

2. **光传播项**：
   ```glsl
   texture(uDirectLighting, uv - dir * pixelSize) * uPropagationRate
   ```
   - 采样**上游像素**的直接光照
   - 乘以传播速率，模拟光的散射
   - 这让光线看起来更柔和、更自然

[WIP_NEED_PIC] 建议补充：光传播效果的对比图（开启/关闭 propagation）

#### 距离区间检查

```glsl
    travelledDist += dist;
    // 【累加距离】将本次步进的距離加入总距离
    
    if (travelledDist >= b)
      break;
    // 【区间终止】如果累计走过的距离超过区间终点 b，停止光线追踪
    // 这确保了我们只计算指定区间的辐射度贡献
    // 例如：第 1 级联只负责 0.078~0.312 这段距离
  }
  
  return vec4(0.0);
  // 【未命中】如果光线没有击中任何表面（走出了区间或屏幕），返回黑色
  // 表示这条光线在这个区间内没有收集到任何辐射度
}
```

**[WIP_NEED_PIC]** 建议补充：距离区间截断的示意图（显示光线在何处停止）

---

### 7️⃣ 主函数 `main()` - 辐射度累积

这是着色器的入口点，协调整个辐射度计算流程。

```glsl
void main() {
  // 【初始化】片段着色器的入口点，对每个像素执行一次
  
  vec4 radiance = vec4(0.0);
  // 【辐射度累加器】初始化为零（黑灰色，无光照）
  
  probe p = get_probe_info(uCascadeIndex);
  // 【获取当前探针】获取当前级联层的探针信息
  
  probe up = get_probe_info(uCascadeIndex+1);
  // 【获取上级探针】获取上一级级联层的探针信息（更高一层，覆盖范围更大）
  // 用于后续的"级联合并"操作
```

#### 光线索引计算

```glsl
  float baseIndex = float(uBaseRayCount) * (p.rayPosition.x + (p.spacing * p.rayPosition.y));
  // 【唯一索引】计算基础光线索引：根据当前探针在网格中的位置，确定这是第几个探针
  
  // 工作原理：
  // 1. p.rayPosition 是当前探针的网格坐标（如 (1, 2) 表示第 1 列第 2 行）
  // 2. p.spacing 是网格宽度（如 4 表示 4×4 网格）
  // 3. rayPosition.x + spacing * rayPosition.y 将 2D 坐标展平为 1D 索引
  // 4. 乘以 uBaseRayCount 确保每个探针对应一组独立的光线索引
  
  // 举例（spacing=4, rayPosition=(1,2), baseRayCount=4）：
  // baseIndex = 4 * (1 + 4*2) = 4 * 9 = 36
  // 表示这是第 36 号探针的第一条光线
```

**[WIP_NEED_PIC]** 建议补充：探针索引映射示意图（2D 网格到 1D 索引的转换）

#### 光线循环

```glsl
  for (float i = 0.0; i < uBaseRayCount; i++) {
    // 【光线循环】对每个探针发射的所有光线进行循环
    // 例如 uBaseRayCount=4 时，每个探针向 4 个不同角度发射光线
    
    float index = baseIndex + i;
    // 【全局索引】计算当前光线的总索引（全局唯一标识）
    
    float angle = (index / p.rayCount) * TWO_PI;
    // 【角度分配】将索引映射到 0~2π 的圆周角
    
    // 举例（rayCount=16）：
    // index=0  → angle=0°      （向右）
    // index=4  → angle=90°     （向上）
    // index=8  → angle=180°    （向左）
    // index=12 → angle=270°    （向下）
    
    // 这样光线就均匀分布在 360 度全方位
```

**[WIP_NEED_PIC]** 建议补充：光线角度分布示意图（显示 4/8/16 条光线的方向）

#### 单条光线追踪

```glsl
    vec4 deltaRadiance = vec4(0.0);
    // 【光线辐射度】初始化当前光线的辐射度增量为零
    
    deltaRadiance += radiance_interval(
      p.position,
      // 起始位置：当前探针内部的相对坐标
      
      vec2(cos(angle) * min(uResolution.x, uResolution.y) / max(uResolution.x, uResolution.y), sin(angle)),
      // 【方向向量】根据角度计算单位方向
      // X 分量乘以纵横比补偿因子，确保在宽屏下角度仍然正确
      // cos(angle): X 方向分量，sin(angle): Y 方向分量
      
      p.intervalStart,
      // 追踪区间起点（离相机较近的距离）
      
      p.intervalEnd
      // 追踪区间终点（离相机较远的距离）
    );
    // 【调用步进】调用光线步进函数，计算这条光线在指定区间内收集的辐射度
```

**纵横比补偿的意义：**
```
如果不补偿：
  宽屏（16:9）下，水平方向的角度会被拉伸
  导致光线分布不均匀

补偿后：
  无论屏幕比例如何，角度都是均匀的
```

[WIP_NEED_PIC] 建议补充：有无纵横比补偿的光线分布对比图

#### 级联合并（Cascade Merging）

这是 RC 算法的**精髓**所在！

```glsl
    // 【合并逻辑】如果不是最后一级 & 当前光线没有击中任何表面 & 未禁用合并功能
    if (!(LAST_LEVEL) && deltaRadiance.a == 0.0 && uDisableMerging != 1.0) {
      // 条件解读：
      // 1. !(LAST_LEVEL): 还有更粗糙的层级可用
      // 2. deltaRadiance.a == 0.0: 当前光线没有收集到辐射度（alpha=0 表示未命中）
      // 3. uDisableMerging != 1.0: 用户没有禁用合并功能
      
      up.position = vec2(
        mod(index, up.spacing), floor(index / up.spacing)
      ) * up.size;
      // 【上级探针定位】将当前光线索引映射到上级探针网格坐标
      // mod: 计算在上级网格中的 X 坐标
      // floor/index/spacing: 计算 Y 坐标
      // 乘以 up.size 转换为归一化坐标
```

**[WIP_NEED_PIC]** 建议补充：级联合并的流程图（显示数据如何从粗糙层流向精细层）

```glsl
      #define PIXEL vec2(1.0)/uResolution
      // 【像素大小】定义一个像素的归一化大小（用于边界保护）
      
      vec2 offset = p.position / up.spacing;
      // 【偏移计算】将当前探针位置转换到上级探针的相对坐标
      
      offset = clamp(offset, PIXEL, up.size - PIXEL);
      // 【边界保护】限制偏移范围：确保不超出上级探针的边界
      // 留出至少一个像素的余量，避免采样到相邻探针
      
      vec2 uv = up.position + offset;
      // 【最终坐标】最终 UV 坐标 = 上级探针位置 + 偏移量
      
      deltaRadiance += texture(uLastPass, uv);
      // 【数据借用】从上一遍渲染结果中采样辐射度值
      // 这就是"级联合并"：当精细级别没有数据时，回退到粗糙级别
    }
```

**为什么需要合并？**

想象你在黑暗中用手电筒照墙：
- **精细层级**（近距离）：光束很窄，可能照不到远处的墙
- **粗糙层级**（远距离）：光束很宽，能覆盖更大的区域

当精细层级"看不见"时，就从粗糙层级"借"一些光过来！

**[WIP_NEED_PIC]** 建议补充：合并前后的效果对比图（显示远处阴影的改善）

#### 辐射度累积与输出

```glsl
    radiance += deltaRadiance;
    // 【累加辐射度】将当前光线的辐射度贡献累加到总辐射度中
  }
  
  radiance /= uBaseRayCount;
  // 【平均化】将所有光线的辐射度总和除以光线数量，得到平均辐射度
  // 这样无论发射多少条光线，结果都是标准化的
  
  radiance += vec4(uAmbientColor*uAmbient*0.005, 1.0);
  // 【环境光】添加环境光贡献：环境光颜色 × 开关状态 × 微弱系数 (0.005)
  // 即使没有直接光照，也有微弱的底色（模拟全局环境光）
  // 0.005 是一个很小的值，确保环境光不会喧宾夺主
```

#### 级联显示控制

```glsl
  if (uCascadeIndex < uCascadeDisplayIndex) 
    radiance = vec4(vec3(texture(uLastPass, gl_FragCoord.xy/uResolution)), 1.0);
  // 【调试模式】如果当前级联低于显示阈值，直接显示上一遍的结果
  // 这用于调试可视化：可以选择性地查看某一级联层的贡献
  // 例如：设置 displayIndex=2，只显示第 0、1、2 级的效果
```

**[WIP_NEED_PIC]** 建议补充：不同 cascadeDisplayIndex 的调试视图截图

#### 最终输出

```glsl
  fragColor = vec4((FIRST_LEVEL && uSrgb == 1) ? lin_to_srgb(radiance.rgb) : radiance.rgb, 1.0);
  // 【色彩转换】如果是第一级且启用 sRGB 转换，则将线性色彩转为 sRGB 格式
  // 这样做是为了正确显示：显示器期望 sRGB 信号，而内部计算用线性空间
  // 只有第一级需要转换，因为后续层级会继续在线性空间计算
}
```

---

## 🎨 算法流程总结

### 完整渲染管线

```
用户绘制场景
    ↓
JFA 生成距离场 (prepjfa_frag.glsl)
    ↓
计算直接光照 (传统光照模型)
    ↓
┌─────────────────────────────────┐
│  Radiance Cascades 主循环        │
│                                 │
│  for each cascade level (0..N): │
│    ├─ 计算探针网格属性           │
│    ├─ 对每个探针发射多条光线     │
│    ├─ 光线步进追踪距离场         │
│    ├─ 收集表面辐射度             │
│    ├─ 必要时从上级级联合并       │
│    └─ 输出到 uLastPass 纹理      │
└─────────────────────────────────┘
    ↓
混合直接光照与间接光照
    ↓
sRGB 转换（仅第一级）
    ↓
输出到屏幕
```

**[WIP_NEED_PIC]** 建议补充：完整管线的流程图（包含所有着色器阶段）

### 关键设计思想

1. **分而治之**：将大范围的光照计算分解为多个小范围的级联层
2. **多尺度融合**：精细层负责近处细节，粗糙层负责远处概貌
3. **自适应采样**：根据距离场动态调整光线步长，提高效率
4. **数据复用**：通过级联合并，避免重复计算

---

## 🛠️ 性能优化技巧

### 1. 调整光线数量

```glsl
// 在 C++ 代码中设置
uBaseRayCount = 4;  // 快速预览
uBaseRayCount = 8;  // 平衡质量与性能
uBaseRayCount = 16; // 高质量渲染
```

**影响：**
- 光线数量翻倍 → 计算量翻倍 → 帧率下降约 30-50%
- 但视觉提升递减：4→8 提升明显，8→16 提升有限

### 2. 控制级联层数

```glsl
uCascadeAmount = 3;  // 推荐默认值
uCascadeAmount = 4;  // 需要更好的远距离光照
uCascadeAmount = 5;  // 极致质量（性能开销大）
```

**经验法则：**
- 每增加一级，内存占用翻倍
- 大多数场景 3-4 级足够

### 3. 优化 MAX_RAY_STEPS

```glsl
#define MAX_RAY_STEPS 64   // 简单场景
#define MAX_RAY_STEPS 128  // 标准配置
#define MAX_RAY_STEPS 256  // 复杂场景
```

**权衡：**
- 步数太少 → 光线可能无法到达表面
- 步数太多 → 浪费计算资源

### 4. 启用/禁用合并

```glsl
uDisableMerging = 0;  // 启用合并（推荐，质量更好）
uDisableMerging = 1;  // 禁用合并（调试用）
```

**何时禁用？**
- 调试单一级联的贡献
- 测试合并算法的性能影响

**[WIP_NEED_PIC]** 建议补充：不同优化策略的性能对比图表

---

## ❓ 常见问题解答

### Q1: 为什么需要多个级联层？单层不行吗？

**A:** 单层面临两难选择：
- **探针太密**：计算量大，但只能覆盖近距离
- **探针太疏**：覆盖远，但近处细节丢失

多层级联通过**分工合作**解决这个矛盾：
- 第 0 级：1 个大探针，覆盖 0~10 像素
- 第 1 级：4 个中探针，覆盖 10~40 像素
- 第 2 级：16 个小探针，覆盖 40~160 像素

### Q2: EPS 值设多少合适？

**A:** 取决于你的场景尺度：
- **小场景**（像素艺术）：EPS = 0.001
- **中等场景**：EPS = 0.0005（默认）
- **大场景**：EPS = 0.0001

**太小**：可能漏检碰撞（光线穿过表面）  
**太大**：提前终止（光线停在表面外）

### Q3: 为什么 Y 坐标要翻转（-uv.y）？

**A:** 可能的原因：
1. **纹理坐标系差异**：OpenGL 纹理原点在左下角，而某些工具生成的纹理原点在左上角
2. **镜像对称效果**：故意实现的视觉效果
3. **历史遗留**：早期版本的 bug 被保留下来

**建议：** 如果你的场景看起来上下颠倒，尝试移除负号。

### Q4: 如何实现彩色光照？

**A:** 修改 `uSceneMap` 纹理：
- 当前：存储灰度距离场
- 改进：存储 RGB 颜色信息

或者在 `radiance_interval` 中：
```glsl
// 替换这一行
return vec4(texture(uSceneMap, uv).rgb, 1.0);

// 改为
vec3 surfaceColor = texture(uSceneMap, uv).rgb;
vec3 lightColor = vec3(1.0, 0.5, 0.0); // 橙色光
return vec4(surfaceColor * lightColor, 1.0);
```

**[WIP_NEED_PIC]** 建议补充：彩色光照效果示例

### Q5: 能否扩展到 3D？

**A:** 理论上可以，但复杂度剧增：
- **2D**：每个像素发射 N 条光线
- **3D**：每个体素发射 N² 条光线（球面采样）

**挑战：**
- 内存爆炸：512³ 体素需要 GB 级显存
- 性能瓶颈：光线数量呈平方增长
- 需要稀疏数据结构（如八叉树）

详见项目中的 `3d/MIGRATION_TO_3D.md` 文档。

---

## 🧪 动手实验

### 实验 1：观察不同级联层的贡献

**步骤：**
1. 在 ImGui 界面中找到 `Cascade Display Index` 滑块
2. 从 0 开始逐步增加到最大值
3. 观察每一层添加后的效果变化

**预期现象：**
- Index=0：只有最近处的光照
- Index=1：中等距离出现柔和阴影
- Index=2+：远处也开始有间接光照

**[WIP_NEED_PIC]** 建议补充：实验步骤截图序列

### 实验 2：调整光线数量

**修改代码：**
```glsl
// 在 C++ 端设置
demo.SetBaseRayCount(4);   // 低质量
demo.SetBaseRayCount(16);  // 高质量
```

**观察：**
- 光线越少，阴影边缘越"锯齿"
- 光线越多，阴影越平滑
- 但超过一定数量后，肉眼难以分辨差异

### 实验 3：禁用级联合并

**修改代码：**
```glsl
uDisableMerging = 1;
```

**对比：**
- 启用合并：远处有柔和的间接光
- 禁用合并：远处完全黑暗（除非直接被照亮）

### 实验 4：可视化距离场

**临时修改 `radiance_interval`：**
```glsl
// 在函数开头添加
if (uCascadeIndex == 0) {
    float dist = texture(uDistanceField, uv).r;
    fragColor = vec4(vec3(dist * 10.0), 1.0); // 放大距离值以便观察
    return;
}
```

**效果：**
- 亮的区域 = 远离表面
- 暗的区域 = 靠近表面
- 黑色 = 在表面内部

**[WIP_NEED_PIC]** 建议补充：距离场可视化效果图

---

## 📖 延伸阅读

### 相关资源

1. **原始论文**：
   - "Radiance Cascades: Real-time Global Illumination in 2D" by Alexander Sannikov
   
2. **项目文档**：
   - `res/class2/class7_rc_theory.md` - RC 理论详解
   - `res/class2/class8_rc_implementation.md` - RC 实现细节
   - `res/doc/rc_frag.md` - 着色器技术文档

3. **在线演示**：
   - ShaderToy 上的 RC 实现：https://www.shadertoy.com/（搜索 "radiance cascades"）

### 相关算法对比

| 算法 | 优点 | 缺点 | 适用场景 |
|------|------|------|----------|
| **Radiance Cascades** | 实时性能好，质量高 | 实现复杂 | 2D 游戏、交互式应用 |
| **Screen Space GI** | 实现简单 | 只能处理屏幕内内容 | 3D 引擎辅助 |
| **Voxel Cone Tracing** | 支持 3D | 内存占用大 | 3D 场景 |
| **Light Propagation Volumes** | 动态光源友好 | 泄漏问题 | 室内场景 |

---

## 📝 总结

### 核心要点回顾

1. **探针网格**：将屏幕划分为多层不同密度的采样点网络
2. **距离分段**：每层级联负责不同的距离区间，避免重复计算
3. **光线步进**：利用距离场动态调整步长，高效追踪光线
4. **级联合并**：当精细层无数据时，从粗糙层"借用"信息
5. **色彩管理**：内部线性空间计算，输出时转 sRGB

### 学习路径建议

**初学者：**
1. 先理解 `get_probe_info` 如何计算探针属性
2. 再研究 `radiance_interval` 的光线步进逻辑
3. 最后看 `main` 函数如何整合所有部分

**进阶者：**
1. 尝试修改光线分布策略（如重要性采样）
2. 实现自适应级联层数（根据场景复杂度动态调整）
3. 探索时间重投影（Temporal Reprojection）减少闪烁

**专家级：**
1. 移植到 3D（参考 `3d/` 目录）
2. 集成到游戏引擎（Unity/Unreal）
3. 研究与其他 GI 算法的混合方案

### 下一步行动

- ✅ 运行程序，观察不同参数的效果
- ✅ 修改 `uBaseRayCount`，体验质量与性能的权衡
- ✅ 阅读 `res/class2/` 目录下的课程文档
- ✅ 尝试在 ShaderToy 上复现简化版 RC

---

## 🙏 致谢

本文档基于以下资源编写：
- 原始 Radiance Cascades 实现：Alexander Sannikov
- 项目维护者：yhyu13
- GLSL 社区的优秀教程与示例

**祝你在图形学的世界中探索愉快！** 🎨✨

---

**文档版本**: 1.0  
**最后更新**: 2026-04-05  
**作者**: Lingma Assistant
