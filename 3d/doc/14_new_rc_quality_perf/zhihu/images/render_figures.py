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


def round_box(draw, xy, fill=BOX, outline=BOX_LINE, width=2, radius=12):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def text(draw, xy, s, f=F_P, fill=INK, anchor="lt"):
    draw.text(xy, s, font=f, fill=fill, anchor=anchor)


def save(img: Image.Image, name: str) -> None:
    path = OUT / name
    img.save(path, "PNG", optimize=True)
    print("wrote", path)


def fig1_two_paths() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "同名「级联」，其实是两套算法", F_TITLE)
    text(d, (40, 78), "探针放在哪里、往哪边采样、在什么空间里合并，三者都不同。测量结果不能互换。", F_P, MUTED)

    round_box(d, (60, 140, 660, 740), fill=(232, 242, 232), outline=GREEN)
    text(d, (360, 175), "表面附着", F_H, GREEN, "mt")
    for i, line in enumerate([
        "探针长在表面上",
        "沿法线一侧打半球",
        "在表面参数空间里合并",
        "近处空间密、角度粗",
        "远处空间疏、角度细",
        "适合墙、地板这类封闭场景",
    ]):
        text(d, (100, 230 + i * 70), "•  " + line, F_P)

    round_box(d, (740, 140, 1340, 740), fill=(252, 240, 232), outline=AMBER)
    text(d, (1040, 175), "体空间网格", F_H, AMBER, "mt")
    for i, line in enumerate([
        "探针铺在空的体积里",
        "四面八方采样整球",
        "在三维网格里做三线性插值",
        "靠距离场引导步进",
        "空腔里的探针经常「看不见」小灯",
        "更像探针体 / DDGI 那一类",
    ]):
        text(d, (780, 230 + i * 70), "•  " + line, F_P)

    save(img, "01-two-paths.png")


def fig2_three_layers() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "「对不对」其实是三件不同的事", F_TITLE)
    text(d, (40, 78), "每一层有自己的证据。上一层红了，下一层的「更好看 / 更快」一律不算数。", F_P, MUTED)

    layers = [
        (GREEN, "第一层　实现是否等于定义",
         "探针布局、方向映射、合并权重、跨帧回读\n必须和参考算法逐项对上",
         "对不上 = 实现错了，停"),
        (ACCENT, "第二层　近似是否够用",
         "和路径追踪比能量、比空间分布\n粗方向分层是算法自己的上限",
         "不够 = 换分辨率档，不改定义"),
        (AMBER, "第三层　这一帧是否付得起",
         "GPU 时间、启动的线程、占用的显存\n必须量本算法，不能借另一套的数字",
         "贵 = 少做空转，不动公式"),
    ]
    x0 = 70
    for i, (color, title, body, rule) in enumerate(layers):
        y = 140 + i * 210
        round_box(d, (x0, y, 1330, y + 190), outline=color, width=3)
        d.rectangle((x0, y, x0 + 16, y + 190), fill=color)
        text(d, (x0 + 50, y + 24), title, F_H, color)
        for j, line in enumerate(body.split("\n")):
            text(d, (x0 + 50, y + 78 + j * 32), line, F_P)
        text(d, (1280, y + 95), rule, F_B, color, "rm")

    save(img, "02-three-layers.png")


def fig3_coupling() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "空间密度和角分辨率绑在同一个幂次上", F_TITLE)
    text(d, (40, 78), "探针格子变大一倍，方向数乘四，空间探针除四。总成本几乎不变，只是把预算从「密」换成「细」。", F_P, MUTED)

    round_box(d, (70, 150, 670, 430))
    text(d, (370, 180), "参考档　最细层 2×2 方向", F_H, ACCENT, "mt")
    text(d, (100, 230), "半球被切成四块", F_P)
    text(d, (100, 270), "小面积光源很容易撑满一整块", F_P)
    text(d, (100, 310), "对路径追踪大约偏亮一成", F_P)
    text(d, (100, 350), "这是定义本身，用来验收移植", F_B, GREEN)
    text(d, (100, 390), "不是「还没调好」", F_S, MUTED)

    round_box(d, (730, 150, 1330, 430))
    text(d, (1030, 180), "更细档　最细层 4×4 方向", F_H, AMBER, "mt")
    text(d, (760, 230), "立体角更小，小灯更不容易填满", F_P)
    text(d, (760, 270), "空间探针变稀，近处细节会换一种错", F_P)
    text(d, (760, 310), "必须单独对照路径追踪", F_P)
    text(d, (760, 350), "不能拿它顶替参考档的验收", F_B, AMBER)
    text(d, (760, 390), "换档，不是改定义", F_S, MUTED)

    round_box(d, (70, 470, 1330, 760), fill=(236, 240, 246))
    text(d, (100, 500), "看起来像质量旋钮、其实会毁掉验收的东西", F_H, RED)
    banned = [
        "乘一个增益",
        "加亮度地板",
        "用代理可见性",
        "两边一起夹能量",
        "改验收期望值",
        "用截图当证据",
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
    text(d, (40, 28), "空转是一块矩形留白，不是探针变稀疏", F_TITLE)
    text(d, (40, 78), "表面数据被打包进一张二维图。用不到的角落仍会被计算着色器扫到——除非你别启动那一块。", F_P, MUTED)

    text(d, (70, 130), "整张图一次启动", F_B)
    round_box(d, (70, 170, 70 + 520, 170 + 260), outline=LINE, width=2)
    d.rectangle((72, 172, 70 + 520 - 2, 170 + 130 - 1), fill=(196, 222, 196))
    d.rectangle((72, 170 + 130, 70 + 130 - 1, 170 + 260 - 2), fill=(196, 222, 196))
    d.rectangle((70 + 130, 170 + 130, 70 + 520 - 2, 170 + 260 - 2), fill=(230, 230, 232))
    text(d, (330, 230), "有表面数据", F_S, GREEN, "mm")
    text(d, (200, 365), "有数据", F_S, GREEN, "mm")
    text(d, (420, 365), "留白 37.5%", F_S, MUTED, "mm")
    text(d, (330, 450), "每个像素都启动线程", F_P, MUTED, "mt")

    d.polygon([(640, 300), (700, 280), (700, 270), (760, 310), (700, 350), (700, 340)], fill=ACCENT)

    text(d, (800, 130), "按有数据的区域启动", F_B)
    round_box(d, (800, 170, 800 + 520, 170 + 130), outline=GREEN, width=2, fill=(196, 222, 196))
    text(d, (1060, 235), "上半张：整宽都有数据", F_P, GREEN, "mm")
    round_box(d, (800, 320, 800 + 130, 320 + 130), outline=GREEN, width=2, fill=(196, 222, 196))
    text(d, (865, 385), "左下角", F_S, GREEN, "mm")
    text(d, (1100, 385), "右下角不再启动", F_P, MUTED, "mm")
    text(d, (1060, 450), "线程少 37.5%，图像误差不变", F_P, GREEN, "mt")

    text(d, (70, 520), "计时器会骗人，线程数不会", F_H)
    text(d, (70, 570), "用调试器回放同一帧，GPU 时间常常抖两三倍。两次测量相除，不能写成「加速了四点八倍」。", F_P)
    text(d, (70, 620), "可以写进报告的是：少启动了多少线程，验收误差有没有变。", F_P)
    text(d, (70, 670), "下一步若还要快，先看新的测量：带宽不够再降精度；远场级联贵再隔帧。没有新数字，就没有下一刀。", F_P)
    text(d, (70, 730), "乘增益让图变快变亮，两件事都没做。", F_P, MUTED)

    save(img, "04-split-dispatch.png")


def fig5_roadmap() -> None:
    img, d = new_canvas()
    text(d, (40, 28), "先能验收，再换更复杂的几何", F_TITLE)
    text(d, (40, 78), "把实验室场景换成真实网格时，表面块数和求交成本会一起跳。没有基线，慢了无法归因。", F_P, MUTED)

    boxes = [
        (GREEN, "1　钉死定义", "在简单封闭场景上\n证明实现等于参考算法"),
        (ACCENT, "2　分开三层证据", "实现、近似、成本\n各用各的门，互不顶替"),
        (AMBER, "3　先打包表面", "只在 CPU 上切开、摆好\n还不改求交"),
        (MUTED, "4　再接到真网格", "带着能量报告和耗时预算\n不允许用盒子冒充表面"),
    ]
    for i, (color, title, body) in enumerate(boxes):
        x = 50 + i * 335
        round_box(d, (x, 160, x + 310, 430), outline=color, width=3)
        text(d, (x + 155, 200), title, F_H, color, "mt")
        for j, line in enumerate(body.split("\n")):
            text(d, (x + 155, 270 + j * 42), line, F_P, INK, "mt")
        if i < 3:
            d.polygon([(x + 318, 285), (x + 334, 295), (x + 318, 305)], fill=LINE)

    round_box(d, (50, 480, 1350, 760), fill=(236, 240, 246))
    text(d, (80, 510), "换几何之前先写下来的三句话", F_H)
    text(d, (80, 565), "能量对比用线性高动态范围图像，不用经过色调映射的截图。", F_P)
    text(d, (80, 615), "耗时相对当前基线给一个倍数，超过了再谈下一刀优化。", F_P)
    text(d, (80, 665), "盒子、包围盒平面不能冒充「表面级联已经支持网格」。", F_P)
    text(d, (80, 715), "用观感顶替实现验收，或用另一套算法的毫秒数当基线，直接停。", F_P, RED)

    save(img, "05-roadmap.png")


if __name__ == "__main__":
    fig1_two_paths()
    fig2_three_layers()
    fig3_coupling()
    fig4_split_dispatch()
    fig5_roadmap()
