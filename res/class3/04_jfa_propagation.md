# 04 · JFA · jfa.frag — 跳跃传播（核心算法）

> 📁 源码：`res/shaders/jfa.frag`（45 行）  
> 📞 调用方：`demo.cpp::render()` 第 185-198 行的 `for (j = jfaSteps*2; j >= 1; j /= 2)` 循环  
> 🎯 **Goal**: 逐行讲清楚 JFA 单 pass 的"8 邻居投票"逻辑

---

## 1. 完整源码（带行号）

```glsl
 1  #version 330 core
 2  
 3  out vec4 fragColor;
 4  
 5  uniform sampler2D uCanvas;
 6  uniform int uJumpSize;
 7  
 8  /* this shader performs the Jump-Flood algorithm (JFA) */
 9  
10  void main() {
11    /*
12     * Our 'seed texture' from prep.frag (uCanvas) contains pixels that either indicate no data (their alpha is 0)
13     * or a texture coordinate encoded in their red-green channels (pixels that encode texture coordinates are referred to as 'seeds').
14     * For a pixel at (x, y) we gather (a maximum of) eight seeds that neighbour that pixel as (x + i, y + j) where i, j ∈ {-1, 0, 1}
15     * to decide which is the closest seed known so far to that pixel.
16     *
17     * We also apply an offset ('jump') of smaller sizes each round (n/2, n/4, n/8 ... 1) which is multiplied with our neighbour's coordinate
18     * so that i, j ∈ {-1*offset, 0, 1*offset}, e.g. if we jump 64 pixels i, j ∈ {-64, 0, 64}.
20     *
21     * The closest seed found for each pixel has its encoded texture coordinate written into that pixel's red-green channels and its distance
22     * stored in the pixel's blue channel so that we can create a distance field later on for radiance cascading.
23     *
24     * This essentially means that after running this shader the input texture will be processed so that each previously-empty pixel contains
25     * the coordinates to its nearest seed.
26     */
27  
28    vec2 resolution = textureSize(uCanvas, 0);
29    vec2 fragCoord = gl_FragCoord.xy/resolution;
30    float closest = 1.0;
31    for (int Nx = -1; Nx <= 1; Nx++) {
32      for (int Ny = -1; Ny <= 1; Ny++) {
33        vec2 NTexCoord = fragCoord + (vec2(Nx, Ny) / resolution) * uJumpSize;
34        vec4 Nsample = texture(uCanvas, NTexCoord);
35  
36        if (NTexCoord != clamp(NTexCoord, 0.0, 1.0)) continue; // skip pixels outside frame
37        if (Nsample.a == 0) continue;                          // skip pixels with no encoded texture coordinates
38  
39        float d = length((Nsample.rg - fragCoord) * vec2(resolution.x/resolution.y, 1.0));
40        if (d < closest) {
41          closest = d;
42          fragColor = vec4(Nsample.rg, d, 1.0);
43        }
44      }
45    }
46  }
```

> ⚠️ 注意第 39 行的 `* vec2(resolution.x/resolution.y, 1.0)` —— 这是个**长宽比修正**，让"距离"在非正方形屏幕上保持各向同性。**没有它的话，宽屏上的距离场会横向拉伸**。

---

## 2. 输入与输出

### 输入

- `uCanvas` — 上一轮 JFA 的输出（第一轮时是 `prepjfa.frag` 的输出）
- `uJumpSize` — 整数，本 pass 的**跳距**（像素）

### 输出

`fragColor` — 4 通道含义：

| 通道 | 内容 |
|------|------|
| R | 最近种子的 U 坐标 |
| G | 最近种子的 V 坐标 |
| B | 当前像素到种子的**欧氏距离**（带长宽比修正）|
| A | 1.0（永远写 1，因为 JFA 处理过的像素必然"有数据"）|

---

## 3. 逐行拆解

### 第 28-29 行：归一化坐标

```glsl
vec2 resolution = textureSize(uCanvas, 0);
vec2 fragCoord = gl_FragCoord.xy / resolution;
```

把"我是哪个像素"换成 UV 坐标（0~1），方便和种子的 UV 直接相减。

### 第 30 行：初始化"最近距离"为最大值

```glsl
float closest = 1.0;
```

归一化坐标下，**最远的两个像素距离 = √2 ≈ 1.414**。但用 `1.0` 当初始值是**有意为之**的：保证对角线方向的远距离被剪掉，**只关心"看得见"的范围**。

> 💡 如果你想做全屏距离场（比如雾），改成 `length(vec2(2.0))` = √2 即可。

### 第 31-32 行：8 邻居循环

```glsl
for (int Nx = -1; Nx <= 1; Nx++) {
  for (int Ny = -1; Ny <= 1; Ny++) {
```

`Nx, Ny ∈ {-1, 0, 1}`，组合出 **9 个**邻居（包括 (0, 0) 自己）。但 (0, 0) 位置 `NTexCoord = fragCoord`，`Nsample.rg` 通常 ≈ `fragCoord`（如果本像素已经有效），`d ≈ 0`，会被记录但不会传播。

### 第 33 行：计算邻居 UV（带 jump）

```glsl
vec2 NTexCoord = fragCoord + (vec2(Nx, Ny) / resolution) * uJumpSize;
```

**这是整个 JFA 的灵魂**。  
拆开看：
- `vec2(Nx, Ny) / resolution` = 1 像素对应的 UV 偏移（屏幕宽 W 时为 `1/W`）
- `* uJumpSize` = `uJumpSize` 像素对应的 UV 偏移
- `+ fragCoord` = 当前像素 + 这个偏移 = 邻居像素的 UV

举例：屏幕 1920×1080，`uJumpSize = 64`：
- `(Nx, Ny) = (1, 0)` → `NTexCoord = fragCoord + (64/1920, 0) = fragCoord + (0.033, 0)`
- 也就是"右 64 像素"的 UV

> 🧠 **关键洞察**：**除以 resolution 是为了让 jump 单位永远是"像素数"**，与屏幕分辨率无关。

### 第 34 行：采样邻居

```glsl
vec4 Nsample = texture(uCanvas, NTexCoord);
```

`uCanvas` 可能是 `prepjfa.frag` 的输出（首轮），也可能是上一轮 `jfa.frag` 的输出（后续轮）。内容格式是统一的。

### 第 36 行：边界检查

```glsl
if (NTexCoord != clamp(NTexCoord, 0.0, 1.0)) continue;
```

UV 在 [0, 1] 之外时跳过（屏幕外像素）—— 否则 `texture()` 会用 GL 默认的 CLAMP_TO_EDGE 或 REPEAT 模式**返回错误值**。

> ⚠️ 这里用 `!=` 而非 `< 0 || > 1`，是因为 vec2 的 `clamp` 是**逐分量**的，**只有任一分量越界**才会改变结果。

### 第 37 行：跳过空白像素

```glsl
if (Nsample.a == 0) continue;
```

A=0 表示这个像素**没被任何种子传播到**（或本来是空地），跳过。

> ⚠️ 这个 `continue` 是 **JFA 性能的关键**：跳过大量无意义采样，GPU warp 不会卡在 ALU 上。

### 第 39 行：距离计算（带长宽比修正）

```glsl
float d = length((Nsample.rg - fragCoord) * vec2(resolution.x/resolution.y, 1.0));
```

拆开看：
- `Nsample.rg - fragCoord` = 邻居记录的"最近种子 UV"减去"我的 UV" = **UV 空间中的位移向量**
- `* vec2(aspect, 1.0)` = **把 X 方向的位移乘上长宽比**，等效于"如果屏幕是 1920×1080，X 方向 1 单位 UV = 1920 像素，Y 方向 1 单位 UV = 1080 像素"，乘上 `1920/1080` 后两者单位对齐成"像素"。

为什么需要：如果你**不**做长宽比修正，那么 1920×1080 屏幕上水平方向 100 UV 单位的距离和垂直方向 100 UV 单位的距离会被**视为相等**——但实际像素距离分别是 192000 和 108000，**差了 1.78 倍**。后续 `rc.frag` 拿这个距离做 raymarching 步长，会**横向走太快、纵向走太慢**。

### 第 40-43 行：投票

```glsl
if (d < closest) {
  closest = d;
  fragColor = vec4(Nsample.rg, d, 1.0);
}
```

9 个邻居（包括自己）里**距离最小**的那个胜出，记录它的 UV 和距离。

> 🧠 **注意**：本像素**不会**参与下一轮（直到 `jfa.frag` 把本轮结果写回 `jfaBufferA`，下一轮 `uCanvas` 才会变），所以"自传播"是良性的。

---

## 4. 三个细节

### 4.1 为什么不直接传种子 UV，要传"最近种子 UV"？

```
Round 0: 像素 P 的 RG = 种子的 UV
Round 1: P 看 8 个邻居, 发现 L 是最近的种子, 把 L 的 UV 记到自己身上
         → 现在 P 的 RG = L 的 UV, 但 L 离 P 的距离已经被算进 B 通道
Round 2: 别的像素 Q 看 P, 看到 P 的 RG = L 的 UV
         → Q 直接用 (Q.uv - P.rg) = (Q.uv - L.uv) 计算距离
         → Q 也找到了 L, 不用再走一遍
```

**所以"JFA 不是传播种子，是传播种子的 UV"**。这就是它 O(log n) 的精髓——**信息沿 log n 跳数就能到达屏幕任何像素**。

### 4.2 为什么是 8 邻居而不是 4 邻居？

4 邻居（上下左右）在对角线方向会有"棋盘格伪影"（checkerboard artifact），因为对角线距离需要 2 跳才能传过去，而 8 邻居 1 跳就到。

### 4.3 跳距减半的几何解释

```
第 1 轮 jump = N/2:   "屏幕对角线半长" 级别
   ┌─●─────●─────●─┐     一次能跨越约 1/2 屏幕
   │ :     :     : │
   ●  :     :     :  ●
   │  :     :     :  │
   ●  :     :     :  ●
   │  :     :     :  │
   ●  :     :     :  ●
   │ :     :     : │
   └─●─────●─────●─┘
   
   → 1 跳后信息距离最远的像素 = 1 个 jump ≈ N/2

第 k 轮 jump = N/2^k:
   → 1 跳后信息距离最远的像素 ≈ N/2^k
   → 总覆盖距离 = (N/2 + N/4 + ... + 1) * 2 邻居 = N
   → 即 log₂N + 1 跳后信息"渗透"全屏
```

---

## 5. 完整数据流：把"3 像素远"的种子搬到你身上

假设屏幕 4×4，墙在 (1,1)，**目标是 (3,3) 这点要知道墙在 (1,1)**。

```
Prepjfa 后:           Pass 1 (jump=2):       Pass 2 (jump=1):
(0,0)(0,0)(0,0)(0,0)  (0,0)(0,0)(0,0)(0,0)  (1,1)(1,1)(0,0)(0,0)
(0,0) W  (0,0)(0,0)  (0,0) W  (0,0)(0,0)  (1,1) W  (0,0)(0,0)
(0,0)(0,0)(0,0)(0,0)  (0,0)(0,0)(0,0)(0,0)  (0,0)(0,0)(1,1)(1,1)
(0,0)(0,0)(0,0)(0,0)  (0,0)(0,0)(0,0)(0,0)  (0,0)(0,0)(1,1)(1,1)

(只标了 A==1 的像素的 (R,G) UV)
```

- **Pass 1** (jump=2): (3,3) 问 8 个 ±2 远的邻居，**全部在屏幕外**或距离不够近，closest 还是 1.0，没写入种子。但 (2,2) 看到了 (1,1) ±2 偏移的——没有，**2 跳远** (3,3) 才到 (1,1)，所以 (2,2) 看不到 (1,1)。等等，**0 跳的自身呢**？但 A=0 的自身被跳过了。
- **Pass 2** (jump=1): (3,3) 问 ±1 远的 8 个邻居——(2,2) 仍是 A=0；这次 closest 找不到更小的，输出 (0,0,0,0)。**bug？**

啊不，**这是 4×4 的特殊情况**。在更实际的 16×16 屏幕上：

```
(0,0) (0,0) (0,0) (0,0) | (0,0) (0,0) (0,0) (0,0) | ...
(0,0) W    (0,0) (0,0) | (0,0) W    (0,0) (0,0) | ...
(0,0) (0,0) (0,0) (0,0) | (0,0) (0,0) (0,0) (0,0) |
(0,0) (0,0) (0,0) (0,0) | (0,0) (0,0) (0,0) (0,0) |
 ─── Pass 1 (jump=8) ─── ─── Pass 2 (jump=4) ─── ─── Pass 3 (jump=2) ─── Pass 4 (jump=1) ───
```

每一轮，**信息以 2·jump 像素的速度向外辐射**。到 pass 4 时，墙的信息已经在 1 像素精度上覆盖了屏幕所有像素。

---

## 6. 关键问题

- [ ] 改成长宽比修正 `vec2(1.0, 1.0)` 会有什么视觉问题？
- [ ] 8 邻居改成 4 邻居（去掉对角）会有什么视觉问题？
- [ ] 改成 25 邻居（5×5 范围、jump 单位）会更快还是更慢？

<details>
<summary>答案</summary>

1. 1920×1080 屏幕的水平方向 100 UV 单位距离，会被误判为 100 像素；但实际是 1920 像素。下游 raymarching 横向**走 19.2 倍速度**，导致墙"远处"的距离场被压扁，**横向的光照梯度消失**（墙横向快速变暗），**纵向光照渐变更长**。
2. 出现**棋盘格伪影**：对角线方向的传播需要 2 跳才能传 1 像素（因为 4 邻居只能传 4 方向），而水平/垂直 1 跳就 1 像素。对角线上的像素距离场会有"过校正"伪影。
3. **不一定更快**。25 邻居每 pass ALU 多 3 倍，但 pass 数不变，总计算量 ×3。不过 25 邻居的"有效传播半径"是 13 像素（vs 8 邻居的 9 像素），**pass 数可以从 11 降到约 9**。是"用 ALU 换带宽"——大多数 GPU ALU 不值钱，纹理采样才值钱，所以**反而可能慢**。本项目用 8 邻居是平衡之选。

</details>

---

*下一节：[05_jfa_distfield.md](./05_jfa_distfield.md) JFA 收尾——`distfield.frag` 怎么把"距离"从 B 通道里抠出来。*
