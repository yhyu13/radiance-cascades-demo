#version 330 core
// 版本声明：使用 OpenGL 着色语言 3.30 核心配置

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

out vec4 fragColor;
// 输出变量：片段着色器的最终颜色输出（RGBA 四通道）

uniform sampler2D uDistanceField;
// 均匀变量：距离场纹理采样器，存储每个像素到最近表面的距离

uniform sampler2D uSceneMap;
// 均匀变量：场景地图纹理采样器，存储场景的颜色/材质信息

uniform sampler2D uDirectLighting;
// 均匀变量：直接光照纹理采样器，存储直接光源照射的亮度

uniform sampler2D uLastPass;
// 均匀变量：上一遍渲染结果纹理，用于级联之间的数据传递

uniform vec2  uResolution;
// 均匀变量：屏幕分辨率（宽度 x 高度），用于坐标转换

uniform int   uBaseRayCount;
// 均匀变量：基础光线数量，决定每个探针发射的光线数目

uniform int   uCascadeDisplayIndex;
// 均匀变量：当前显示的级联层级索引，用于调试可视化

uniform int   uCascadeIndex;
// 均匀变量：当前处理的级联层级索引（0 表示第一级，数值越大级别越高）

uniform int   uCascadeAmount;
// 均匀变量：级联总层数，表示有多少个不同尺度的辐射缓存层

uniform int   uSrgb;
// 均匀变量：sRGB 色彩空间开关（1 表示启用线性到 sRGB 的转换）

uniform float uPropagationRate;
// 均匀变量：传播速率，控制间接光照的扩散强度

uniform int   uDisableMerging;
// 均匀变量：禁用合并开关（1 表示禁止从上级级联合并数据）

uniform float uBaseInterval;
// 均匀变量：基础间隔距离，以像素为单位的光线追踪起始距离

uniform float uMixFactor;
// 均匀变量：混合因子，控制直接光照与间接光照的混合比例

uniform int   uAmbient; // bool
// 均匀变量：环境光开关（类似布尔值，1 表示启用环境光贡献）

uniform vec3  uAmbientColor;
// 均匀变量：环境光颜色（RGB 三色分量）

struct probe {
  float spacing;       // 探针间距 = 每维度的探针数量，例如 1, 2, 4, 16（级联层级越高，探针越稀疏）
  vec2 size;           // 探针尺寸 = 在屏幕空间中每个探针占据的大小，例如 1.0x1.0(全屏), 0.5x0.5(四分之一屏) 等
  vec2 position;       // 探针位置 = 在当前层级探针网格内的相对坐标（0~spacing 范围）
  // vec2 center;         // 探针中心 = 当前探针覆盖区域的中心点坐标
  vec2 rayPosition;    // 光线位置 = 标识这是哪个探针发出的光线（整数网格坐标）
  float intervalStart; // 追踪区间起点 = 光线开始追踪的距离阈值（离相机近的距离）
  float intervalEnd;   // 追踪区间终点 = 光线结束追踪的距离阈值（离相机远的距离）
  float rayCount;      // 光线总数 = 该层级探针向周围发射的总光线数目（角度分辨率）
};
// 结构体定义：探针（probe）是辐射度采样的基本单元
// 想象成在屏幕上撒网：第一级网眼很密（很多小探针），越高级网眼越疏（大探针覆盖更大区域）

probe get_probe_info(int index) {
  probe p;
  // 创建探针信息结构体实例
  
  vec2 fragCoord = gl_FragCoord.xy/uResolution;
  // 将当前片段的屏幕坐标转换为归一化坐标 (0~1 范围)
  
  // amount of probes in our current cascade
  // [1, 4, 16, 256, ...]
  // 当前级联层的探针总数 = baseRayCount 的 index 次幂
  // 第 0 级：1 个探针（全覆盖）
  // 第 1 级：4 个探针（2×2 网格）
  // 第 2 级：16 个探针（4×4 网格）
  // 第 3 级：256 个探针（16×16 网格）
  // 级联层级越高，探针数量呈指数增长，覆盖越精细
  float probeAmount = pow(uBaseRayCount, index);
  
  p.spacing = sqrt(probeAmount); 
  // 计算每维度的探针数量（即网格密度）
  // 例如：probeAmount=16 → spacing=4（表示 4×4 的网格）
  
  // screen size of a probe in our current cascade
  // [resolution/1, resolution/2, resolution/4,  resolution/16, ...]
  // [1.0x1.0,      0.5x0.05,     0.25x0.25,     0.0625x0.0625, ...]
  // 当前级联中单个探针的屏幕尺寸
  // 第 0 级：1.0×1.0（一个探针覆盖整个屏幕）
  // 第 1 级：0.5×0.5（一个探针覆盖屏幕的四分之一）
  // 第 2 级：0.25×0.25（一个探针覆盖屏幕的十六分之一）
  // 级联层级越高，每个探针覆盖的区域越小，采样越密集
  p.size = 1.0/vec2(p.spacing);
  
  // current position within a probe in our current cascade
  // 计算当前片段在探针网格中的相对位置
  // mod 取模运算：将屏幕分割成 spacing×spacing 个小格子
  // 例如 spacing=4 时，position 会在 0~4 范围内循环
  p.position = mod(fragCoord, p.size) * p.spacing;
  
  // centre of current probe
  // p.center = (p.position + vec2(0.5/uResolution)) * p.spacing / uResolution;
  // （已注释）探针中心点坐标的计算公式
  
  p.rayCount = pow(uBaseRayCount, index+1); 
  // 计算该层级的光线总数（角度分辨率）
  // 级联层级越高，每个探针发射的光线越多，角度采样越精细
  // 例如：index=0 时 rayCount=base，index=1 时 rayCount=base²
  
  // calculate which group of rays we're calculating this pass
  // 计算当前片段属于哪个探针组（整数网格坐标）
  // floor 向下取整：将屏幕划分成多个探针区域
  p.rayPosition = floor(fragCoord / p.size);
  
  float a = uBaseInterval; // px
  // 基础间隔距离（单位：像素），光线追踪的基准步长
  
  // 计算光线追踪的距离区间
  // 第 0 级：从 0 开始到 base*base/minRes
  // 第 1 级：从 base*base/minRes 到 base*base²/minRes
  // 级联层级越高，追踪的距离越远，覆盖范围越大
  p.intervalStart = (FIRST_LEVEL) ? 0.0 : a * pow(uBaseRayCount, index) / min(uResolution.x, uResolution.y);
  // 区间起点：第一级从 0 开始，其他级别从前一级的终点开始
  p.intervalEnd = a * pow(uBaseRayCount, index+1) / min(uResolution.x, uResolution.y);
  // 区间终点：随级联层级指数增长
  
  return p;
}
// 函数功能：根据级联索引获取探针的所有属性信息
// 核心思想：级联就像俄罗斯套娃，外层（高级联）覆盖大范围但粗糙，内层（低级联）覆盖小范围但精细

// sourced from https://gist.github.com/Reedbeta/e8d3817e3f64bba7104b8fafd62906dfj
vec3 lin_to_srgb(vec3 rgb)
{
  // 线性色彩空间转换到 sRGB 色彩空间
  // 原理：人眼对暗部更敏感，sRGB 通过伽马校正压缩亮部、扩展暗部
  // 公式：当 rgb > 0.0031308 时用幂函数曲线，否则用线性段
  return mix(1.055 * pow(rgb, vec3(1.0 / 2.4)) - 0.055,
             rgb * 12.92,
             lessThanEqual(rgb, vec3(0.0031308)));
}

vec3 srgb_to_lin(vec3 rgb)
{
  // sRGB 色彩空间转换回线性色彩空间（上述函数的逆运算）
  // 用于在着色器内部进行正确的物理光照计算
  return mix(pow((rgb + 0.055) * (1.0 / 1.055), vec3(2.4)),
             rgb * (1.0/12.92),
             lessThanEqual(rgb, vec3(0.04045)));
}

// // sourced from https://www.shadertoy.com/view/NttSW7
// vec3 sky_integral(float a0, float a1) {
//     vec3 SI = SkyColor*(a1-a0-0.5*(cos(a1)-cos(a0)));
//     SI += SunColor*(atan(SSunS*(SunA-a0))-atan(SSunS*(SunA-a1)))*ISSunS;
//     return SI;
// }
// （已注释）天空光照积分计算的备用代码

// altered raymarching function; only calculates coordinates with a distance between a and b
vec4 radiance_interval(vec2 uv, vec2 dir, float a, float b) {
  // 改进版光线步进函数：只计算距离在区间 [a, b] 范围内的辐射度
  // uv: 起始归一化坐标，dir: 光线方向向量，a: 起点距离，b: 终点距离
  
  uv += a * dir;
  // 将光线起点沿方向移动距离 a（跳到该区间的起始位置）
  
  float travelledDist = a;
  // 记录光线已经走过的总距离，初始为区间起点
  
  for (int i = 0; i < MAX_RAY_STEPS; i++) {
    // 开始光线步进循环，最多迭代 MAX_RAY_STEPS 次
    
    float dist = texture(uDistanceField, uv).r;         // sample distance field
    // 从距离场纹理中采样当前 UV 位置的最近表面距离
    // .r 表示取红色通道的值（距离值存储在 R 通道）
    
    uv += (dir * dist); // march our ray
    // 沿光线方向前进距离 dist（这就是"光线步进"的核心：每次跳到一个新位置）
    
    // skip UVs outside of the window
    if (uv.xy != clamp(uv.xy, 0.0, 1.0))
      break;
    // 边界检查：如果 UV 坐标超出 [0,1] 范围（跳出屏幕），立即终止光线
    // clamp 函数将 UV 限制在 0~1 之间，如果超出说明光线射出屏幕了
    
    // surface hit
    if (dist < EPS) {
      // 表面命中判断：当采样距离小于极小值 EPS 时，认为光线击中了物体表面
      
      if (uMixFactor != 0) {
        // 如果启用了混合模式（需要结合直接光照和间接光照）
        
        return vec4(
            mix(
              texture(uSceneMap, uv).rgb,
              // 场景本身的颜色（物体表面颜色）
              
              max(
                texture(uDirectLighting, vec2(uv.x, -uv.y)).rgb,
                // 直接光照亮度（注意 Y 坐标翻转，可能是镜像对称处理）
                
                texture(uDirectLighting, vec2(uv.x, -uv.y) - (dir * (1.0/uResolution))).rgb * uPropagationRate
                // 相邻像素的直接光照 × 传播速率（模拟光的扩散效果）
              ),
              uMixFactor
              // 混合因子：控制直接光照和间接光照的权重
            ),
            1.0);
        // 返回混合后的颜色，透明度为 1.0（完全不透明）
      }
      
      return vec4(texture(uSceneMap, uv).rgb, 1.0); 
      // 简单模式：直接返回物体表面颜色
      // 注释说虽然有些冗余但去掉会损失 2-3 FPS（性能优化妥协）
    }
    
    travelledDist += dist;
    // 累加已走过的距离
    
    if (travelledDist >= b)
      break;
    // 距离检查：如果累计走过的距离超过区间终点 b，停止光线追踪
    // 这确保了我们只计算指定区间的辐射度贡献
  }
  
  return vec4(0.0);
  // 如果光线没有击中任何表面（走出了区间或屏幕），返回黑色（无辐射度贡献）
}
// 函数核心作用：模拟光线在距离区间 [a,b] 内的传播，收集沿途遇到的表面辐射度

void main() {
  // 主函数：片段着色器的入口点，对每个像素执行一次
  
  vec4 radiance = vec4(0.0);
  // 初始化辐射度累积值为零（黑灰色，无光照）
  
  probe p = get_probe_info(uCascadeIndex);
  // 获取当前级联层的探针信息（当前正在处理哪一层）
  
  probe up = get_probe_info(uCascadeIndex+1);
  // 获取上一级级联层的探针信息（更高一层，覆盖范围更大的探针）
  
  float baseIndex = float(uBaseRayCount) * (p.rayPosition.x + (p.spacing * p.rayPosition.y));
  // 计算基础光线索引：根据当前探针在网格中的位置，确定这是第几个探针
  // 这确保每个探针对应一组独立的光线索引，不会重复
  
  for (float i = 0.0; i < uBaseRayCount; i++) {
    // 对每个探针发射的所有光线进行循环
    // 例如 uBaseRayCount=4 时，每个探针向 4 个不同角度发射光线
    
    float index = baseIndex + i;
    // 计算当前光线的总索引（全局唯一标识）
    
    float angle = (index / p.rayCount) * TWO_PI;
    // 计算光线的发射角度：将索引映射到 0~2π 的圆周角
    // 例如：index=0→0 度，index=rayCount/4→90 度，index=rayCount/2→180 度
    // 这样光线就均匀分布在 360 度全方位
    
    vec4 deltaRadiance = vec4(0.0);
    // 初始化当前光线的辐射度增量为零
    
    deltaRadiance += radiance_interval(
      p.position,
      // 起始位置：当前探针内部的相对坐标
      
      vec2(cos(angle) * min(uResolution.x, uResolution.y) / max(uResolution.x, uResolution.y), sin(angle)),
      // 光线方向向量：根据角度计算单位方向
      // X 分量乘以纵横比补偿因子，确保在宽屏下角度仍然正确
      // cos(angle): X 方向分量，sin(angle): Y 方向分量
      
      p.intervalStart,
      // 追踪区间起点（离相机较近的距离）
      
      p.intervalEnd
      // 追踪区间终点（离相机较远的距离）
    );
    // 调用光线步进函数，计算这条光线在指定区间内收集的辐射度
    
    // merging
    if (!(LAST_LEVEL) && deltaRadiance.a == 0.0 && uDisableMerging != 1.0) {
      // 合并逻辑：如果不是最后一级 & 当前光线没有击中任何表面 & 未禁用合并功能
      
      up.position = vec2(
        mod(index, up.spacing), floor(index / up.spacing)
      ) * up.size;
      // 计算上级探针的位置：将当前光线索引映射到上级探针网格坐标
      // mod: 计算在上级网格中的 X 坐标，floor/spacing: 计算 Y 坐标
      
      #define PIXEL vec2(1.0)/uResolution
      // 定义一个像素的归一化大小（用于边界保护）
      
      vec2 offset = p.position / up.spacing;
      // 计算偏移量：将当前探针位置转换到上级探针的相对坐标
      
      offset = clamp(offset, PIXEL, up.size - PIXEL);
      // 限制偏移范围：确保不超出上级探针的边界（留出至少一个像素的余量）
      
      vec2 uv = up.position + offset;
      // 最终 UV 坐标 = 上级探针位置 + 偏移量
      
      deltaRadiance += texture(uLastPass, uv);
      // 从上一遍渲染结果中采样辐射度值（从更粗糙的级联层借用数据）
      // 这就是"级联合并"：当精细级别没有数据时，回退到粗糙级别
    }
    
    radiance += deltaRadiance;
    // 将当前光线的辐射度贡献累加到总辐射度中
  }
  
  radiance /= uBaseRayCount;
  // 平均化：将所有光线的辐射度总和除以光线数量，得到平均辐射度
  // 这样无论发射多少条光线，结果都是标准化的
  
  radiance += vec4(uAmbientColor*uAmbient*0.005, 1.0);
  // 添加环境光贡献：环境光颜色 × 开关状态 × 微弱系数 (0.005)
  // 即使没有直接光照，也有微弱的底色（模拟全局环境光）
  
  if (uCascadeIndex < uCascadeDisplayIndex) radiance = vec4(vec3(texture(uLastPass, gl_FragCoord.xy/uResolution)), 1.0);
  // 级联显示控制：如果当前级联低于显示阈值，直接显示上一遍的结果
  // 这用于调试可视化：可以选择性地查看某一级联层的贡献
  
  // if (uMixFactor != 0 ) fragColor = texture(uDirectLighting, gl_FragCoord.xy/uResolution);
  // （已注释）调试用代码：如果启用混合，直接显示直接光照纹理
  
  fragColor = vec4((FIRST_LEVEL && uSrgb == 1) ? lin_to_srgb(radiance.rgb) : radiance.rgb, 1.0);
  // 最终输出：如果是第一级且启用 sRGB 转换，则将线性色彩转为 sRGB 格式
  // 这样做是为了正确显示：显示器期望 sRGB 信号，而内部计算用线性空间
}
// 主函数总结：
// 1. 对每个像素，确定它属于哪个探针
// 2. 对该探针发射的多条光线进行追踪
// 3. 每条光线在指定距离区间内步进，收集表面辐射度
// 4. 如果没有击中表面，从上级级联"借用"数据（多尺度融合）
// 5. 所有光线的结果平均后，加上环境光，输出最终颜色