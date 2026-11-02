#!/usr/bin/env python3
"""从 docs/icon.svg 生成 src/SideDeck.ico 与 docs/icon.png。

Chrome/Edge 无头模式只渲染一次 512px（部分尺寸会整片透明，不可靠），
其余尺寸用面积平均重采样得到；ICO 里 64 及以下写 BMP 帧、128/256 写 PNG 帧，
Windows 外壳、安装器与旧图标接口都能读。

用法：python tools/make-icon.py [--chrome PATH]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, 'docs', 'icon.svg')
ICO = os.path.join(ROOT, 'src', 'SideDeck.ico')
PNG_OUT = os.path.join(ROOT, 'docs', 'icon.png')

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SOURCE = 512
BMP_MAX = 64

CHROME_CANDIDATES = [
    r'C:\Program Files\Google\Chrome\Application\chrome.exe',
    r'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
    r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
    r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
]


def find_browser(explicit):
    for path in ([explicit] if explicit else []) + CHROME_CANDIDATES:
        if path and os.path.isfile(path):
            return path
    sys.exit('找不到 Chrome/Edge，可用 --chrome 指定路径')


def render(browser, size, out_path):
    # 直接截图 SVG 会被 body 的默认 margin 和内联基线间隙推偏，四周留白不再均等；
    # 内联进一个清零边距的页面里渲染，图形才真正居中。
    with open(SVG, 'r', encoding='utf-8') as fh:
        markup = fh.read()
    page = out_path + '.html'
    with open(page, 'w', encoding='utf-8') as fh:
        fh.write('<!doctype html><html><head><meta charset="utf-8"><style>'
                 'html,body{margin:0;padding:0;background:transparent;overflow:hidden}'
                 'svg{display:block;width:100vw;height:100vh}'
                 '</style></head><body>' + markup + '</body></html>')
    url = 'file:///' + page.replace('\\', '/')
    subprocess.run([
        browser, '--headless=new', '--disable-gpu', '--hide-scrollbars',
        '--force-device-scale-factor=1', '--default-background-color=00000000',
        f'--window-size={size},{size}', '--virtual-time-budget=800',
        f'--screenshot={out_path}', url,
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def decode_png(data):
    """极简 PNG 解码：8 位 RGBA、非隔行，够处理浏览器导出的截图。"""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('不是 PNG')
    pos, idat, width, height = 8, b'', 0, 0
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or color != 6 or interlace != 0:
                raise ValueError('仅支持 8 位 RGBA 非隔行')
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray(stride * height)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filt == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, bytes(out)


def encode_png(width, height, rgba):
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw += b'\x00' + rgba[y * stride:(y + 1) * stride]

    def chunk(kind, body):
        return (struct.pack('>I', len(body)) + kind + body
                + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
            + chunk(b'IEND', b''))


def resample(src, size, target):
    """面积平均（box）重采样，任意比例；对图标这种大色块足够干净。"""
    scale = size / target
    out = bytearray(target * target * 4)
    for ty in range(target):
        y0, y1 = ty * scale, (ty + 1) * scale
        for tx in range(target):
            x0, x1 = tx * scale, (tx + 1) * scale
            acc = [0.0, 0.0, 0.0, 0.0]
            weight = 0.0
            for sy in range(int(y0), min(int(y1) + 1, size)):
                wy = min(y1, sy + 1) - max(y0, sy)
                if wy <= 0:
                    continue
                row = sy * size * 4
                for sx in range(int(x0), min(int(x1) + 1, size)):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    if wx <= 0:
                        continue
                    w = wx * wy
                    i = row + sx * 4
                    acc[0] += src[i] * w
                    acc[1] += src[i + 1] * w
                    acc[2] += src[i + 2] * w
                    acc[3] += src[i + 3] * w
                    weight += w
            o = (ty * target + tx) * 4
            out[o + 0] = min(255, int(acc[0] / weight + 0.5))
            out[o + 1] = min(255, int(acc[1] / weight + 0.5))
            out[o + 2] = min(255, int(acc[2] / weight + 0.5))
            out[o + 3] = min(255, int(acc[3] / weight + 0.5))
    return bytes(out)


def bmp_frame(width, height, rgba):
    """ICO 里的 BMP 帧：BITMAPINFOHEADER + 自下而上的 BGRA + AND 掩码。"""
    header = struct.pack('<IiiHHIIiiII', 40, width, height * 2, 1, 32, 0,
                         width * height * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(height - 1, -1, -1):
        row = rgba[y * width * 4:(y + 1) * width * 4]
        for x in range(width):
            r, g, b, a = row[x * 4:x * 4 + 4]
            body += bytes((b, g, r, a))
    mask_stride = ((width + 31) // 32) * 4
    body += b'\x00' * (mask_stride * height)
    return header + bytes(body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--chrome', default=os.environ.get('SIDEDECK_CHROME'))
    args = parser.parse_args()
    browser = find_browser(args.chrome)

    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'source.png')
        source = None
        data = b''
        for attempt in range(4):
            render(browser, SOURCE, path)
            with open(path, 'rb') as fh:
                data = fh.read()
            width, height, rgba = decode_png(data)
            if (width, height) != (SOURCE, SOURCE):
                sys.exit(f'渲染尺寸不符：{width}x{height}')
            opaque = sum(1 for i in range(3, len(rgba), 4) if rgba[i] > 8)
            if opaque >= SOURCE * SOURCE * 0.15:
                source = rgba
                break
            if attempt == 3:
                sys.exit('渲染结果为空白，重试 4 次仍失败')
        with open(PNG_OUT, 'wb') as out:
            out.write(data)

    frames = []
    for size in SIZES:
        rgba = resample(source, SOURCE, size)
        payload = bmp_frame(size, size, rgba) if size <= BMP_MAX else encode_png(size, size, rgba)
        frames.append((size, payload))

    count = len(frames)
    offset = 6 + 16 * count
    directory = bytearray(struct.pack('<HHH', 0, 1, count))
    for size, payload in frames:
        directory += struct.pack('<BBBBHHII', size if size < 256 else 0,
                                 size if size < 256 else 0, 0, 0, 1, 32,
                                 len(payload), offset)
        offset += len(payload)
    with open(ICO, 'wb') as fh:
        fh.write(bytes(directory))
        for _, payload in frames:
            fh.write(payload)
    print(f'SideDeck.ico: {count} 帧 {"/".join(str(s) for s, _ in frames)}  ({os.path.getsize(ICO)} 字节)')
    print(f'docs/icon.png: {SOURCE}px 预览')


if __name__ == '__main__':
    main()
