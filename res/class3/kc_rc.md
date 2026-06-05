# Key Concept · RC — 诗、口诀与速查

> 📁 关联：见 `07_rc_overview.md` ~ `11_rc_cpu.md`  
> 🎯 **Goal**: 用最短的时间让 RC "刻在脑子里"

---

## 1. English Poem

```
Five cascades stacked, from rough to fine,
Each one a probe, each probe a shrine.
A probe divides the screen in cells,
With more than one, the story tells.

The first is sharp, the last is wide,
Each angle four, each step a stride.
From zero to a base it starts,
Then doubles fast in powers of arts.

If the ray finds no wall in flight,
Borrow from the level's night.
The coarser knows what finer missed,
A merge of light, by light, is kissed.

Direct then indirect, twice the run,
Two passes cycle, then we're done.
A buffer keeps the bright, the warm,
Reflects the world in radiant form.
```

> 🎓 押韵：`fine/shrine, cells/tells, wide/stride, starts/arts, flight/night, missed/kissed, run/done, warm/form`  
> 📜 大意：5 个 cascade 从粗到细；每个探针一组；cascade 0 最细，cascade 4 最粗；区间指数增长；找不到墙就借光；直光轮 → 间接轮；最终出图。

---

## 2. 中文口诀

**完整版（七言）**：

```
五级级联由粗到精，每级探针切屏分。
基线间隔 a 起算，四倍递推到 c5。
角分辨率 4ⁱ⁺¹，间距大小 1/spacing。
近处细节远粗糙，光线数多反少匀。

撞墙返色不撞零，向上借光融本轮。
直光先算存缓存，间接再跑一帧新。
A 一表示撞到墙，零则留白待合并。
```

**精简版（五言）**：

```
五级 cascade，间距幂级数
区间 a 起算，4 倍扩出去
角分辨率 4ⁱ⁺¹，间距倒着数
撞墙返色零则借，粗级细级补
直光缓存间接跑，两轮合成图
```

**关键字段口诀**：

> **"spacing 探针数，size 屏幕份，position 探针内，rayPosition 网格位"**

> **"intervalStart = 0 是 Cascade 0；其他从 4^i 开始"**

> **"A=1 撞墙，A=0 借光"**

---

## 3. Quick Reference Table

| 问： | 答： |
|------|-----|
| RC 解决什么问题？ | 实时全局光照（GI）的"远光"和"近光"分辨率自适应 |
| 时间复杂度？ | O(cascadeAmount × baseRayCount) per pixel |
| 默认 cascadeAmount？ | 5 |
| 默认 baseRayCount？ | 4 |
| 默认 baseInterval？ | 0.5 px |
| 总有效光线数 (5 cascade)? | 4⁵ = 1024 条等效/像素 |
| 实际计算量 (5 cascade)? | 5 × 4 = **20 次 raymarch/像素** |
| 提速比 (vs 传统 GI 64 光线)? | **~64 倍**（理论），**~8 倍**（实测） |
| interval 序列 (a=0.5)? | 0, 0.5, 2, 8, 32, 128 px |
| rayCount 序列? | 4, 16, 64, 256, 1024 |
| spacing 序列? | 1, 2, 4, 8, 16 |
| size 序列? | 1, 0.5, 0.25, 0.125, 0.0625 |
| 哪级是 FIRST_LEVEL? | **Cascade 0**（最精细！）|
| 哪级是 LAST_LEVEL? | **Cascade `cascadeAmount`**（最粗糙）|
| `uMixFactor` 直光 vs 间接？ | 直光=0，间接=用户值（默认 0.7）|
| `uPropagationRate` 直光 vs 间接？ | 直光不设，间接=1.3 |
| merging 触发条件？ | `!LAST_LEVEL && deltaRadiance.a == 0 && !uDisableMerging` |
| `uDisableMerging = 1` 效果？ | 远光丢失，像素变黑 |
| `uCascadeDisplayIndex` 用途？ | 调试：显示某级结果而不合并 |
| `rcBilinear` 的作用？ | 写入时 GPU 自动 4 像素平均，模拟"低分辨率 cascade" |
| `lastFrameBuf` 在 RC 模式下？ | "**直光缓存**"，不是真的"上一帧" |

---

## 4. Code Anvils (5 个"钉子"代码片段)

### Anvil 1: 探针大小

```glsl
float probeAmount = pow(uBaseRayCount, index);   // 4^i
p.spacing = sqrt(probeAmount);                   // 探针数/维
p.size = 1.0 / vec2(p.spacing);                  // 单探针 UV 尺寸
```

### Anvil 2: 区间

```glsl
float a = uBaseInterval;
p.intervalStart = (FIRST_LEVEL) ? 0.0 : a * pow(uBaseRayCount, index) / min(uResolution.x, uResolution.y);
p.intervalEnd   = a * pow(uBaseRayCount, index+1) / min(uResolution.x, uResolution.y);
```

### Anvil 3: 光线方向（带长宽比修正）

```glsl
vec2(cos(angle) * min(uResolution.x, uResolution.y) / max(uResolution.x, uResolution.y), sin(angle))
```

### Anvil 4: Merging 位置计算

```glsl
up.position = vec2(mod(index, up.spacing), floor(index / up.spacing)) * up.size;
deltaRadiance += texture(uLastPass, up.position + (p.position / up.spacing));
```

### Anvil 5: 区间 raymarching

```glsl
uv += a * dir;  // 跳过近处
for (int i = 0; i < MAX_RAY_STEPS; i++) {
  float dist = texture(uDistanceField, uv).r;
  uv += dir * dist;
  if (uv != clamp(uv, 0.0, 1.0)) break;   // 出屏
  if (dist < EPS) return vec4(hitColor, 1.0);  // 撞墙
  if (travelledDist >= b) break;          // 区间结束
}
return vec4(0.0);  // miss
```

---

## 5. 一图总结

```
RC Pipeline:

┌────────────────────────────────────────────────────────┐
│ 直光轮 (Direct)  for i = 5, 4, 3, 2, 1, 0              │
│                                                        │
│   rc.frag:                                             │
│     - 4 条光线/探针, 区间 [4^i, 4^(i+1)] px            │
│     - merge: if 没找到墙 && 不是 LAST_LEVEL             │
│              → 从 cascade i+1 (粗糙级) 借光              │
│     - 输出 → radianceBufferA                            │
└────────────────────────────────────────────────────────┘
                          │
                          ▼
                  lastFrameBuf 拷贝
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│ 间接轮 (Indirect)  for i = 5, 4, 3, 2, 1, 0            │
│                                                        │
│   rc.frag:                                             │
│     - 4 条光线/探针, 区间 [4^i, 4^(i+1)] px            │
│     - 撞墙返 mix(scene, direct, mixFactor)             │
│     - merge 同上                                       │
│     - 输出 → radianceBufferA → 屏幕                    │
└────────────────────────────────────────────────────────┘
```

---

## 6. 一句话总结 (One-Line Summary)

> **"RC = 5 个 cascade + 4 光线/探针 + 指数区间 + 从粗到细合并 + 直光/间接两轮；'光线数 × 像素精度 = 常数' 是核心原则。"**

---

*下一节：[exercises.md](./exercises.md) 8 道动手题巩固所学。*
