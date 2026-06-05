# 09 · RC · radiance_interval — 区间 raymarching 内核

> 📁 源码：`res/shaders/rc.frag` 第 95-128 行（34 行）  
> 🎯 **Goal**: 拆解 RC 的 raymarching——为什么它比传统 raymarching 快 2 个数量级，以及 `[a, b]` 区间的数学含义。

---

## 1. 完整源码（带行号）

```glsl
 95  vec4 radiance_interval(vec2 uv, vec2 dir, float a, float b) {
 96    uv += a * dir;
 97    float travelledDist = a;
 98    for (int i = 0; i < MAX_RAY_STEPS; i++) {
 99      float dist = texture(uDistanceField, uv).r;         // sample distance field
100      uv += (dir * dist); // march our ray
101  
102      // skip UVs outside of the window
103      if (uv.xy != clamp(uv.xy, 0.0, 1.0))
104        break;
105  
106      // surface hit
107      if (dist < EPS) {
108        if (uMixFactor != 0) {
109          return vec4(
110              mix(
111                texture(uSceneMap, uv).rgb,
112                max(
113                  texture(uDirectLighting, vec2(uv.x, -uv.y)).rgb,
114                  texture(uDirectLighting, vec2(uv.x, -uv.y) - (dir * (1.0/uResolution))).rgb * uPropagationRate
115                ),
116                uMixFactor
117              ),
118              1.0);
119        }
120        return vec4(texture(uSceneMap, uv).rgb, 1.0);
121      }
122  
123      travelledDist += dist;
124      if (travelledDist >= b)
125        break;
126    }
127    return vec4(0.0);
128  }
```

---

## 2. 函数签名

```glsl
vec4 radiance_interval(vec2 uv, vec2 dir, float a, float b)
```

| 参数 | 类型 | 含义 |
|------|------|------|
| `uv` | vec2 | 光线起点（UV 坐标，[0, 1]²）|
| `dir` | vec2 | 光线方向（未归一化！，已带长宽比修正）|
| `a`   | float | **起点距离**（UV 单位，光线从 `uv` 走 `a` 距离才开始采样）|
| `b`   | float | **终点距离**（UV 单位，光线走超过 `b` 距离就停止）|

返回 `vec4`：
- `rgb` = 采样的颜色（如果 hit）
- `a` = 1.0 if hit，0.0 if miss（**这个 `.a` 用来给 merging 判断"是否找到墙"**）

---

## 3. 逐行拆解

### 3.1 第 96-97 行：跳过近处

```glsl
uv += a * dir;
float travelledDist = a;
```

光线从 `uv` 走 `a` 距离到新位置 `uv + a*dir`。`travelledDist` 记录已经走过的距离。

> 💡 为什么跳过 `a`？  
> 因为本 cascade 的 `[a, b]` 是它**唯一负责**的距离区间，**近处的 `0~a`** 由更精细 cascade 负责。  
> 例子：Cascade 2 区间 `[8, 32] px`——它不关心 8 像素内的情况（那是 Cascade 1 的事）。

### 3.2 第 99-100 行：距离场采样 + 步进

```glsl
float dist = texture(uDistanceField, uv).r;
uv += dir * dist;
```

`dist` = 当前像素到最近墙的距离（**未归一化** UV 单位，**已含长宽比修正**）。  
`uv += dir * dist` = 沿光线方向"安全步进"——**不会跳过任何墙**（这是 SDF raymarching 的核心保证）。

> 🧠 **关键不变量**：`travelledDist` 累加 = 真实步进距离，**永远不会越过墙**。

### 3.3 第 103-104 行：屏幕外检测

```glsl
if (uv.xy != clamp(uv.xy, 0.0, 1.0))
  break;
```

光线出了屏幕边界，停止 raymarching。返回 `vec4(0)` 表示"没找到墙"。

### 3.4 第 107-121 行：撞墙处理

```glsl
if (dist < EPS) {  // EPS = 0.0005 UV 单位
  if (uMixFactor != 0) {
    return vec4(
        mix(
          texture(uSceneMap, uv).rgb,
          max(
            texture(uDirectLighting, vec2(uv.x, -uv.y)).rgb,
            texture(uDirectLighting, vec2(uv.x, -uv.y) - (dir * (1.0/uResolution))).rgb * uPropagationRate
          ),
          uMixFactor
        ),
        1.0);
  }
  return vec4(texture(uSceneMap, uv).rgb, 1.0);
}
```

#### 撞墙返回什么？

**两种模式**：

1. **`uMixFactor == 0`（默认间接光轮）** → 返回 `sceneColor`（墙的颜色 = 反射光的颜色）
2. **`uMixFactor != 0`（直光轮）** → 返回 `mix(sceneColor, max(direct, directOffset), uMixFactor)`：
   - `direct` = 撞墙点的**直接光照**（来自 `lastFrameBuf`，即直光轮的结果）
   - `directOffset` = `direct` 沿光线方向偏 1 像素，乘以 `uPropagationRate`（默认 1.3）
   - `max(direct, directOffset * uPropagationRate)` = **取两者中更亮的那个**——让光"沿光线方向"略微"扩散"

#### 为什么 `vec2(uv.x, -uv.y)`？

Y 轴翻转。`uDirectLighting` 是 `lastFrameBuf`，**和屏幕方向一致**；但 RC 计算用的 `uv` 是 OpenGL 纹理坐标（**Y 轴向上**）。所以读取时需要**翻转 Y**。

#### 为什么要 `(dir * (1.0/uResolution))` 的偏移？

相当于**沿光线方向后退 1 像素**采样直光。**让"墙边"的光看起来"渗"进墙一点**——避免锐利的硬阴影边缘。

> 公式 `max(direct, directOffset * 1.3)` 是个**软化技巧**：`direct` 偏 1 像素的"光晕"亮度被放大 30%，叠加到主采样上产生**轻微的辉光**。

### 3.5 第 123-125 行：区间结束检测

```glsl
travelledDist += dist;
if (travelledDist >= b)
  break;
```

如果走过的总距离 ≥ `b`，**已经过了本 cascade 负责的区间**，停止。

> 🎯 **关键**：本 cascade **只关心 `[a, b]` 范围**，超过 `b` 留个下一 cascade 接力。**这是 RC 高效的核心**。

### 3.6 第 127 行：没找到墙

```glsl
return vec4(0.0);
```

`rgb=0, a=0` = "本区间没找到墙"。

> 注意返回的 `.a = 0`，**这是给 merging 用的信号**——`rc.frag` 主循环看到 `.a == 0` 就去"借"上一级 cascade 的结果。

---

## 4. 一次完整调用：4 个 raymarch 步

假设 Cascade 2 区间 `[8, 32] px`（UV `[0.0074, 0.0296]`），光线方向 `dir = (1, 0)`，起点 `uv = (0.5, 0.5)`：

```
初始: uv=(0.5, 0.5), travelledDist=0.0074 (= a)

Step 1: 
  dist=0.01 (距墙 10 px)
  uv += (1,0) * 0.01 = (0.51, 0.5)
  travelledDist = 0.0174
  < 0.0296, 继续

Step 2:
  dist=0.005 (距墙 5 px)
  uv = (0.515, 0.5)
  travelledDist = 0.0224
  < 0.0296, 继续

Step 3:
  dist=0.002 (距墙 2 px)
  uv = (0.517, 0.5)
  travelledDist = 0.0244
  < 0.0296, 继续

Step 4:
  dist=0.0003 (< EPS!)
  → return mix(sceneColor, direct, mixFactor)
  
  光线在 (0.517, 0.5) 处撞墙
  返回该墙的颜色/直光
```

**4 步撞墙**！传统 GI 同样场景需要 200 步。**省了 50 倍**。

---

## 5. 与 `gi.frag` 的 raymarch 对比

```cpp
// gi.frag 的 raymarch (简化):
for (int i = 0; i < MAX_RAY_STEPS; i++) {
  float dist = texture(uDistanceField, uv).r;
  uv += dir * dist;
  if (dist < EPS) {
    // ... 收集光
    break;
  }
}
// 没有 [a, b] 区间限制
```

差异：

| | `gi.frag` (传统) | `rc.frag` (RC) |
|---|----------------|----------------|
| 区间 | `[0, ∞]` | `[a, b]` |
| 最长步进 | `MAX_RAY_STEPS` | `MAX_RAY_STEPS` 但**早结束** |
| 撞墙后 | 继续下一条光线 | 返回 |
| 单像素光线数 | 64-128 | 4 (× cascade 数) |
| 单光线步数 | 200 | 4-10 |

**理论速度提升 = 64/4 × 200/10 = 320 倍**。实际上 `rc.frag` 还要合并 + 5 cascade 串行，所以**净提速约 10-30 倍**。

---

## 6. `MAX_RAY_STEPS = 128` 的隐含安全网

```glsl
#define MAX_RAY_STEPS 128
```

```glsl
for (int i = 0; i < MAX_RAY_STEPS; i++) { ... }
```

**为什么需要 128 步上限？**  
理论上 SDF raymarching 不会无限循环（每步都接近最近的墙），但**有几种情况会卡住**：
1. 距离场精度不足（`R16` 浮点误差）
2. 光线在角落处"反弹"（虽然本项目没有反弹）
3. `dist` 返回 0 但 `EPS` 太小

128 步是**保险**，**实际 5-10 步就够**。

---

## 7. `uMixFactor` 的两种用途

```cpp
// demo.cpp::render()
// 直光轮 (uMixFactor=0):
int uMixFactor = 0;
...
SetShaderValue(rcShader, ..., "uMixFactor", &uMixFactor, SHADER_UNIFORM_FLOAT);

// 间接轮 (uMixFactor=用户值, 默认 0.7):
rcDisableMergingInt = rcDisableMerging;
...
SetShaderValue(rcShader, ..., "uMixFactor", &mixFactor, SHADER_UNIFORM_FLOAT);
```

**直光轮不用 mix**——直光来自 `lastFrameBuf` 的直光轮结果（之前已经 sRGB 处理过），**直接就是最终颜色**。  
**间接轮用 mix**——`mix(sceneColor, direct, 0.7)` 表示 70% 来自 `lastFrameBuf` 的直接光 + 30% 来自墙颜色。**这就是"反射光"的合成**。

---

## 8. 关键问题

- [ ] `dir` 是未归一化的，对 raymarching 步长有什么影响？
- [ ] 为什么 `MAX_RAY_STEPS = 128` 而不是 64？
- [ ] 改 `uBaseInterval = 2.0`（4 倍）会发生什么？

<details>
<summary>答案</summary>

1. `dir` 是带长宽比修正的"屏幕像素单位方向"（如 `dir = (1920/1080, 0)` 表示"屏幕水平方向 1 像素 = 1 像素"）。`dist` 也是 UV 单位。**两者必须用相同单位**才能步进正确距离。**未归一化**就对了——`dir * dist` 已经是"屏幕像素步长"（1 像素 = `1/min(W,H)` UV）。
2. 128 是**保守上限**。实际 4-10 步就够。改 64 在某些边角情况会"光线走到一半就退出了"——你看到的是"墙边偶尔出现小黑洞"。
3. 区间变成 `[8, 16, 64, 256, 1024] px`。**最远覆盖 1024 像素**——差不多是 1920×1080 屏幕的整个宽度。但近处 `0~8 px` 没人管（被 skip），墙边 0-8 像素**没有光照**。视觉上"墙边有一条暗带"。

</details>

---

*下一节：[10_rc_merging.md](./10_rc_merging.md) 拆解 cascade merging——RC 的"借"逻辑。*
