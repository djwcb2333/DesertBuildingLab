"""Build the offline public manual using only the Python standard library.

Run from any directory: python Tools/build_manual.py
The Markdown documents remain the editable source; no external renderer or CDN.
"""

from __future__ import annotations

import html
import json
import os
from pathlib import Path
import re
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "Docs" / "Manual.html"
CHAPTERS = [
    ("overview", "项目介绍与公开版本", "README.md"),
    ("guide", "操作指南", "Docs/GettingStarted.md"),
    ("logic", "生成逻辑与代码结构", "Docs/GenerationLogic.md"),
    ("art", "模型、材质与替换规范", "Docs/AssetIntegration.md"),
    ("assets", "保存与项目管理", "Docs/AssetOrganization.md"),
    ("sources", "美术来源与发布范围", "Docs/美术来源与发布范围.md"),
    ("sourceart", "可编辑源资源", "SourceArt/README.md"),
    ("assetlicense", "美术资源许可", "ASSET_LICENSE.md"),
    ("license", "MIT License", "LICENSE"),
]


def slug(text: str) -> str:
    """GitHub-like heading aliases for the headings used in this repository."""
    text = re.sub(r"[`*_]", "", text).strip().lower()
    text = re.sub(r"[^\w\s\-\u4e00-\u9fff]", "", text)
    return re.sub(r"\s+", "-", text)


TEXTS = {name: (ROOT / name).read_text(encoding="utf-8") for _, _, name in CHAPTERS}
SECTION_IDS = {name: "chapter-" + key for key, _, name in CHAPTERS}
HEADINGS: dict[str, dict[str, str]] = {}
for key, _, name in CHAPTERS:
    aliases: dict[str, str] = {}
    duplicates: dict[str, int] = {}
    fenced = False
    for line in TEXTS[name].splitlines():
        if line.startswith("```"):
            fenced = not fenced
        match = re.match(r"^(#{1,6})\s+(.+)$", line) if not fenced else None
        if match:
            base = slug(match.group(2))
            n = duplicates.get(base, 0)
            duplicates[base] = n + 1
            alias = base if n == 0 else f"{base}-{n}"
            aliases[alias] = f"{key}--{alias}"
    HEADINGS[name] = aliases


def relative_url(target: Path) -> str:
    return Path(os.path.relpath(target, OUTPUT.parent)).as_posix()


def link_url(url: str, source: str, image: bool = False) -> str:
    url = url.strip().removeprefix("<").removesuffix(">")
    parsed = urlsplit(url)
    if parsed.scheme in ("http", "https", "mailto"):
        if image:
            raise ValueError("Manual images must be repository-local")
        return url
    if parsed.scheme or re.match(r"^[A-Za-z]:", url) or url.startswith(("/", "\\")):
        raise ValueError("Absolute or non-public link in manual input")
    path = (ROOT / source).parent / unquote(parsed.path) if parsed.path else ROOT / source
    path = path.resolve()
    try:
        repo_name = path.relative_to(ROOT).as_posix()
    except ValueError as exc:
        raise ValueError("Link escapes public repository") from exc
    fragment = unquote(parsed.fragment)
    if not image and repo_name in SECTION_IDS:
        if fragment:
            if fragment not in HEADINGS[repo_name]:
                raise ValueError(f"Unknown Markdown heading: {repo_name}#{fragment}")
            return "#" + HEADINGS[repo_name][fragment]
        return "#" + SECTION_IDS[repo_name]
    if path == OUTPUT:
        return "#top"
    return relative_url(path) + ("#" + fragment if fragment else "")


def inline(text: str, source: str) -> str:
    stored: dict[str, str] = {}

    def keep(value: str) -> str:
        token = f"\x00{len(stored)}\x00"
        stored[token] = value
        return token

    text = re.sub(r"`([^`]+)`", lambda m: keep("<code>" + html.escape(m[1]) + "</code>"), text)

    def render_link(match: re.Match[str]) -> str:
        image, label, url = match.groups()
        target = html.escape(link_url(url, source, bool(image)), quote=True)
        if image:
            return keep(f'<img src="{target}" alt="{html.escape(label, quote=True)}" loading="lazy">')
        external = ' rel="noopener noreferrer"' if urlsplit(url).scheme in ("http", "https") else ""
        return keep(f'<a href="{target}"{external}>{html.escape(label)}</a>')

    text = re.sub(r"(!?)\[([^\]]+)\]\(([^)]+)\)", render_link, text)
    text = html.escape(text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
    for token, value in stored.items():
        text = text.replace(token, value)
    return text


def split_row(line: str) -> list[str]:
    return [part.strip() for part in line.strip().strip("|").split("|")]


def flow_svg(code: str) -> str:
    """Render this manual's small acyclic Mermaid flowchart as an offline SVG."""
    labels: dict[str, str] = {}
    edges: list[tuple[str, str]] = []
    for start, label, end, endlabel in re.findall(
        r"(\w+)(?:\[([^\]]+)\])?\s*-->\s*(\w+)(?:\[([^\]]+)\])?", code
    ):
        if label:
            labels[start] = label
        if endlabel:
            labels[end] = endlabel
        edges.append((start, end))
    if not labels or not edges:
        return '<pre><code>' + html.escape(code) + '</code></pre>'
    levels = {node: 0 for node in labels}
    for _ in range(len(labels)):
        changed = False
        for start, end in edges:
            proposed = levels[start] + 1
            if levels[end] < proposed:
                levels[end] = proposed
                changed = True
        if not changed:
            break
    columns: dict[int, list[str]] = {}
    for node in labels:
        columns.setdefault(levels[node], []).append(node)
    width = (max(columns) + 1) * 168 + 24
    height = max(len(nodes) for nodes in columns.values()) * 94 + 30
    points: dict[str, tuple[float, float]] = {}
    for column, nodes in columns.items():
        for index, node in enumerate(nodes):
            points[node] = (24 + column * 168, (height - len(nodes) * 94) / 2 + index * 94 + 22)
    out = [f'<div class="diagram"><svg viewBox="0 0 {width} {height}" role="img" aria-label="制作输入经过承托、占用与通路求解，共同用于预览、确认、实例输出和保存">',
           '<defs><marker id="flow-arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 Z" fill="#926a46"/></marker></defs>']
    for start, end in edges:
        x1, y1 = points[start]
        x2, y2 = points[end]
        middle = (x1 + 140 + x2) / 2
        out.append(f'<path d="M{x1+140},{y1+25} C{middle},{y1+25} {middle},{y2+25} {x2},{y2+25}" fill="none" stroke="#926a46" stroke-width="2" marker-end="url(#flow-arrow)"/>')
    for node, (x, y) in points.items():
        out.append(f'<rect x="{x}" y="{y}" width="140" height="50" rx="8" fill="#f0e2c9" stroke="#bb926b"/>')
        out.append(f'<text x="{x+70}" y="{y+29}" text-anchor="middle" fill="#483a2e" font-size="12">{html.escape(labels[node])}</text>')
    out.append('</svg><p class="caption">输入与规则共用；预览、保存和关卡实例由同一套制作数据派生。</p></div>')
    return "".join(out)


def render_markdown(source: str, key: str) -> str:
    lines = TEXTS[source].splitlines()
    out: list[str] = []
    i = 0
    heading_seen: dict[str, int] = {}
    while i < len(lines):
        line = lines[i].strip()
        if not line or line.startswith("<!--"):
            i += 1
            continue
        if line.startswith("```"):
            language = line[3:].strip()
            i += 1
            code: list[str] = []
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            text = "\n".join(code)
            out.append(flow_svg(text) if language == "mermaid" else '<pre><code>' + html.escape(text) + '</code></pre>')
            i += 1
            continue
        heading = re.match(r"^(#{1,6})\s+(.+)$", line)
        if heading:
            level = min(len(heading[1]) + 1, 6)
            base = slug(heading[2])
            n = heading_seen.get(base, 0)
            heading_seen[base] = n + 1
            alias = base if not n else f"{base}-{n}"
            out.append(f'<h{level} id="{HEADINGS[source][alias]}">{inline(heading[2], source)}</h{level}>')
            i += 1
            continue
        if line.startswith("|") and i + 1 < len(lines) and re.match(r"^\s*\|?\s*:?-{3,}", lines[i+1]):
            header = split_row(line)
            i += 2
            rows: list[list[str]] = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(split_row(lines[i]))
                i += 1
            out.append('<div class="table-wrap"><table><thead><tr>' + ''.join('<th>' + inline(cell, source) + '</th>' for cell in header) + '</tr></thead><tbody>')
            for row in rows:
                out.append('<tr>' + ''.join('<td>' + inline(cell, source) + '</td>' for cell in row) + '</tr>')
            out.append('</tbody></table></div>')
            continue
        marker = re.match(r"^(?:([-*])|\d+\.)\s+(.+)$", line)
        if marker:
            kind = "ul" if marker[1] else "ol"
            out.append(f'<{kind}>')
            while i < len(lines):
                item = re.match(r"^(?:([-*])|\d+\.)\s+(.+)$", lines[i].strip())
                if not item or ("ul" if item[1] else "ol") != kind:
                    break
                content = item[2]
                task = re.match(r"^\[([ xX])\]\s+(.+)$", content)
                if task:
                    symbol = "☑" if task[1].strip() else "☐"
                    content = symbol + " " + task[2]
                out.append('<li>' + inline(content, source) + '</li>')
                i += 1
            out.append(f'</{kind}>')
            continue
        if line.startswith(">"):
            out.append('<blockquote>' + inline(line[1:].strip(), source) + '</blockquote>')
            i += 1
            continue
        paragraph = [line]
        i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r"^(#{1,6}\s|```|\||[-*]\s|\d+\.\s|>)", lines[i].strip()):
            paragraph.append(lines[i].strip())
            i += 1
        out.append('<p>' + inline(" ".join(paragraph), source) + '</p>')
    return "\n".join(out)


CSS = """
:root{color-scheme:light;--ink:#302a24;--muted:#786b5e;--paper:#fffaf0;--edge:#e2d5bf;--red:#963e31;--sand:#e9d4ad}
*{box-sizing:border-box}html{scroll-behavior:auto;scroll-padding-top:24px}body{margin:0;background:#efe5d3;color:var(--ink);font-family:system-ui,-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;line-height:1.8;overflow-wrap:anywhere}
a{color:var(--red);text-underline-offset:3px}a:hover{color:#552218}img{display:block;width:100%;max-width:100%;height:auto;border-radius:12px;margin:20px 0;background:#c7b796}main{min-width:0;max-width:1050px;margin:auto;padding:36px 48px 80px;background:var(--paper)}
.layout{display:grid;grid-template-columns:252px minmax(0,1fr);max-width:1350px;margin:auto}.sidebar{background:#44372a;color:#f3e5cb;padding:28px 22px;position:sticky;top:0;height:100vh;overflow:auto;min-width:0}.sidebar h2{font-size:20px;line-height:1.5;margin:0 0 8px;color:#f3e5cb}.sidebar p{font-size:13px;color:#d0bda0;margin:0 0 20px}.sidebar a{display:block;padding:8px 0;color:#f1dfbe;text-decoration:none;font-size:14px}.sidebar a:hover{color:#fff}
.eyebrow{font-size:12px;font-weight:700;letter-spacing:.14em;color:var(--red)}h1{font-size:clamp(28px,4vw,43px);line-height:1.3;margin:10px 0 16px}h2{font-size:28px;line-height:1.4;margin:18px 0;color:#493626}h3{font-size:21px;margin:30px 0 12px;color:#74513a}h4{font-size:18px;margin:26px 0 10px}.lead{font-size:18px;color:#6b5743}.notice{padding:17px 20px;background:#efe1c6;border-left:4px solid #a27445;border-radius:0 10px 10px 0;margin:22px 0}.notice p{margin:0}.chapter{padding-top:36px;margin-top:36px;border-top:1px solid var(--edge)}.chapter-number{font-size:12px;font-weight:700;letter-spacing:.12em;color:#9c7351}.caption{font-size:13px;color:var(--muted);margin:10px 0 20px}.chapter-source{font-size:12px;color:var(--muted)}
p{margin:13px 0}ul,ol{padding-left:25px}li{padding-left:4px;margin:8px 0}code{font-family:Consolas,"Cascadia Code",monospace;background:#f0e7d7;padding:2px 5px;border-radius:4px;font-size:.89em;word-break:break-word}pre{max-width:100%;overflow:auto;white-space:pre;background:#f1e8d9;border:1px solid var(--edge);border-radius:10px;padding:18px;font-size:13px;line-height:1.6}pre code{padding:0;background:none;white-space:inherit;word-break:normal}.table-wrap{max-width:100%;overflow-x:auto;margin:20px 0;border:1px solid var(--edge);border-radius:10px}table{border-collapse:collapse;width:100%;table-layout:fixed;font-size:14px}td,th{padding:12px 14px;border-bottom:1px solid var(--edge);vertical-align:top;overflow-wrap:anywhere;text-align:left}th{background:#eee1c8;font-weight:700}tr:last-child td{border-bottom:0}blockquote{border-left:3px solid #bb9673;padding:8px 18px;margin:20px 0;background:#f3eadb}.diagram{padding:14px 12px;border:1px solid var(--edge);border-radius:12px;margin:24px 0;background:#faf1df}.diagram svg{display:block;width:100%;max-width:100%;height:auto}.footer{font-size:13px;color:var(--muted);padding-top:28px;border-top:1px solid var(--edge);margin-top:40px}
@media(max-width:900px){.layout{grid-template-columns:minmax(0,1fr)}.sidebar{position:static;height:auto;padding:22px}.sidebar nav{display:flex;gap:8px 18px;flex-wrap:wrap}.sidebar a{padding:4px 0}main{padding:26px 24px 55px;width:100%;max-width:none}h2{font-size:25px}}
@media(max-width:480px){main{padding:22px 16px 44px}table{font-size:12px}td,th{padding:10px 8px}.lead{font-size:16px}pre{padding:12px;font-size:12px}.notice{padding:13px 14px}}
@media print{.layout{display:block}.sidebar{display:none}main{max-width:none;padding:0;background:white}.chapter{break-before:page}pre{white-space:pre-wrap}a{color:inherit}.table-wrap{overflow:visible}img{max-height:21cm;object-fit:contain}}
"""


class InspectHTML(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: list[str] = []
        self.links: list[str] = []
        self.images: list[str] = []
        self.network_resources: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        data = dict(attrs)
        if data.get("id"):
            self.ids.append(data["id"])
        if tag == "a" and data.get("href"):
            self.links.append(data["href"])
        if tag == "img" and data.get("src"):
            self.images.append(data["src"])
        if tag in ("script", "iframe", "link", "video", "audio", "source"):
            for attribute in ("src", "href"):
                if data.get(attribute):
                    self.network_resources.append(data[attribute])


def validate(document: str) -> dict[str, object]:
    parser = InspectHTML()
    parser.feed(document)
    errors: list[str] = []
    if len(set(parser.ids)) != len(parser.ids):
        errors.append("Duplicate HTML IDs")
    local_links = 0
    external_links = 0
    source_links = 0
    for url in parser.links + parser.images:
        parsed = urlsplit(url)
        if parsed.scheme in ("http", "https", "mailto"):
            external_links += 1
            continue
        if parsed.scheme or url.startswith(("/", "\\")):
            errors.append("Non-relative local link")
            continue
        if parsed.path:
            target = (OUTPUT.parent / unquote(parsed.path)).resolve()
            try:
                target.relative_to(ROOT)
            except ValueError:
                errors.append("Link escapes repository")
            if not target.exists():
                errors.append("Missing link: " + parsed.path)
            if "/Source/" in target.as_posix():
                source_links += 1
            local_links += 1
        elif parsed.fragment and unquote(parsed.fragment) not in parser.ids:
            errors.append("Missing HTML anchor: " + parsed.fragment)
    if parser.network_resources:
        errors.append("Unexpected resource loading")
    if re.search(r"(?<![A-Za-z0-9_])[A-Za-z]:[\\/]|file://|/Users/|\\Users\\", document):
        errors.append("Machine-absolute path")
    if "@import" in CSS or re.search(r"url\s*\(", CSS):
        errors.append("External CSS resource")
    # Structural overflow guard only: actual viewport measurements require a browser.
    required_guards = ["grid-template-columns:252px minmax(0,1fr)", "min-width:0", "max-width:100%", "overflow-x:auto", "@media(max-width:480px)"]
    if any(rule not in CSS for rule in required_guards):
        errors.append("Missing narrow-layout overflow guard")
    if errors:
        raise ValueError("; ".join(errors))
    return {"success": True, "output": "Docs/Manual.html", "chapters": len(CHAPTERS), "anchors": len(parser.ids),
            "internal_anchor_links": sum(link.startswith("#") for link in parser.links), "relative_file_links": local_links,
            "source_links": source_links, "images": len(parser.images), "external_reference_links": external_links,
            "network_resource_loads": 0, "machine_absolute_paths": 0,
            "overflow_check": "Static CSS guards passed; browser viewport overflow is not established by this check."}


def main() -> None:
    navigation = "".join(f'<a href="#chapter-{key}">{index:02d} · {html.escape(title)}</a>' for index, (key, title, _) in enumerate(CHAPTERS, 1))
    sections: list[str] = []
    for index, (key, title, source) in enumerate(CHAPTERS, 1):
        content = '<pre class="license"><code>' + html.escape(TEXTS[source]) + '</code></pre>' if source == "LICENSE" else render_markdown(source, key)
        sections.append(f'<section class="chapter" id="chapter-{key}"><div class="chapter-number">CHAPTER {index:02d}</div>'
                        f'<p class="chapter-source">可编辑源：<a href="{html.escape(relative_url(ROOT / source), quote=True)}">{html.escape(source)}</a></p>{content}</section>')
    document = f'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="Desert Building Lab 公开插件离线操作、生成逻辑与美术接入手册"><title>沙漠建筑实验室 · 公开插件手册</title><style>{CSS}</style></head>
<body><div class="layout"><aside class="sidebar"><h2>沙漠建筑实验室</h2><p>Desert Building Lab<br>0.4.7-assets.1 · UE 5.8.2</p><nav aria-label="手册目录">{navigation}</nav><p style="margin-top:24px">本地阅读无需联网。图片及源码链接需要与仓库一起保留。</p></aside>
<main><div id="top" class="eyebrow">PUBLIC EDITION · OFFLINE MANUAL</div><h1>从模块到可编辑建筑</h1><p class="lead">介绍、安装、操作、规则与美术接口，整合为一份可随仓库阅读的中文手册。</p>
<div class="notice"><p><strong>图像与验证边界：</strong>本页建筑图片为公开套件在 Unreal 中实际生成、由 SceneCapture 输出的渲染，属于模型效果展示，不是插件 UI 操作截图。公开样例 204/204 项准备检查与原开发配置 264/264 项保存流程检查分别说明；新项目、真实角色／导航、游戏打包与性能仍需单独验收。</p></div>
{''.join(sections)}<div class="footer">手册由 <a href="../Tools/build_manual.py">Tools/build_manual.py</a> 从仓库内 Markdown 与许可生成。修改正文后重新生成。保留 Docs/Images、Source、Tools、Examples、SourceArt 和许可文件，离线链接才完整。<a href="#top">返回顶部</a></div></main></div></body></html>'''
    report = validate(document)
    OUTPUT.write_text(document, encoding="utf-8", newline="\n")
    report["bytes"] = OUTPUT.stat().st_size
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
