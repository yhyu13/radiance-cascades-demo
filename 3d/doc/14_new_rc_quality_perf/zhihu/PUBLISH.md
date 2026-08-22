# 知乎专栏粘贴说明

知乎不渲染 mermaid，也不显示 SVG。本文配图已是 PNG，按顺序在编辑器里上传后，再贴正文。

知乎开放平台 CLI（`zhihu-cli` 0.2）只有读接口：搜索、热榜、直答、`me contents`。**没有创建或更新专栏的命令。** 「push」在本仓库里是 git push 粘贴包；上线仍需在 zhuanlan 编辑器里手动改稿。

## 标题

不要拿「看起来像路径追踪」当全局光照正确

## 导语（30–40 字，可作开头摘要）

正确、质量、性能是三层证据。混用它们去调 3D 全局光照，会把算法上限当成 bug。

## 配图上传顺序

1. `images/01-two-paths.png` — 两套 3D RC，证据不能混用
2. `images/02-three-layers.png` — 正确 / 质量 / 性能必须拆开
3. `images/03-quality-knobs.png` — 质量旋钮在耦合上，不在 gain 上
4. `images/04-split-dispatch.png` — Inactive 是矩形，杠杆是拆 dispatch
5. `images/05-roadmap.png` — 先控制面，再泛化网格

正文里的 `![…](images/….png)` 粘贴后不会自动带上本地文件；在对应位置插入刚上传的图即可。

## 和已有专栏的关系

账号里已有 2D Radiance Cascades 课程系列（Class 0–11）以及 `rc.frag` 详解。本文是 3D 默认内核（表面 RC）的方法论续篇，不是再开一节 2D 课，也不替换那些文章。

建议作为新专栏发布。若要改已有文章，只能在网页编辑器里手动更新——CLI 做不到。

## 标签建议

图形学、全局光照、Radiance Cascades、渲染、技术设计

## 来源

`3d/doc/14_new_rc_quality_perf/new_rc_quality_perf_plan.md`（Phase 12 调研分析，含已落地的 A–D 证据）
