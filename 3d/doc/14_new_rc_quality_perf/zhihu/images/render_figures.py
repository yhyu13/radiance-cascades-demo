"""Render PNG figures for the Zhihu pack. Light background, Chinese labels."""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent
W, H = 1400, 820
BG = (248, 249, 251)
INK = (28, 32, 38)
MUTED = (90, 98, 110)
LINE = (48, 54, 64)
ACCENT = (30, 90, 160)
GREEN = (36, 120, 72)
AMBER = (160, 96, 24)
RED = (150, 48, 48)
WHITE = (255, 255, 255)
BOX = (255, 255, 255)
BOX_LINE = (200, 206, 214)


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        r"C:\Windows\Fonts\msyhbd.ttc" if bold else r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\msyh.ttc",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


F_TITLE = font(32, True)
F_H = font(22, True)
F_B = font(18, True)
F_P = font(16)
F_S = font(14)


def new_canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (W, H), BG)
    return img, ImageDraw.Draw(img)


def round_box(draw: ImageDraw.ImageDraw, xy, fill=BOX, outline=BOX_LINE, width=2, radius=12):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def text(draw, xy, s, f=F_P, fill=INK, anchor="lt"):
    draw.text(xy, s, font=f, fill=fill, anchor=anchor)


def save(img: Image.Image, name: str) -> None:
    path = OUT / name
    img.save(path, "PNG", optimize=True)
    print("wrote", path)


def fig1_two_paths() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "两套 3D RC，证据不能混用", F_TITLE)
    text(d, (40, 78), "默认路径才是 New RC。Legacy 的毫秒数、α-gate、Cornell 审计不能当它的成绩单。", F_P, MUTED)

    round_box(d, (60, 140, 660, 740), fill=(232, 242, 232), outline=GREEN)
    text(d, (360, 175), "New RC · 默认 App3D", F_H, GREEN, "mt")
    for i, line in enumerate([
        "表面附着探针",
        "半球采样 + 表面空间合并",
        "reference_transport.comp",
        "G0–G10 语义门禁",
        "质量档 parity / high-c0",
        "rdc 基线：拆 dispatch 后 ~6 ms",
    ]):
        text(d, (100, 230 + i * 70), "•  " + line, F_P)

    round_box(d, (740, 140, 1340, 740), fill=(252, 240, 232), outline=AMBER)
    text(d, (1040, 175), "Legacy · --runtime-shell=legacy", F_H, AMBER, "mt")
    for i, line in enumerate([
        "体网格探针",
        "八面体全方向 + SDF 步进",
        "radiance_3d.comp",
        "Era 11/12 Cornell 审计",
        "α-gate / (4/D²) 争论",
        "rdc：C2 bake 10.64 ms",
    ]):
        text(d, (780, 230 + i * 70), "•  " + line, F_P)

    save(img, "01-two-paths.png")


def fig2_three_layers() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "正确 / 质量 / 性能 必须拆开", F_TITLE)
    text(d, (40, 78), "红了停在本层。不许用「更好看」或「更快」覆盖上一层。", F_P, MUTED)

    layers = [
        (GREEN, "L1 语义正确", "G0–G10 PASS\n公式、布局、merge、feedback", "红 = 算法坏了，停"),
        (ACCENT, "L2 算法质量", "命名档的 PT EXR\nparity 允许 2×2 C0 缺口", "差 = 换档，不改 parity 常量"),
        (AMBER, "L3 产品性能", "rdc Duration + occupancy\n相对已入库 JSON", "差 = 动 dispatch/格式/skip"),
    ]
    x0 = 70
    for i, (color, title, body, rule) in enumerate(layers):
        y = 140 + i * 210
        round_box(d, (x0, y, 1330, y + 190), outline=color, width=3)
        d.rectangle((x0, y, x0 + 16, y + 190), fill=color)
        text(d, (x0 + 50, y + 28), title, F_H, color)
        for j, line in enumerate(body.split("\n")):
            text(d, (x0 + 50, y + 78 + j * 32), line, F_P)
        text(d, (1280, y + 95), rule, F_B, color, "rm")

    save(img, "02-three-layers.png")


def fig3_coupling() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "质量旋钮在耦合上，不在 gain 上", F_TITLE)
    text(d, (40, 78), "probeSize = 2^(cascade+1)。空间密度和角分辨率幂次互换，总成本同阶。", F_P, MUTED)

    round_box(d, (70, 150, 670, 430))
    text(d, (370, 180), "Parity 档 · C0 = 2×2", F_H, ACCENT, "mt")
    text(d, (100, 230), "probeSize = 2^(c+1)", F_P)
    text(d, (100, 270), "半球只有 4 个方向", F_P)
    text(d, (100, 310), "对 PT ratio ≈ 1.084（~13% luma）", F_P)
    text(d, (100, 350), "唯一能过 G0–G10 的档", F_B, GREEN)
    text(d, (100, 390), "小灯打进一个 bin = 填满立体角", F_S, MUTED)

    round_box(d, (730, 150, 1330, 430))
    text(d, (1030, 180), "High-C0 档 · C0 = 4×4", F_H, AMBER, "mt")
    text(d, (760, 230), "只改 shader：uC0Log2Offset = 1", F_P)
    text(d, (760, 270), "不改 reference_layout.h", F_P)
    text(d, (760, 310), "validation 拒绝（exit 2）", F_P)
    text(d, (760, 350), "独立 PT EXR，单独记账", F_B, AMBER)
    text(d, (760, 390), "用来压 alias，不是「修 port」", F_S, MUTED)

    round_box(d, (70, 470, 1330, 760), fill=(236, 240, 246))
    text(d, (100, 500), "禁止当作质量旋钮的东西", F_H, RED)
    banned = [
        "gain / MB 增益",
        "irradiance floor",
        "proxy visibility",
        "对称 neighborhood clamp",
        "改 G0–G10 期望值",
        "用 PNG 观感推进",
    ]
    for i, item in enumerate(banned):
        col, row = i % 3, i // 3
        x = 120 + col * 400
        y = 560 + row * 70
        round_box(d, (x, y, x + 340, y + 52), outline=RED)
        text(d, (x + 170, y + 26), item, F_P, RED, "mm")

    save(img, "03-quality-knobs.png")


def fig4_split_dispatch() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "Inactive 是矩形，所以杠杆是拆 dispatch", F_TITLE)
    text(d, (40, 78), "1024×512 里 37.5% 是 interior padding（x≥256 且 y≥256），不是探针稀疏。", F_P, MUTED)

    # Full atlas
    text(d, (70, 130), "全 atlas：1024 × 512", F_B)
    ax, ay, cell = 70, 170, 1
    # draw simplified 4x2 grid representing pages
    # primary 1024x256 active green, interior left 256x256 green, rest gray
    scale_x = 520 / 1024
    scale_y = 260 / 512
    # full rect
    round_box(d, (70, 170, 70 + 520, 170 + 260), outline=LINE, width=2)
    # primary active
    d.rectangle((72, 172, 70 + 520 - 2, 170 + 130 - 1), fill=(196, 222, 196))
    # interior used
    d.rectangle((72, 170 + 130, 70 + 130 - 1, 170 + 260 - 2), fill=(196, 222, 196))
    # padding
    d.rectangle((70 + 130, 170 + 130, 70 + 520 - 2, 170 + 260 - 2), fill=(230, 230, 232))
    text(d, (330, 230), "primary 全宽有效", F_S, GREEN, "mm")
    text(d, (200, 365), "interior 有效", F_S, GREEN, "mm")
    text(d, (420, 365), "padding 37.5%", F_S, MUTED, "mm")
    text(d, (330, 450), "6 × 1024×512 thread", F_P, MUTED, "mt")

    # Arrow
    d.polygon([(640, 300), (700, 280), (700, 270), (760, 310), (700, 350), (700, 340)], fill=ACCENT)

    # Split
    text(d, (800, 130), "拆开：跳过 padding", F_B)
    round_box(d, (800, 170, 800 + 520, 170 + 130), outline=GREEN, width=2, fill=(196, 222, 196))
    text(d, (1060, 235), "primary 1024 × 256", F_P, GREEN, "mm")
    round_box(d, (800, 320, 800 + 130, 320 + 130), outline=GREEN, width=2, fill=(196, 222, 196))
    text(d, (865, 385), "256×256", F_S, GREEN, "mm")
    text(d, (1060, 385), "不再 dispatch 这块", F_P, MUTED, "mm")
    text(d, (1060, 450), "线程 −37.5%；G9 误差不变", F_P, GREEN, "mt")

    text(d, (70, 520), "诚实说法", F_H)
    text(d, (70, 570), "Replay 的 GPU Duration 会抖 ±2–5×，不要拿 28.6 ms / 5.95 ms 宣称 4.8 倍加速。", F_P)
    text(d, (70, 610), "可写进报告的只有：线程数少了 37.5%；G9 max_pixel_error 仍是 0.0806657。", F_P)
    text(d, (70, 650), "当前生产基线是拆 dispatch 之后的 JSON，不是拆之前那次非单调 capture。", F_P)
    text(d, (70, 710), "下一步若还要快：有数字才许动 RGBA16F 或隔帧远 cascade。不许加 gain。", F_P, MUTED)

    save(img, "04-split-dispatch.png")


def fig5_roadmap() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "先控制面，再泛化网格", F_TITLE)
    text(d, (40, 78), "Sponza UV2 会同时放大 chart 数和 primitive 数。没有 Cornell 基线，慢了无法归因。", F_P, MUTED)

    boxes = [
        (GREEN, "Phase 0–10", "语义内核\nG0–G10 PASS"),
        (ACCENT, "Phase 12", "三层控制面\nA 过滤 · B 基线\nC 档位 · D 拆 dispatch"),
        (AMBER, "Phase 11 M1", "CPU UV2 packer\n不碰 GPU"),
        (MUTED, "M2 / M3", "kind-5 tracer\nSponza 实跑\n预提交 EXR + 预算"),
    ]
    for i, (color, title, body) in enumerate(boxes):
        x = 50 + i * 335
        round_box(d, (x, 160, x + 310, 430), outline=color, width=3)
        text(d, (x + 155, 195), title, F_H, color, "mt")
        for j, line in enumerate(body.split("\n")):
            text(d, (x + 155, 260 + j * 40), line, F_P, INK, "mt")
        if i < 3:
            d.polygon(
                [(x + 318, 285), (x + 334, 295), (x + 318, 305)],
                fill=LINE,
            )

    round_box(d, (50, 480, 1350, 760), fill=(236, 240, 246))
    text(d, (80, 510), "M2/M3 的预提交门禁", F_H)
    text(d, (80, 560), "质量：HDR EXR，禁止 PNG 观感。Parity 档继续过 G0–G10。High-c0 单独记账。", F_P)
    text(d, (80, 610), "性能：相对拆 dispatch 后的 JSON 给倍数预算，超了再谈下一刀。", F_P)
    text(d, (80, 660), "几何：Sponza 不准用 AABB / box proxy 冒充 surface RC。", F_P)
    text(d, (80, 710), "混用 L1/L2/L3，或用截图推进 = STOP。这是 v2.x 整条校正线失败后留下的纪律。", F_P, RED)

    save(img, "05-roadmap.png")


if __name__ == "__main__":
    fig1_two_paths()
    fig2_three_layers()
    fig3_coupling()
    fig4_split_dispatch()
    fig5_roadmap()
