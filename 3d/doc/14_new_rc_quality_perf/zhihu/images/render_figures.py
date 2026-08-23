"""Zhihu figures: short Chinese labels, diagrams over jargon lists."""
from __future__ import annotations

from pathlib import Path
import math

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent
W, H = 1400, 780
BG = (248, 249, 251)
INK = (28, 32, 38)
MUTED = (90, 98, 110)
ACCENT = (30, 90, 160)
GREEN = (36, 120, 72)
AMBER = (160, 96, 24)
RED = (150, 48, 48)
BOX = (255, 255, 255)
BOX_LINE = (200, 206, 214)
WALL = (70, 78, 90)
LIGHT = (230, 180, 40)
FILL_G = (196, 222, 196)
FILL_A = (252, 232, 204)
FILL_GRAY = (228, 230, 234)


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    paths = [
        r"C:\Windows\Fonts\msyhbd.ttc" if bold else r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
    ]
    for path in paths:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


F_TITLE = font(34, True)
F_H = font(22, True)
F_P = font(17)
F_S = font(15)


def canvas():
    img = Image.new("RGB", (W, H), BG)
    return img, ImageDraw.Draw(img)


def box(d, xy, fill=BOX, outline=BOX_LINE, width=2, r=14):
    d.rounded_rectangle(xy, radius=r, fill=fill, outline=outline, width=width)


def t(d, xy, s, f=F_P, fill=INK, anchor="lt"):
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


def save(img, name):
    path = OUT / name
    img.save(path, "PNG", optimize=True)
    print("wrote", path)


def arrow(d, a, b, fill=ACCENT, w=4, head=14):
    d.line([a, b], fill=fill, width=w)
    ang = math.atan2(b[1] - a[1], b[0] - a[0])
    p1 = (b[0] - head * math.cos(ang - 0.45), b[1] - head * math.sin(ang - 0.45))
    p2 = (b[0] - head * math.cos(ang + 0.45), b[1] - head * math.sin(ang + 0.45))
    d.polygon([b, p1, p2], fill=fill)


def fan(d, cx, cy, n, length, spread=math.pi * 0.7, fill=GREEN, w=3):
    a0 = -math.pi / 2 - spread / 2
    for i in range(n):
        a = a0 + spread * (i + 0.5) / n
        arrow(d, (cx, cy), (cx + length * math.cos(a), cy + length * math.sin(a)),
              fill=fill, w=w, head=9)
    d.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), fill=fill, outline=WALL)


def fig0():
    img, d = canvas()
    t(d, (40, 28), "近处排得密，远处看得细", F_TITLE)
    t(d, (40, 78), "总预算差不多。近处用「人多」换「每人只看几眼」；远处反过来。", F_P, MUTED)

    box(d, (50, 140, 670, 700), fill=(236, 246, 236), outline=GREEN, width=3)
    t(d, (360, 175), "近处", F_H, GREEN, "mt")
    d.line([(120, 560), (600, 560)], fill=WALL, width=8)
    for x in (160, 240, 320, 400, 480, 560):
        fan(d, x, 552, 4, 95, fill=GREEN, w=3)
    t(d, (360, 620), "探针多，每个只问四个方向", F_P, GREEN, "mt")
    t(d, (360, 660), "管墙根那一米：接触阴影、颜色渗透", F_S, MUTED, "mt")

    box(d, (730, 140, 1350, 700), fill=(236, 242, 250), outline=ACCENT, width=3)
    t(d, (1040, 175), "远处", F_H, ACCENT, "mt")
    d.line([(800, 560), (1280, 560)], fill=WALL, width=8)
    for x in (920, 1160):
        fan(d, x, 552, 12, 230, fill=ACCENT, w=2)
    t(d, (1040, 620), "探针少，每个把天空切得很细", F_P, ACCENT, "mt")
    t(d, (1040, 660), "管房间另一头过来的间接光", F_S, MUTED, "mt")
    save(img, "00-cascades.png")


def fig1():
    img, d = canvas()
    t(d, (40, 28), "探针放在墙上，还是放在空里？", F_TITLE)
    t(d, (40, 78), "同一套「分层」想法，两种完全不同的摆法。", F_P, MUTED)

    box(d, (50, 130, 670, 730), fill=(236, 246, 236), outline=GREEN, width=3)
    t(d, (360, 160), "长在墙上", F_H, GREEN, "mt")
    # room
    d.rectangle((130, 230, 590, 620), outline=WALL, width=6)
    # floor probes: dots + short inward (up) arrows
    for x in (200, 280, 360, 440, 520):
        d.ellipse((x - 8, 592, x + 8, 608), fill=GREEN)
        arrow(d, (x, 585), (x, 520), fill=GREEN, w=3, head=10)
    # left wall
    for y in (300, 380, 460):
        d.ellipse((138, y - 8, 154, y + 8), fill=GREEN)
        arrow(d, (160, y), (230, y), fill=GREEN, w=3, head=10)
    # ceiling light
    d.ellipse((330, 242, 390, 268), fill=LIGHT)
    t(d, (360, 660), "只问墙内侧：那边亮不亮", F_P, GREEN, "mt")

    box(d, (730, 130, 1350, 730), fill=(252, 242, 232), outline=AMBER, width=3)
    t(d, (1040, 160), "铺在空里", F_H, AMBER, "mt")
    d.rectangle((810, 230, 1270, 620), outline=WALL, width=3)
    for i in range(3):
        for j in range(3):
            x = 900 + i * 140
            y = 310 + j * 120
            d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=AMBER)
            for k in range(6):
                ang = k * math.pi / 3
                arrow(d, (x, y), (x + 28 * math.cos(ang), y + 28 * math.sin(ang)),
                      fill=AMBER, w=2, head=7)
    t(d, (1040, 660), "空腔里的点，四面八方都问一遍", F_P, AMBER, "mt")
    save(img, "01-two-paths.png")


def fig2():
    img, d = canvas()
    t(d, (40, 28), "「对不对」其实是三问", F_TITLE)
    t(d, (40, 78), "前一问没过，后一问的好看、更快都不算。", F_P, MUTED)

    rows = [
        (GREEN, "1", "实现有没有跑偏", "和参考算法逐项对上", "对不上，停"),
        (ACCENT, "2", "亮得合不合理", "再和路径追踪比一比", "不合理，换更细的分法"),
        (AMBER, "3", "这一帧跑不跑得起", "数线程、看耗时", "太贵，少做空活"),
    ]
    for i, (color, num, title, body, rule) in enumerate(rows):
        y = 140 + i * 200
        box(d, (70, y, 1330, y + 175), outline=color, width=3)
        d.rectangle((70, y, 88, y + 175), fill=color)
        t(d, (130, y + 28), num + "  " + title, F_H, color)
        t(d, (130, y + 90), body, F_P)
        t(d, (1260, y + 88), rule, F_H, color, "rm")
    save(img, "02-three-layers.png")


def pie(d, cx, cy, r, n, hit=0):
    for i in range(n):
        a0 = -math.pi / 2 + i * 2 * math.pi / n
        a1 = a0 + 2 * math.pi / n
        pts = [(cx, cy)]
        for k in range(12):
            a = a0 + (a1 - a0) * k / 11
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        fill = LIGHT if i == hit else (236, 240, 246)
        d.polygon(pts, fill=fill, outline=WALL)
    d.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), fill=INK)


def fig3():
    img, d = canvas()
    t(d, (40, 28), "分得越粗，小灯越容易「撑满」一块", F_TITLE)
    t(d, (40, 78), "近处方向少、排得密；远处方向多、排得疏。两档不能混着用。", F_P, MUTED)

    box(d, (70, 140, 670, 620))
    t(d, (370, 175), "切成 4 块", F_H, ACCENT, "mt")
    pie(d, 370, 380, 150, 4, hit=1)
    t(d, (370, 570), "一小盏灯 = 整整四分之一天空", F_P, MUTED, "mt")

    box(d, (730, 140, 1330, 620))
    t(d, (1030, 175), "切成 16 块", F_H, AMBER, "mt")
    pie(d, 1030, 380, 150, 16, hit=3)
    t(d, (1030, 570), "同一盏灯只占其中一小块", F_P, MUTED, "mt")

    t(d, (700, 680), "想更细，换一档。不要乘个系数把图调亮。", F_H, RED, "mt")
    save(img, "03-quality-knobs.png")


def fig4():
    img, d = canvas()
    t(d, (40, 28), "空白角落不必算", F_TITLE)
    t(d, (40, 78), "墙被打包进一张大图。没墙的地方仍会启动计算——除非你别点那一块。", F_P, MUTED)

    t(d, (70, 140), "整张都算", F_H)
    box(d, (70, 185, 620, 560), outline=WALL, width=3, r=8)
    d.rectangle((74, 189, 616, 368), fill=FILL_G)
    d.rectangle((74, 372, 250, 556), fill=FILL_G)
    d.rectangle((254, 372, 616, 556), fill=FILL_GRAY)
    t(d, (345, 275), "有墙", F_P, GREEN, "mm")
    t(d, (162, 464), "有墙", F_S, GREEN, "mm")
    t(d, (435, 464), "空白", F_P, MUTED, "mm")
    t(d, (345, 600), "空白也占用计算", F_P, MUTED, "mt")

    d.polygon([(680, 360), (750, 330), (750, 320), (820, 375), (750, 430), (750, 420)], fill=ACCENT)

    t(d, (860, 140), "只算有墙的地方", F_H)
    box(d, (860, 185, 1330, 368), outline=GREEN, width=3, fill=FILL_G, r=8)
    t(d, (1095, 276), "上半张", F_P, GREEN, "mm")
    box(d, (860, 390, 1040, 560), outline=GREEN, width=3, fill=FILL_G, r=8)
    t(d, (950, 475), "左下", F_P, GREEN, "mm")
    t(d, (1185, 475), "右下不算", F_P, MUTED, "mm")
    t(d, (1095, 600), "少做三分之一空活，图不变", F_P, GREEN, "mt")

    t(d, (700, 690), "计时器会抖。数少做了多少，比报「快了几倍」更老实。", F_P, MUTED, "mt")
    save(img, "04-split-dispatch.png")


def fig5():
    img, d = canvas()
    t(d, (40, 28), "先在实验室盒子上对上，再换复杂场景", F_TITLE)
    t(d, (40, 78), "场景一复杂，墙变多、求交变贵。没有前面的对照，慢了说不清为什么。", F_P, MUTED)

    steps = [
        (GREEN, "1", "盒子对上", "实现没跑偏"),
        (ACCENT, "2", "三问分开", "对 / 亮 / 快 不混用"),
        (AMBER, "3", "先摆好墙", "还不动复杂模型"),
        (MUTED, "4", "再换大厅", "带着耗时预算去"),
    ]
    for i, (color, num, title, body) in enumerate(steps):
        x = 50 + i * 335
        box(d, (x, 170, x + 310, 470), outline=color, width=3)
        t(d, (x + 155, 220), num, F_TITLE, color, "mt")
        t(d, (x + 155, 300), title, F_H, color, "mt")
        t(d, (x + 155, 370), body, F_P, INK, "mt")
        if i < 3:
            d.polygon([(x + 318, 310), (x + 334, 320), (x + 318, 330)], fill=WALL)

    t(d, (700, 560), "换场景之前约定好", F_H, INK, "mt")
    notes = [
        "比亮度用线性图，不用调过色的截图",
        "耗时先写预算，超了再优化",
        "用盒子冒充复杂大厅，不算过关",
    ]
    for i, line in enumerate(notes):
        x = 80 + i * 440
        box(d, (x, 600, x + 410, 720), outline=BOX_LINE)
        t(d, (x + 205, 660), line, F_S, INK, "mm")
    save(img, "05-roadmap.png")


if __name__ == "__main__":
    fig0()
    fig1()
    fig2()
    fig3()
    fig4()
    fig5()
