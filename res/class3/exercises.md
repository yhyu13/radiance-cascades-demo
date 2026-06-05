# 动手练习 (Exercises)

> 🎯 **Goal**: 8 道题覆盖 JFA 和 RC 的核心概念。每题 5-15 分钟。  
> 建议按顺序做，前 4 题是 JFA，后 4 题是 RC。  
> 所有答案都可以在 `res/class3/*.md` 找到——但**先自己试**。

---

## JFA 篇

### Ex 1: 算 log 步数（基础）⭐

屏幕分辨率 2560×1440，要把墙的"距离场"算出来：

1. `jfaSteps` 至少设多少？
2. 实际会跑多少个 pass？
3. 如果 `jfaSteps = 256`，会有什么视觉问题？

<details>
<summary>答案</summary>

1. `jfaSteps ≥ max(W, H) / 2 = 2560 / 2 = 1280`
2. `log₂(2560) + 1 ≈ 13` 个 pass
3. `jfaSteps=256` 初始 jump=512，覆盖到 512 像素；屏幕宽 2560，**距离 ≥ 512 像素的墙不会被传播到**。视觉上"远处的墙消失了"或"墙远端一片黑"。

</details>

---

### Ex 2: 编码反向（基础）⭐⭐

`prepjfa.frag` 的第 16-17 行：

```glsl
if (mask.a == 1.0)
  mask = vec4(fragCoord, 0.0, 1.0);
```

如果**反着**写：

```glsl
if (mask.a == 0.0)
  mask = vec4(fragCoord, 0.0, 1.0);
```

会发生什么？为什么是错的？

<details>
<summary>答案</summary>

变成"空地编码 UV，墙是空白"。  
JFA 跑完后，**空地像素**都知道最近的"空地 UV"，**墙像素**都还是空白 (A=0)。  
整个距离场**反了**——空地变成"知道最近的空地在哪里"，墙变成"什么都不知道"。  
后续 raymarching 把墙当成"可以穿过去"，**所有墙都消失**。

</details>

---

### Ex 3: 邻居数选择（中等）⭐⭐⭐

`jfa.frag` 默认 8 邻居。如果改成 **4 邻居**（去掉对角 `(±1, ±1)`，只剩 `(0, ±1)` 和 `(±1, 0)`）：

1. 第一轮（jump=1024）能"看到"的最大距离怎么变？
2. 视觉上会出现什么伪影？
3. 8 邻居改为 25 邻居（5×5 范围，跳距单位不变）会更快还是更慢？

<details>
<summary>答案</summary>

1. **不变**——4 邻居下第一轮仍然能"看到" ±1024 像素的邻居。区别在于"同一距离"需要 2 跳才能从对角方向传过去。
2. **棋盘格伪影**（checkerboard artifact）：奇偶像素的传播路径不同，对角线方向的传播会"延迟 1 步"，产生锯齿状距离场。
3. 25 邻居每 pass 多采样 16 次（25-9），但有效传播距离 √((2·2)² + (2·2)²) ≈ 5.7 像素（vs 8 邻居的 √(1²+1²)·2 = 2.83 像素）。**pass 数能从 11 降到约 9**，但每 pass ALU ×3。
   - **通常更慢**：GPU texture sample 成本 > ALU 成本，pass 数的减少不足以抵消 ALU 增加。
   - **特殊情况更快**：如果距离场生成是瓶颈（如 CPU 端在移动端），减少 pass 数更值钱。

</details>

---

### Ex 4: 距离公式里的修正（中等）⭐⭐⭐

`jfa.frag` 第 39 行：

```glsl
float d = length((Nsample.rg - fragCoord) * vec2(resolution.x/resolution.y, 1.0));
```

为什么**乘** `vec2(W/H, 1.0)` 而不是 `vec2(H/W, 1.0)`？  
如果屏幕 1920×1080，墙在 (0, 0)，**当前像素 (1920, 0) 到墙的实际像素距离 = 1920**。  
验证：UV 差 = (1, 0)，乘 `vec2(1920/1080, 1) = (1.778, 1)`，length = 1.778。  
**诶？怎么不对？应该是 1920 啊？**

<details>
<summary>答案</summary>

啊不，**单位**——返回的 `d` 是**归一化距离**（UV 单位），不是像素！  
`length((1, 0) * (1.778, 1)) = length(1.778, 0) = 1.778` UV。  
乘 `min(W, H) = 1080` 才能转像素：`1.778 × 1080 = 1920` 像素。✓

回到公式——`(W/H, 1)` 把 X 方向 UV 差等比放大到和 Y 方向 UV 差"等像素"：

```
X 方向: 1 UV = W 像素   → 归一化到 1 UV = min(W,H) 像素 → 乘 (min(W,H)/W) = (H/W) ???
```

哦不，让我重算——

`vec2(resolution.x / resolution.y, 1.0)` = `vec2(W/H, 1)` = 把 X 单位从"1 UV = W 像素"映射到"1 UV = 1 × (W/H) 单位"。

`length((uv_diff) * (W/H, 1))` = 实际像素距离 / min(W, H) 吗？

验证：屏幕 1920×1080，UV 差 (1, 0)（水平 1 UV = 1920 像素），乘 (1920/1080, 1) = (1.778, 0)，length = 1.778。

`1.778 × min(W, H) = 1.778 × 1080 = 1920` 像素。**✓** 正确。

所以公式是 `d_normalized = pixel_dist / min(W, H)`。**这正是"归一化距离"**——`distfield.frag` 写到 `R16` 的就是它。RC 用它做 raymarching 步长时，**会再乘 `min(W, H)` 还原成像素**。

**记忆技巧**：**`vec2(W/H, 1)` 等价于"用 min(W,H) 归一化后，距离还是正比于像素"**。

</details>

---

## RC 篇

### Ex 5: 算 cascade 参数（基础）⭐

`uBaseRayCount=4, uCascadeIndex=2, uBaseInterval=0.5, uResolution=1920×1080`：

手算：
1. `probeAmount`, `spacing`, `size`
2. `intervalStart` (UV), `intervalEnd` (UV), 像素距离
3. `rayCount`
4. **这个探针覆盖屏幕的多少个像素？**

<details>
<summary>答案</summary>

1. `probeAmount = 4² = 16`  
   `spacing = √16 = 4`  
   `size = 1/4 = 0.25` (UV)
2. `intervalStart = 0.5 × 4² / 1080 = 0.00741` UV = **8 px**  
   `intervalEnd = 0.5 × 4³ / 1080 = 0.0296` UV = **32 px**
3. `rayCount = 4³ = 64`
4. 探针数/维 = 4，4×4 = **16 个探针**  
   每个探针 size = 0.25 UV = 1920×0.25 = 480 px 宽 × 1080×0.25 = 270 px 高 = **129600 像素**  
   整个屏幕 = 1920×1080 = **2073600 像素** = 16 × 129600 ✓

</details>

---

### Ex 6: 角度索引（中等）⭐⭐⭐

`rc.frag` 第 136-140 行：

```glsl
float baseIndex = float(uBaseRayCount) * (p.rayPosition.x + (p.spacing * p.rayPosition.y));
for (float i = 0.0; i < uBaseRayCount; i++) {
  float index = baseIndex + i;
  float angle = (index / p.rayCount) * TWO_PI;
}
```

设 `uBaseRayCount=4`，Cascade 2 (spacing=4)，像素 (1440, 810)：

1. `p.rayPosition` = ?
2. `baseIndex` = ?
3. 4 条光线的 `index`, `angle (rad)`, `angle (deg)` 各是多少？
4. 这 4 条光线对应 64 个角度 (`rayCount=64`) 中的哪几号？

<details>
<summary>答案</summary>

1. `fragCoord = (0.75, 0.75)`, `p.size = 0.25`  
   `p.rayPosition = floor((0.75, 0.75) / 0.25) = floor(3, 3) = (3, 3)`  
2. `baseIndex = 4 * (3 + 4*3) = 4 * 15 = 60`  
3. | i | index | angle (rad) | angle (deg) |
   |---|-------|-------------|-------------|
   | 0 | 60 | 60/64 × 2π ≈ 5.890 | 337.5° |
   | 1 | 61 | 61/64 × 2π ≈ 5.988 | 343.1° |
   | 2 | 62 | 62/64 × 2π ≈ 6.087 | 348.8° |
   | 3 | 63 | 63/64 × 2π ≈ 6.185 | 354.4° |
4. 全屏 64 个角度的**最后 4 个**。其他探针占用前 60 个。

</details>

---

### Ex 7: Merging 数学（中等）⭐⭐⭐

设 `uBaseRayCount=4, uCascadeIndex=2, uCascadeIndex+1 = 3` (Cascade 3)。

像素 (1440, 810) 的 `p.position = (0, 0)` (探针左上角)，4 条光线中第 1 条 `index = 61` 撞墙返回 `deltaRadiance.a = 0`（没找到墙）。

计算 merging 时的 `up.position`：

1. `up.spacing`, `up.size`
2. `mod(61, up.spacing)`, `floor(61 / up.spacing)`
3. 最终 `up.position` (UV)
4. `texture(uLastPass, uv)` 的 `uv` 是多少？（假设 `p.position = (0, 0)`）

<details>
<summary>答案</summary>

1. Cascade 3: `probeAmount = 4³ = 64`, `up.spacing = 8`, `up.size = 1/8 = 0.125`
2. `mod(61, 8) = 5`, `floor(61 / 8) = 7`
3. `(5, 7) * 0.125 = (0.625, 0.875)` UV  
   解释：第 7 行第 5 列的 Cascade 3 探针，左上角在 (0.625, 0.875) UV
4. `offset = p.position / up.spacing = (0, 0) / 8 = (0, 0)`  
   `clamp((0, 0), PIXEL, up.size - PIXEL) = (PIXEL, PIXEL)` (1 像素偏移防边界)  
   `uv = (0.625, 0.875) + (PIXEL, PIXEL) ≈ (0.626, 0.876)`  
   也就是 Cascade 3 输出纹理的"第 7 行第 5 列探针内 1 像素"。

</details>

---

### Ex 8: 性能调优（进阶）⭐⭐⭐⭐

你想让 RC 跑得**和 GI 一样快**，但**质量不下降**。可调参数：

| 参数 | 当前值 | 可调范围 |
|------|--------|----------|
| `jfaSteps` | 512 | 64 ~ 2048 |
| `cascadeAmount` | 5 | 1 ~ 8 |
| `rcRayCount` | 4 | 1 ~ 16 |
| `baseInterval` | 0.5 | 0.1 ~ 10 |
| `giRayCount` | 64 | 16 ~ 512 |

**任务**：把 RC 跑得比当前（~1.5 ms）**快 30%**，**不**显著降低质量。

请列出 3 个候选方案，并说明每个方案的 trade-off。

<details>
<summary>答案提示（不是唯一解）</summary>

候选 1: `cascadeAmount=4` (5→4)
- **省**：1 个 cascade 的 4 光线 × 10 步 = 40 操作/像素 + merging 简化
- **代价**：最远覆盖从 128 px 降到 64 px，**远处的墙没有光晕**
- **视觉效果**：近处光照不变，**远景稍微"硬"一点**

候选 2: `rcRayCount=2` (4→2)
- **省**：每个探针的光线数减半，shader ALU 减半
- **代价**：角度分辨率从 4/16/64/256/1024 降到 2/4/16/64/256
- **视觉效果**：Cascade 0 出现明显**对角线噪声**（2 光线有 180° 间隙）
- **不适合**：质量下降太大

候选 3: `cascadeAmount=5, rcRayCount=4, baseInterval=1.0` (0.5→1.0)
- **省**：每个 cascade 的区间翻倍，**raymarch 步数减半**（最远 8 px 一次能到）
- **代价**：**近距离的"半影"丢失**——墙紧贴像素（< 1 px）会"卡住"
- **视觉效果**：远光不变，**近处稍微"硬"一点**

**推荐**：方案 1（cascadeAmount=4）**最稳健**。  
质量下降主要在远景（>64 px 距离的墙），大部分实际场景影响小。

</details>

---

## 🎓 自我评估

完成上述 8 题后，你应该能：

- [ ] 在不查文档的情况下默写 JFA 三个 shader 的输入/输出
- [ ] 在不查文档的情况下默写 RC 的 `probe` 字段和 `get_probe_info` 公式
- [ ] 手算任意 cascade 的 spacing / size / interval / rayCount
- [ ] 解释 `lastFrameBuf` 在 RC 和 GI 模式下的不同角色
- [ ] 调参时能预测"哪个参数影响什么"

如果 80% 都达到了，**恭喜**——你可以开始改 `rc.frag` 和 `jfa.frag` 玩出新效果了。
