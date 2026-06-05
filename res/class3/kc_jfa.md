# Key Concept · JFA — 诗、口诀与速查

> 📁 关联：见 `02_jfa_overview.md` ~ `06_jfa_cpu.md`  
> 🎯 **Goal**: 用最短的时间让 JFA "刻在脑子里"

---

## 1. English Poem

```
A seed is born where walls do stand,
It whispers UV across the land.
The flood begins with mighty jump,
Eight neighbors sampled, one big thump.

The jump grows small, the march refines,
Half each pass, in powers of twos shines.
The alpha flag, the empty sky,
Skipped silent, the empty pass go by.

At distance stored in channel blue,
Each pixel knows the nearest view.
One more pass, the field is whole,
RGB packed, the jump's the soul.
```

> 🎓 押韵：`stand/land, jump/thump, refines/shines, sky/by, blue/view, whole/soul`  
> 📜 大意：种子在墙的位置诞生，传播 UV；跳距减半的 8 邻居投票；alpha 标志过滤空白；B 通道存距离，JFA 的灵魂是"跳"。

---

## 2. 中文口诀

**完整版（七言）**：

```
墙生种子 UV 藏，RG 是址 B 是长，
A 一有效零是空，跳距对半数递减。
每轮问邻八方向，越界越空皆不忙，
JFA 跑过 log₂N，距场一朝布满堂。
```

**精简版（五言）**：

```
种子 RG 编码，A 零是空墙
跳距对半减，每轮问八方
B 通道存距离，log N 跑完场
距离场一行，distfield 提精
```

**关键字口诀**：

> **"RG 是地址，B 是距离，A 是'我在'"**

> **"prepjfa 编种子，jfa 跳投票，distfield 提距离"**

---

## 3. Quick Reference Table

| 问： | 答： |
|------|-----|
| JFA 解决什么问题？ | 每个像素到最近墙的距离场 |
| 时间复杂度？ | O(log N) 次 pass，每 pass O(W·H) |
| GPU 友好度？ | **极高**——每像素独立，无原子写 |
| 跳距序列（jfaSteps=512）？ | 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2, 1（**11 次**）|
| 跳距序列（jfaSteps=64）？ | 128, 64, 32, 16, 8, 4, 2, 1（**8 次**，1920×1080 不够）|
| 邻居数？ | 8 (3×3 - 中心自身) |
| 通道分配 (R32G32B32A32)？ | R=种子 U, G=种子 V, B=距离, A=有效 |
| 通道分配 (R16 distFieldBuf)？ | R=距离, 其他丢弃 |
| 为什么位深 R32G32B32A32？ | UV 精度 1/2²⁴，**1920 像素 / 256 精度 = 7.5 px 误差** (8-bit 不够) |
| 边界检查的写法？ | `if (uv != clamp(uv, 0, 1)) continue;` |
| 距离公式？ | `length((seed.rg - fragCoord) * vec2(W/H, 1))` |
| 8 vs 4 邻居？ | 8 邻居避免棋盘格伪影（对角线 1 跳直达） |
| 8 vs 25 邻居？ | 8 平衡 ALU 和 pass 数；25 用 ALU 换带宽，反而慢 |

---

## 4. Code Anvils (4 个"钉子"代码片段)

### Anvil 1: 种子编码

```glsl
if (mask.a == 1.0)
  mask = vec4(fragCoord, 0.0, 1.0);  // RG=UV, A=1
```

### Anvil 2: 8 邻居循环

```glsl
for (int Nx = -1; Nx <= 1; Nx++)
  for (int Ny = -1; Ny <= 1; Ny++) {
    vec2 NTexCoord = fragCoord + (vec2(Nx, Ny) / resolution) * uJumpSize;
    ...
  }
```

### Anvil 3: 距离 + 长宽比修正

```glsl
float d = length((Nsample.rg - fragCoord) * vec2(resolution.x/resolution.y, 1.0));
```

### Anvil 4: 距离场提取

```glsl
fragColor = vec4(vec3(texture(uJFA, fragCoord).b), 1.0);
```

---

## 5. 一图总结

```
JFA Pipeline:

┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  sceneBuf    │ ──▶ │   prepjfa    │ ──▶ │  jfaBufferA  │
│  墙=1,空=0   │     │  RG=UV,A=1   │     │  (seed)      │
└──────────────┘     └──────────────┘     └──────────────┘
                                                    │
                                                    │ ping-pong
                                                    ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ distFieldBuf │ ◀── │   distfield  │ ◀── │  jfaBufferA  │
│  R=distance  │     │  pick B ch   │     │  (final)     │
│  (R16)       │     │              │     │  RG=UV, B=d  │
└──────────────┘     └──────────────┘     └──────────────┘
       │
       ▼
   used by:
   ┌────────────┐  ┌────────────┐
   │  rc.frag   │  │  gi.frag   │
   └────────────┘  └────────────┘
```

---

## 6. 一句话总结 (One-Line Summary)

> **"JFA = prepjfa 编种子 + jfa 跳投票 + distfield 提距离；log₂N 步到位，UV 在 RG 距离在 B 有效在 A。"**

---

*下一节：[kc_rc.md](./kc_rc.md) 同样格式的 RC 速记。*
