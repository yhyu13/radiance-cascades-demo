# 05 · JFA · distfield.frag — 距离场提取

> 📁 源码：`res/shaders/distfield.frag`（17 行）  
> 📞 调用方：`demo.cpp::render()` 第 202-208 行  
> 🎯 **Goal**: 解释为什么 JFA 完成后还要做一次"提取"pass，以及位深从 `R32G32B32A32` 降到 `R16` 的好处。

---

## 1. 完整源码（带行号）

```glsl
 1  #version 330 core
 2  
 3  out vec4 fragColor;
 4  
 5  uniform sampler2D uJFA;
 6  // uniform vec2 uResolution;
 7  
 8  /*
 9   * this shader reads out the distance field contained within the JFA output.
10   * Useful as to reducing amount of data sent to RC shader
11   */
12  
13  void main() {
14    // vec2 fragCoord = gl_FragCoord.xy/uResolution; // for some reason fragTexCoord is just upside down sometimes? Raylib issue
15    vec2 fragCoord = gl_FragCoord.xy/textureSize(uJFA, 0);
16    fragColor = vec4(vec3(texture(uJFA, fragCoord).b), 1.0);
17  }
```

> 🪶 **只有 3 行实际代码**，是最简单的一个 shader。  
> 它的存在纯粹是**工程性**的：换位深。

---

## 2. 输入与输出

### 输入：`uJFA`（`R32G32B32A32`）

经过 N 轮 JFA 后，每像素：

| 通道 | 内容 | RC 需不需要？ |
|------|------|---------------|
| R | 最近种子的 U 坐标 | ❌ 不要 |
| G | 最近种子的 V 坐标 | ❌ 不要 |
| **B** | 到最近种子的距离 | **✅ RC 只看距离** |
| A | 1.0 | ❌ 不要 |

### 输出：`fragColor`（写入 `distFieldBuf`，`R16`）

```
R = G = B = 距离
A = 1.0
```

`R16` 格式：单通道 16-bit 半精度浮点。范围 0~65504，精度 ≈ 1/1024，**对 1920×1080 屏幕**（最远距离 = √(1920²+1080²) ≈ 2200 像素）精度 0.2 像素，**完全够用**。

---

## 3. 逐行拆解

### 第 15 行：归一化 UV

```glsl
vec2 fragCoord = gl_FragCoord.xy / textureSize(uJFA, 0);
```

和 `prepjfa.frag`、`jfa.frag` 完全一样的写法（**Raylib 跨平台**保险）。

### 第 16 行：提取 B 通道

```glsl
fragColor = vec4(vec3(texture(uJFA, fragCoord).b), 1.0);
```

把 `.b`（距离）**复制到 R、G、B 三个通道**。为什么要复制到 3 通道？

> 因为 `distFieldBuf` 的格式是 `PIXELFORMAT_UNCOMPRESSED_R16`，**只有 R 通道有数据**（虽然 `PIXELFORMAT_UNCOMPRESSED_R16` 的命名暗示"单通道"）。但 shader 输出 `vec4` 强制 4 通道，**重复填充 R=G=B=distance** 是个**约定**——下游 `rc.frag` 用 `.r` 通道读，**用哪个通道读都一样**。

---

## 4. 为什么需要这一步？直接用 `jfaBufferA` 不行吗？

### 4.1 位深节省

```
R32G32B32A32 = 4 * 32 = 128 bit/像素
R16          = 1 * 16 =  16 bit/像素
                           ─────
              节省 112 bit/像素 = 87.5%
```

1920×1080 屏幕：
- `jfaBufferA`：1920 × 1080 × 16 字节 = 33.2 MB
- `distFieldBuf`：1920 × 1080 × 2 字节 = **4.1 MB**

`rc.frag` 每帧每像素都采样距离场，**省下的带宽 = 8.5 倍**。

### 4.2 缓存友好

`R16` 单通道纹理 GPU L1 缓存命中率更高（连续 4 个像素 = 8 字节，**正好一个 cache line**）。

### 4.3 简化下游 shader

`rc.frag` 里写 `texture(uDistanceField, uv).r`，**比**写 `texture(uJFA, uv).b` 更**通用**——将来如果换成 SDF 真距离场（SDF 不需要 UV），代码不用动。

---

## 5. 与 CPU 端的对应

```cpp
// demo.cpp::setBuffers()
changeBitDepth(jfaBufferA,   PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
changeBitDepth(jfaBufferB,   PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
changeBitDepth(jfaBufferC,   PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
changeBitDepth(sceneBuf,     PIXELFORMAT_UNCOMPRESSED_R5G5B5A1);
changeBitDepth(distFieldBuf, PIXELFORMAT_UNCOMPRESSED_R16);
changeBitDepth(occlusionBuf, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA);
changeBitDepth(emissionBuf,  PIXELFORMAT_UNCOMPRESSED_R5G5B5A1);
```

**位深的选择是"按需分配"**：

| 缓冲 | 位深 | 原因 |
|------|------|------|
| `jfaBufferA/B/C` | `R32G32B32A32` | UV 编码需要 8-bit 以上的精度 |
| `distFieldBuf`   | `R16`           | 距离精度 1/1024 够用，省 8× 内存 |
| `sceneBuf`       | `R5G5B5A1`      | 只存"墙颜色"，不需要高精度 |
| `occlusionBuf`   | `GRAY_ALPHA`    | 2 通道 (灰度+透明度)，1 字节够用 |
| `emissionBuf`    | `R5G5B5A1`      | 颜色，5-bit/通道够用 |

> 🧠 **工程原则**：**每多 1 bit 都是带宽成本**。能省就省，但别为了省 bit 丢精度。

---

## 6. 关键问题

- [ ] 如果直接拿 `jfaBufferA` 给 `rc.frag` 用，会发生什么？
- [ ] 能不能让 `jfa.frag` 最后一轮**直接**输出 `R16`？
- [ ] 距离场是 `R16`，RC raymarching 步长会受 16-bit 精度限制吗？

<details>
<summary>答案</summary>

1. 能跑，但 (a) 浪费 8× 显存和带宽，(b) `rc.frag` 要改写为 `texture(uJFA, uv).b`，耦合了"距离在 B 通道"这个内部约定。
2. **不能**。中间 pass 用 `R32` 是为了让 1024 像素以上的屏幕**UV 编码不丢精度**；最后一轮把 `B` 通道单独搬到 `R16` 是用 shader 显式**做一次"位深转换"**——这是 GPU 的常见模式（color grading、LDR/HDR 转换等都是这个思路）。
3. 不会。R16 半精度浮点数 `1.0 + 0.001 = 1.001` 完全能表示，**精度 0.05 像素**对 1920×1080 屏幕来说**比一个像素小 20 倍**，raymarching 步长根本不会因为 R16 精度而失真。

</details>

---

*下一节：[06_jfa_cpu.md](./06_jfa_cpu.md) 跳到 C++ 端，看 CPU 怎么编排 JFA 的所有 pass。*
