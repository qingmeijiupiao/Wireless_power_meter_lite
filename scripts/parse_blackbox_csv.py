#!/usr/bin/env python3
"""Convert a Web-exported blackbox CSV file into an Excel-friendly HTML report."""

from __future__ import annotations

import argparse
import csv
import html
import re
import sys
from collections import Counter
from datetime import datetime, timedelta, timezone
from pathlib import Path


UINT32_MOD = 1 << 32
ROLLOVER_EDGE_MS = 24 * 60 * 60 * 1000
SYNC_RAW_RE = re.compile(r"\[W\]\[TimeService\] sync raw unix_s=(\d+) unix_us=(\d+)")
BOOT_RE = re.compile(r"system: boot_start\b")
RESET_RE = re.compile(r"^\[Blackbox\]: reset$")
SEVERITY_RE = re.compile(r"^\[([EW])\]\[")

ORIGINAL_COLUMNS = [
    "index", "timestamp_ms", "type", "fragments", "text", "payload_hex",
    "snapshot_version", "flags", "protect", "voltage_mv", "current_ua",
    "meter_mwh", "board_temp_c", "chip_temp_c",
]
REPORT_COLUMNS = [
    "run_id", "wall_time", "uptime_ms", "severity", "type_display", "text",
    "flags_decoded", "protect_decoded", "voltage_mv", "current_ua",
    "meter_mwh", "board_temp_c", "chip_temp_c", "index", "fragments",
    "snapshot_version", "flags", "protect", "type", "payload_hex",
]
REPORT_COLUMN_LABELS = {
    "run_id": "运行编号",
    "wall_time": "网络时间",
    "uptime_ms": "启动后时间(ms)",
    "severity": "级别",
    "type_display": "类型",
    "text": "日志文本",
    "flags_decoded": "全局标志位解析",
    "protect_decoded": "保护状态解析",
    "voltage_mv": "电压(mV)",
    "current_ua": "电流(uA)",
    "meter_mwh": "累计电量(mWh)",
    "board_temp_c": "板载温度(C)",
    "chip_temp_c": "芯片温度(C)",
    "index": "原始索引",
    "fragments": "文本分片数",
    "snapshot_version": "快照版本",
    "flags": "全局标志位原始值",
    "protect": "保护状态原始值",
    "type": "原始类型",
    "payload_hex": "原始载荷(hex)",
}
REPORT_COLUMN_WIDTHS = {
    "run_id": 72,
    "wall_time": 210,
    "uptime_ms": 120,
    "severity": 64,
    "type_display": 100,
    "text": 360,
    "flags_decoded": 280,
    "protect_decoded": 180,
    "voltage_mv": 88,
    "current_ua": 88,
    "meter_mwh": 110,
    "board_temp_c": 100,
    "chip_temp_c": 100,
    "index": 80,
    "fragments": 88,
    "snapshot_version": 80,
    "flags": 120,
    "protect": 120,
    "type": 88,
    "payload_hex": 360,
}
GLOBAL_FLAG_LABELS = {
    "output_enabled": "输出已开启",
    "can_resistor_enabled": "CAN终端电阻已开启",
    "protect_bypassed": "保护已旁路",
    "protect_initialized": "保护已初始化",
    "lp_core_running": "LP Core运行中",
    "lp_ina226_initialized": "LP INA226已初始化",
    "lp_i2c_error": "LP I2C错误",
    "lp_ina226_read_timeout": "LP INA226读取超时",
    "wifi_service_initialized": "WiFi服务已初始化",
    "wifi_enabled": "WiFi已开启",
    "wifi_sta_connected": "WiFi STA已连接",
    "wifi_ap_mode": "WiFi AP模式",
    "wifi_has_saved_sta": "已保存WiFi STA配置",
    "wifi_web_enabled_on_boot": "开机启用WiFi/Web",
    "web_backend_running": "Web后端运行中",
    "screen_initialized": "屏幕已初始化",
    "blackbox_enabled": "黑匣子已开启",
    "reserved": "保留位",
}
PROTECT_CHANNEL_LABELS = {
    "temperature": "过温保护",
    "high_voltage": "过压保护",
    "low_voltage": "欠压保护",
    "current": "过流保护",
}
PROTECT_STATE_LABELS = {
    "normal": "正常",
    "warning": "警告",
    "protect": "保护",
}
TYPE_LABELS = {
    "structured": "结构化快照",
    "string": "文本日志",
    "unknown": "未知类型",
    "invalid": "无效记录",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="将网页导出的黑匣子 CSV 转换为 HTML 表格报告。"
    )
    parser.add_argument("input", type=Path, help="网页导出的 CSV 源文件")
    parser.add_argument("-o", "--output", type=Path, help="输出 HTML 路径")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="用于查找标志位定义的工程根目录（默认：脚本所在工程）",
    )
    parser.add_argument(
        "--timezone-offset",
        type=int,
        default=8,
        help="网络时间的 UTC 时区偏移小时数（默认：8）",
    )
    parser.add_argument(
        "--no-source-flags",
        action="store_true",
        help="不从工程源码解析标志位定义",
    )
    return parser.parse_args()


def read_csv(path: Path) -> tuple[list[dict[str, str]], list[str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if not reader.fieldnames:
            raise ValueError("CSV 缺少表头")
        missing = [column for column in ("index", "timestamp_ms", "type", "text") if column not in reader.fieldnames]
        if missing:
            raise ValueError(f"CSV 缺少必要列：{', '.join(missing)}")
        rows = []
        for source_order, row in enumerate(reader):
            normalized = {column: row.get(column, "") or "" for column in reader.fieldnames}
            normalized["_source_order"] = source_order
            normalized["_uptime_raw"] = parse_int(normalized.get("timestamp_ms"))
            rows.append(normalized)
        return rows, list(reader.fieldnames)


def parse_int(value: str | None) -> int | None:
    if value is None or not value.strip():
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_flag_fields(path: Path, union_name: str, member_name: str) -> list[tuple[str, int, int]]:
    source = path.read_text(encoding="utf-8")
    union = re.search(
        rf"union\s+{re.escape(union_name)}\s*\{{.*?struct\s*\{{(?P<body>.*?)\}}\s*{re.escape(member_name)}\s*;",
        source,
        re.DOTALL,
    )
    if not union:
        raise ValueError(f"未找到 {union_name}.{member_name}")
    fields = []
    offset = 0
    for name, width_text in re.findall(r"\b\w+\s+(\w+)\s*:\s*(\d+)\s*;", union.group("body")):
        width = int(width_text)
        fields.append((name, offset, width))
        offset += width
    if not fields:
        raise ValueError(f"未找到 {union_name}.{member_name} 中的位域")
    return fields


def parse_protect_states(path: Path) -> dict[int, str]:
    source = path.read_text(encoding="utf-8")
    values = {}
    for name, value in re.findall(r"\bPROTECT_STATE_(\w+)\s*=\s*(\d+)", source):
        values[int(value)] = name.lower()
    if not values:
        raise ValueError("未找到 ProtectState_t 枚举值")
    return values


def load_source_definitions(project_root: Path, disabled: bool) -> tuple[list[tuple[str, int, int]], list[tuple[str, int, int]], dict[int, str], list[str]]:
    warnings = []
    if disabled:
        return [], [], {}, ["已通过 --no-source-flags 禁用源码标志位解析。"]
    global_path = project_root / "components/app/global_state/include/global_state.h"
    protect_path = project_root / "components/app/protect/include/protect.h"
    try:
        global_fields = parse_flag_fields(global_path, "GlobalStateFlags", "bits")
    except (OSError, ValueError) as error:
        global_fields = []
        warnings.append(f"GlobalStateFlags 源码解析失败：{error}")
    try:
        protect_fields = parse_flag_fields(protect_path, "protect_states_t", "states_bit")
        protect_states = parse_protect_states(protect_path)
    except (OSError, ValueError) as error:
        protect_fields = []
        protect_states = {}
        warnings.append(f"protect_states_t 源码解析失败：{error}")
    return global_fields, protect_fields, protect_states, warnings


def decode_flags(value_text: str, fields: list[tuple[str, int, int]]) -> str:
    value = parse_int(value_text)
    if value is None or not fields:
        return ""
    decoded = []
    for name, offset, width in fields:
        field_value = (value >> offset) & ((1 << width) - 1)
        label = GLOBAL_FLAG_LABELS.get(name, name)
        decoded.append(f"{label}={field_value}")
    return "\n".join(decoded)


def decode_protect(value_text: str, fields: list[tuple[str, int, int]], states: dict[int, str]) -> str:
    value = parse_int(value_text)
    if value is None or not fields or not states:
        return ""
    decoded = []
    for name, offset, width in fields:
        field_value = (value >> offset) & ((1 << width) - 1)
        channel = name.removesuffix("_protect_state")
        channel_label = PROTECT_CHANNEL_LABELS.get(channel, channel)
        state = states.get(field_value)
        state_label = PROTECT_STATE_LABELS.get(state, f"未知({field_value})")
        decoded.append(f"{channel_label}={state_label}")
    return "，".join(decoded)


def assign_runs(rows: list[dict[str, str]]) -> tuple[list[dict[str, str]], list[str]]:
    warnings = []
    chronological = list(reversed(rows))
    run_id = 0
    rollover_offset = 0
    previous_uptime = None
    for row in chronological:
        uptime = row["_uptime_raw"]
        if uptime is None:
            row["_run_id"] = run_id or 1
            row["_uptime_unwrapped"] = None
            continue
        if previous_uptime is None:
            run_id = 1
        elif uptime < previous_uptime:
            if previous_uptime >= UINT32_MOD - ROLLOVER_EDGE_MS and uptime <= ROLLOVER_EDGE_MS:
                rollover_offset += UINT32_MOD
                warnings.append(f"运行 {run_id}：检测到 uint32 启动后时间回绕。")
            else:
                run_id += 1
                rollover_offset = 0
        row["_run_id"] = run_id
        row["_uptime_unwrapped"] = uptime + rollover_offset
        previous_uptime = uptime
    return chronological, warnings


def enrich_rows(
    chronological: list[dict[str, str]],
    global_fields: list[tuple[str, int, int]],
    protect_fields: list[tuple[str, int, int]],
    protect_states: dict[int, str],
    tz: timezone,
) -> None:
    anchors: dict[int, list[tuple[int, float]]] = {}
    for row in chronological:
        uptime = row["_uptime_unwrapped"]
        match = SYNC_RAW_RE.search(row.get("text", ""))
        if uptime is not None and match:
            wall_epoch = int(match.group(1)) + int(match.group(2)) / 1_000_000
            anchors.setdefault(row["_run_id"], []).append((uptime, wall_epoch))

    for row in chronological:
        text = row.get("text", "")
        match = SEVERITY_RE.match(text)
        row["run_id"] = str(row["_run_id"])
        row["uptime_ms"] = str(row["_uptime_unwrapped"] if row["_uptime_unwrapped"] is not None else "")
        row["severity"] = {"E": "错误", "W": "警告"}.get(match.group(1), "") if match else ""
        row["type_display"] = TYPE_LABELS.get(row.get("type", ""), row.get("type", ""))
        row["flags_decoded"] = decode_flags(row.get("flags", ""), global_fields)
        row["protect_decoded"] = decode_protect(row.get("protect", ""), protect_fields, protect_states)
        row["wall_time"] = ""
        uptime = row["_uptime_unwrapped"]
        run_anchors = anchors.get(row["_run_id"], [])
        if uptime is not None and run_anchors:
            anchor_uptime, anchor_epoch = min(run_anchors, key=lambda item: abs(item[0] - uptime))
            estimated_epoch = anchor_epoch + (uptime - anchor_uptime) / 1000
            wall = datetime.fromtimestamp(estimated_epoch, tz)
            row["wall_time"] = wall.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def summarize_runs(chronological: list[dict[str, str]]) -> list[dict[str, object]]:
    grouped: dict[int, list[dict[str, str]]] = {}
    for row in chronological:
        grouped.setdefault(row["_run_id"], []).append(row)
    summaries = []
    for run_id in sorted(grouped, reverse=True):
        rows = grouped[run_id]
        uptimes = [row["_uptime_unwrapped"] for row in rows if row["_uptime_unwrapped"] is not None]
        wall_times = [row["wall_time"] for row in rows if row["wall_time"]]
        counts = Counter(row["severity"] for row in rows)
        boot_rows = [row for row in rows if BOOT_RE.search(row.get("text", ""))]
        summaries.append({
            "run_id": run_id,
            "records": len(rows),
            "duration_ms": max(uptimes) - min(uptimes) if uptimes else 0,
            "errors": counts["错误"],
            "warnings": counts["警告"],
            "anchored": bool(wall_times),
            "wall_range": f"{wall_times[0]} 至 {wall_times[-1]}" if wall_times else "无时间锚点",
            "boot": boot_rows[-1].get("text", "") if boot_rows else "",
        })
    return summaries


def summarize_overview(chronological: list[dict[str, str]], summaries: list[dict[str, object]]) -> dict[str, object]:
    first_text = chronological[0].get("text", "") if chronological else ""
    return {
        "total_duration_ms": sum(int(summary["duration_ms"]) for summary in summaries),
        "blackbox_reset": bool(RESET_RE.search(first_text)),
    }


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def render_flags_cell(row: dict[str, str]) -> str:
    decoded = row.get("flags_decoded", "")
    if not decoded:
        return "<td></td>"
    lines = decoded.splitlines()
    active = []
    for line in lines:
        _, _, value = line.rpartition("=")
        if value != "0":
            active.append(line)
    raw = row.get("flags", "")
    summary = f"已置位 {len(active)} 项 · 原始值 {raw}"
    details = "".join(f"<span>{esc(line)}</span>" for line in lines)
    return (
        '<td class="flags-cell"><details class="flags-details">'
        f"<summary>{esc(summary)}</summary>"
        f'<div class="flags-grid">{details}</div>'
        "</details></td>"
    )


def render_report_cells(row: dict[str, str]) -> str:
    cells = []
    for column in REPORT_COLUMNS:
        if column == "flags_decoded":
            cells.append(render_flags_cell(row))
        else:
            cells.append(f"<td>{esc(row.get(column, ''))}</td>")
    return "".join(cells)


def render_html(rows: list[dict[str, str]], summaries: list[dict[str, object]], overview: dict[str, object], warnings: list[str], source_name: str, timezone_label: str) -> str:
    run_options = "".join(f'<option value="{summary["run_id"]}">运行 {summary["run_id"]}</option>' for summary in summaries)
    warning_html = "".join(f"<li>{esc(warning)}</li>" for warning in warnings) or "<li>无</li>"
    summary_rows = []
    for summary in summaries:
        summary_rows.append(
            "<tr>"
            f'<td>运行 {summary["run_id"]}</td><td>{summary["records"]}</td>'
            f'<td>{summary["duration_ms"] / 1000:.3f} 秒</td>'
            f'<td>{summary["errors"]}</td><td>{summary["warnings"]}</td>'
            f'<td>{"是" if summary["anchored"] else "否"}</td>'
            f'<td>{esc(summary["wall_range"])}</td>'
            f'<td>{esc(summary["boot"])}</td></tr>'
        )
    table_rows = []
    for row in reversed(rows):
        severity = {"错误": "error", "警告": "warn"}.get(row["severity"], "")
        cells = render_report_cells(row)
        searchable = " ".join(str(row.get(column, "")) for column in REPORT_COLUMNS).lower()
        table_rows.append(
            f'<tr class="{severity}" data-run="{esc(row["run_id"])}" '
            f'data-severity="{esc(row["severity"])}" data-search="{esc(searchable)}">{cells}</tr>'
        )
    colgroup = "".join(f'<col style="width:{REPORT_COLUMN_WIDTHS.get(column, 120)}px">' for column in REPORT_COLUMNS)
    headers = "".join(
        f'<th>{esc(REPORT_COLUMN_LABELS.get(column, column))}<span class="resizer" title="拖拽调整列宽"></span></th>'
        for column in REPORT_COLUMNS
    )
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>黑匣子日志报告</title>
<style>
body{{font-family:Arial,"Microsoft YaHei",sans-serif;margin:16px;color:#1f2937}}
h1,h2{{margin:12px 0 8px}} .meta{{color:#475569}} .controls{{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0}}
input,select{{padding:6px 8px}} table{{border-collapse:collapse;width:100%;font-size:12px}}
th,td{{border:1px solid #cbd5e1;padding:4px 6px;text-align:left;vertical-align:top;white-space:pre-wrap;word-break:break-word}}
th{{background:#e2e8f0;position:sticky;top:0}} tr.error td{{background:#fecaca}} tr.warn td{{background:#fef3c7}}
.summary{{margin-bottom:14px}} .summary th{{position:static}} .hidden{{display:none}}
.table-wrap{{max-width:100%;overflow-x:auto}} #log-table{{table-layout:fixed;width:max-content;min-width:100%}}
#log-table th{{position:sticky}} #log-table th .resizer{{position:absolute;top:0;right:-3px;width:7px;height:100%;cursor:col-resize;user-select:none}}
#log-table th .resizer:hover,#log-table th .resizer.active{{background:#64748b}}
.flags-details summary{{cursor:pointer;white-space:nowrap}} .flags-grid{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:2px 10px;margin-top:4px;white-space:normal}}
.flags-grid span{{overflow-wrap:anywhere}} button{{padding:6px 8px;cursor:pointer}}
</style>
</head>
<body>
<h1>黑匣子日志报告</h1>
<div class="meta">源文件：{esc(source_name)} | 记录数：{len(rows)} | 运行次数：{len(summaries)} | 黑匣子总记录运行时长：{overview["total_duration_ms"] / 1000:.3f} 秒 | 黑匣子已重置：{"是" if overview["blackbox_reset"] else "否"} | 网络时间时区：{esc(timezone_label)}</div>
<h2>运行摘要</h2>
<table class="summary"><thead><tr><th>运行</th><th>记录数</th><th>持续时间</th><th>错误数</th><th>警告数</th><th>包含网络时间</th><th>网络时间段</th><th>启动标记</th></tr></thead>
<tbody>{''.join(summary_rows)}</tbody></table>
<h2>解析提示</h2><ul>{warning_html}</ul>
<div class="controls">
<input id="search" placeholder="筛选文本">
<select id="run"><option value="">全部运行</option>{run_options}</select>
<select id="severity"><option value="">全部级别</option><option>错误</option><option>警告</option></select>
<button id="expand-flags" type="button">展开全部标志位</button>
<button id="collapse-flags" type="button">收起全部标志位</button>
<span id="visible"></span>
</div>
<div class="table-wrap"><table id="log-table"><colgroup>{colgroup}</colgroup><thead><tr>{headers}</tr></thead><tbody id="logs">{''.join(table_rows)}</tbody></table></div>
<script>
const rows=[...document.querySelectorAll('#logs tr')];
function applyFilter(){{
  const search=document.querySelector('#search').value.toLowerCase();
  const run=document.querySelector('#run').value;
  const severity=document.querySelector('#severity').value;
  let visible=0;
  rows.forEach(row=>{{
    const show=(!search||row.dataset.search.includes(search))&&(!run||row.dataset.run===run)&&(!severity||row.dataset.severity===severity);
    row.classList.toggle('hidden',!show); if(show) visible++;
  }});
  document.querySelector('#visible').textContent=`显示：${{visible}} / ${{rows.length}}`;
}}
document.querySelectorAll('input,select').forEach(node=>node.addEventListener('input',applyFilter));
document.querySelector('#expand-flags').addEventListener('click',()=>document.querySelectorAll('.flags-details').forEach(node=>node.open=true));
document.querySelector('#collapse-flags').addEventListener('click',()=>document.querySelectorAll('.flags-details').forEach(node=>node.open=false));
const columns=[...document.querySelectorAll('#log-table col')];
document.querySelectorAll('#log-table .resizer').forEach((resizer,index)=>{{
  resizer.addEventListener('mousedown',event=>{{
    event.preventDefault();
    const startX=event.clientX;
    const startWidth=columns[index].getBoundingClientRect().width;
    resizer.classList.add('active');
    const move=moveEvent=>{{
      columns[index].style.width=`${{Math.max(48,startWidth+moveEvent.clientX-startX)}}px`;
    }};
    const stop=()=>{{
      resizer.classList.remove('active');
      document.removeEventListener('mousemove',move);
      document.removeEventListener('mouseup',stop);
    }};
    document.addEventListener('mousemove',move);
    document.addEventListener('mouseup',stop);
  }});
}});
applyFilter();
</script>
</body></html>
"""


def main() -> int:
    args = parse_args()
    output = args.output or args.input.with_suffix(".html")
    try:
        rows, csv_columns = read_csv(args.input)
        global_fields, protect_fields, protect_states, warnings = load_source_definitions(
            args.project_root, args.no_source_flags
        )
        chronological, run_warnings = assign_runs(rows)
        warnings.extend(run_warnings)
        unknown_columns = [column for column in csv_columns if column not in ORIGINAL_COLUMNS]
        if unknown_columns:
            warnings.append(f"CSV 包含报告中未显示的额外列：{', '.join(unknown_columns)}")
        tz = timezone(timedelta(hours=args.timezone_offset))
        enrich_rows(chronological, global_fields, protect_fields, protect_states, tz)
        summaries = summarize_runs(chronological)
        overview = summarize_overview(chronological, summaries)
        sign = "+" if args.timezone_offset >= 0 else "-"
        timezone_label = f"UTC{sign}{abs(args.timezone_offset):02d}:00"
        report = render_html(chronological, summaries, overview, warnings, args.input.name, timezone_label)
        output.write_text("\ufeff" + report, encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    print(f"已生成：{output}")
    print(f"记录数：{len(rows)}，运行次数：{len(summaries)}，解析提示：{len(warnings)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
