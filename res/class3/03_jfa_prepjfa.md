# 03 · JFA · prepjfa.frag — 种子编码

> 📁 源码：`res/shaders/prepjfa.frag`（20 行）  
> 📞 调用方：`demo.cpp::render()` 第 174-180 行  
> 🎯 **Goal**: 5 分钟讲清楚怎么把"白墙黑空"二值图变成 JFA 所需的"种子纹理"。

---

## 1. 完整源码（带行号）

```glsl
 1  #version 330 core
 2  
 3  out vec4 fragColor;
 4  
 5  uniform sampler2D uSceneMap;
 6  
 7  /*
 8   * this shader prepares the canvas texture to be processed by the jump-flood algorithm in jfa.frag
 9   * all it does is replace white pixels with empty vec4s, and encodes texture coordinates into black pixels.
10   */
11  
12  void main() {
13    vec2 fragCoord = gl_FragCoord.xy/textureSize(uSceneMap, 0); // for some reason fragTexCoord is just upside down sometimes? Raylib issue
14    vec4 mask = texture(uSceneMap, fragCoord);
15  
16    if (mask.a == 1.0)
17      mask = vec4(fragCoord, 0.0, 1.0);
18  
19    fragColor = mask;
20  }
```

> ⚠️ 注意第 13 行注释里的 "Raylib issue"：Raylib 的 FBO 在某些平台下纹理坐标 Y 轴是反的，**不能用 `gl_FragCoord` 直接当 UV**，必须用 `textureSize()` 重新归一化。这是一个跨平台时容易踩的坑。

---

## 2. 输入与输出

### 输入：`uSceneMap`

来自 `prepscene.frag`。**约定**：
- `alpha = 1.0` → 该像素是**障碍物（墙）**
- `alpha = 0.0` → 该像素是**空地**

RGB 通道此时已经没用了（光照是后面的事），但**白色/黑色**这种"灰度差异"还残留着，本 shader 直接忽略它们，只看 `a`。

### 输出：`fragColor`

| 像素类型 | 输出 vec4 | 含义 |
|----------|-----------|------|
| 墙像素 (`a == 1`) | `vec4(fragCoord.x, fragCoord.y, 0.0, 1.0)` | **种子**：R=本像素 u，G=本像素 v，B 暂时 0，A=1（"我有效"）|
| 空地像素 (`a == 0`) | `vec4(0, 0, 0, 0)` | 空白：四通道全 0，A=0（"我无效，JFA 别理我"） |

---

## 3. 逐行拆解

### 第 13 行：归一化 fragCoord

```glsl
vec2 fragCoord = gl_FragCoord.xy / textureSize(uSceneMap, 0);
```

- `gl_FragCoord.xy` = 当前像素的**窗口坐标**，单位是**像素**（如 `(512, 384)`）
- `textureSize(uSceneMap, 0)` = uSceneMap 的**分辨率** vec2（mip 0）
- 相除后 = **UV 坐标** vec2 ∈ [0, 1]²

> ❓ 为什么不直接用 GLSL 内置的 `gl_FragCoord` 经过 `varying` 传过来的值？  
> ✅ 因为 Raylib 加载 shader 时，纹理坐标轴在某些平台会上下颠倒（OpenGL 与 Metal 的差异），作者用 `textureSize` 显式归一化来规避。

### 第 14 行：采样

```glsl
vec4 mask = texture(uSceneMap, fragCoord);
```

把 `prepscene` 写的"白墙黑空"读进来。

### 第 16-17 行：把"墙"变成"种子"

```glsl
if (mask.a == 1.0)
  mask = vec4(fragCoord, 0.0, 1.0);
```

**关键**：**丢弃了原始 RGB**，**用当前像素的 UV 替换**。  
所以"种子"的真正含义是"**这个像素就是障碍物，JFA 应该朝我汇聚**"，而 UV 就是种子的"**身份证**"——其他像素只要把自己的 UV 和种子的 UV 比一下，就知道"我离这堵墙有多远"。

### 第 19 行：写回

```glsl
fragColor = mask;
```

写进 `jfaBufferA`，作为 JFA 的**第 0 轮输入**。

---

## 4. 数据格式约定（后面 JFA 全部依赖这个）

```
种子像素:  RG = 自己的 UV   B = 0          A = 1  (本像素是墙)
空白像素:  RG = (0, 0)      B = 0          A = 0  (JFA 必须跳过)
```

JFA 内部约定：
- **采样到一个 A==0 的邻居** → 跳过（`continue`）
- **采样到一个 A==1 的邻居** → 计算它到自己 (`fragCoord`) 的距离，比当前 `closest` 小就更新

---

## 5. 为什么"种子必须是 UV 而不是颜色"？

假设我们**错误地**把墙的 RGB 保留下来：

```
错误做法：墙像素 → (1, 1, 1, 1)  ; 空地 → (0, 0, 0, 0)
JFA 算法：3×3 邻居里取 "RGB 最亮" 的那个
问题：RGB 是离散的 (256³ 颜色)，JFA 会把"颜色"和"位置"混淆。
      你画的"红墙"和"蓝墙"会互相影响距离场。
```

正确做法：

```
正确做法：墙像素 → (u, v, 0, 1)  ; 空地 → (0, 0, 0, 0)
JFA 算法：3×3 邻居里取 "UV 距离" 最近的
优势：UV 是连续的 (0,1)² 浮点，可直接做距离计算。
```

---

## 6. 与后续 shader 的衔接

```
prepjfa.frag     jfa.frag (pass 1)        jfa.frag (pass N)        distfield.frag
┌──────────┐     ┌──────────┐             ┌──────────┐             ┌──────────┐
│ 墙→种子  │ ──▶ │ 最近种子  │ ── ping ──▶ │ 最近种子  │ ────────▶ │ 距离场   │
│ (UV,1)   │     │ (UV,d,1)  │     pong     │ (UV,d,1)  │            │ (R=d)    │
│ 空→空白  │     │ 空白仍是  │             │ 全填满了  │             │          │
│ (0,0,0,0)│     │ (0,0,0,0) │             │           │             │          │
└──────────┘     └──────────┘             └──────────┘             └──────────┘
   第 0 轮          跳 1024 像素              跳 1 像素               提取 B
```

---

## 7. 关键问题

- [ ] 如果场景图是 `R5G5B5A1`（5-bit RGB）会怎么样？
- [ ] 去掉第 13 行的 `textureSize` 改用 `gl_FragCoord.xy / resolution` 会有什么隐患？
- [ ] 为什么 `mask.a == 1.0`（精确比较）而不是 `mask.a > 0.5`？

<details>
<summary>答案</summary>

1. **会失真**。5-bit RGB 总共 32³ ≈ 32K 种颜色，而 `R5G5B5A1` 的 RGB 是 8-bit 输入采样到 5-bit 输出，墙的"白色" 1.0 会被舍入成 ~31/31 = 1.0 还好；但 UV 编码精度 = 1/31 ≈ 3%，**100×100 屏幕上 UV 误差 = 3 像素**，JFA 跳距很小时根本找不到种子。
2. 取决于 Raylib 版本，**Y 轴可能反转**，墙会出现在屏幕的"镜像"位置。textureSize 显式归一化是 Raylib 跨平台保险写法。
3. 因为 `prepscene.frag` 输出时**精确**写了 `0.0` 或 `1.0`，浮点比较安全。**不需要模糊**——模糊反而会引入边缘灰度像素，被 JFA 当成"半种子"污染距离场。

</details>

---

*下一节：[04_jfa_propagation.md](./04_jfa_propagation.md) 是 JFA 的核心——`jfa.frag` 跳跃传播算法。*
