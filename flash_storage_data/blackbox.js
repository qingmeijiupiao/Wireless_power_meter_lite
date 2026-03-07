(function () {
    // DOM 元素
    const countDisplay = document.getElementById('logCountDisplay');
    const fetchCountBtn = document.getElementById('fetchCountBtn');
    const startInput = document.getElementById('startIndex');
    const endInput = document.getElementById('endIndex');
    const fetchRangeBtn = document.getElementById('fetchRangeBtn');
    const tableBody = document.getElementById('tableBody');
    const statusMsg = document.getElementById('statusMsg');
    const exportBtn = document.getElementById('exportCsvBtn');

    // 存储当前显示的日志数据 (用于导出)
    let currentLogs = [];                // 原始数据对象数组
    let currentStart = 0;                // 本次拉取的起始索引
    let currentEnd = -1;                 // 结束索引

    // 辅助函数：更新状态 (普通信息/错误/加载)
    function setStatus(text, isError = false, isLoading = false) {
        statusMsg.innerHTML = text;
        if (isError) {
            statusMsg.style.background = '#fee2e2';
            statusMsg.style.color = '#b91c1c';
            statusMsg.style.border = '1px solid #fecaca';
        } else {
            statusMsg.style.background = '#ffffff';
            statusMsg.style.color = '#64748b';
            statusMsg.style.border = '1px dashed #cbd5e1';
        }
        if (isLoading) {
            statusMsg.innerHTML = `<span class="spinner" style="border-top-color:#3b82f6;"></span> 加载中...`;
        }
    }

    // 清除表格并显示占位符
    function showEmptyTable(message = '暂无数据') {
        tableBody.innerHTML = `<tr><td colspan="8" style="text-align:center; padding:48px; color:#94a3b8;">${message}</td></tr>`;
        currentLogs = [];
        currentStart = 0;
        currentEnd = -1;
    }

    // 获取日志总数
    async function fetchLogCount() {
        setStatus('正在获取总数...', false, true);
        try {
            const response = await fetch('/api/log/count');
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const data = await response.json();
            // 假设返回格式为 {"count":1234} 或直接数字
            const count = (typeof data === 'object' && data !== null) ? data.count : data;
            if (typeof count !== 'number') throw new Error('返回格式异常');
            countDisplay.innerText = count;
            setStatus(`总数已更新: ${count} 条`, false, false);
            // 调整endInput的最大值提醒 (但不强制)
            endInput.max = Math.max(0, count - 1);
            startInput.max = Math.max(0, count - 1);
        } catch (err) {
            console.error(err);
            setStatus(`获取总数失败: ${err.message}`, true, false);
            countDisplay.innerText = '!';
        }
    }

    // 拉取指定范围的日志
    async function fetchLogs(start, end) {
        // 基本校验
        if (isNaN(start) || isNaN(end) || start < 0 || end < 0) {
            setStatus('起始/结束索引必须是非负整数', true);
            return;
        }
        if (start > end) {
            setStatus('起始索引不能大于结束索引', true);
            return;
        }

        setStatus(`正在拉取索引 ${start} ~ ${end} ...`, false, true);

        try {
            const url = `/api/log?start=${start}&end=${end}`;
            const response = await fetch(url);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const logsArray = await response.json();
            if (!Array.isArray(logsArray)) throw new Error('返回数据不是数组');

            // 保存到全局
            currentLogs = logsArray;
            currentStart = start;
            currentEnd = end;

            // 渲染表格
            renderTable(logsArray, start);
            setStatus(`成功加载 ${logsArray.length} 条日志 (索引 ${start} ~ ${start + logsArray.length - 1})`, false);
        } catch (err) {
            console.error(err);
            setStatus(`拉取失败: ${err.message}`, true);
            showEmptyTable('加载失败，请重试');
        }
    }

    // 渲染表格：data是从start开始的日志数组 (按索引升序，即data[0]对应get_log(start))
    function renderTable(data, startIdx) {
        if (!data || data.length === 0) {
            showEmptyTable('该范围内没有日志');
            return;
        }

        let html = '';
        for (let i = 0; i < data.length; i++) {
            const entry = data[i];
            const actualIndex = startIdx + i;   // 原始索引

            // 提取字段（兼容字段名大小写？假设返回与结构体一致）
            const timestamp = entry.timestamp ?? '—';
            const voltage = entry.voltage ?? '—';
            const current = entry.current ?? '—';
            const mah = entry.mah ?? '—';
            const mwh = entry.mwh ?? '—';
            let crc = entry.crc_checksum;
            if (crc !== undefined && crc !== null) {
                crc = '0x' + crc.toString(16).toUpperCase().padStart(2, '0');
            } else {
                crc = '—';
            }
            const sof = entry.sof ?? '—';
            const sofHex = (typeof sof === 'number') ? '0x' + sof.toString(16).toUpperCase() : sof;

            html += `<tr>
                <td>${actualIndex}</td>
                <td>${timestamp}</td>
                <td>${typeof voltage === 'number' ? voltage.toFixed(3) : voltage}</td>
                <td>${typeof current === 'number' ? current.toFixed(3) : current}</td>
                <td>${typeof mah === 'number' ? mah.toFixed(2) : mah}</td>
                <td>${typeof mwh === 'number' ? mwh.toFixed(2) : mwh}</td>
                <td>${crc}</td>
                <td>${sofHex}</td>
            </tr>`;
        }
        tableBody.innerHTML = html;
    }

    // 导出CSV (基于currentLogs)
    function exportToCsv() {
        if (!currentLogs || currentLogs.length === 0) {
            setStatus('没有数据可导出', true);
            return;
        }

        // 定义列名 (与表头一致)
        const headers = ['原始索引', '时间戳', '电压(V)', '电流(A)', 'mAh', 'mWh', 'CRC(hex)', '帧头(SOF)'];
        const rows = [];

        for (let i = 0; i < currentLogs.length; i++) {
            const entry = currentLogs[i];
            const actualIndex = currentStart + i;

            const timestamp = entry.timestamp ?? '';
            const voltage = entry.voltage ?? '';
            const current = entry.current ?? '';
            const mah = entry.mah ?? '';
            const mwh = entry.mwh ?? '';
            let crc = entry.crc_checksum;
            if (crc !== undefined && crc !== null) {
                crc = '0x' + crc.toString(16).toUpperCase().padStart(2, '0');
            } else {
                crc = '';
            }
            const sof = entry.sof ?? '';
            const sofHex = (typeof sof === 'number') ? '0x' + sof.toString(16).toUpperCase() : sof;

            rows.push([
                actualIndex,
                timestamp,
                voltage,
                current,
                mah,
                mwh,
                crc,
                sofHex
            ]);
        }

        // 构建CSV字符串
        let csvContent = headers.join(',') + '\n';
        rows.forEach(row => {
            // 简单转义：如果字段包含逗号或换行，用双引号包裹（这里数据简单，但稳妥起见）
            const escapedRow = row.map(cell => {
                if (typeof cell === 'string' && (cell.includes(',') || cell.includes('\n') || cell.includes('"'))) {
                    return `"${cell.replace(/"/g, '""')}"`;
                }
                return cell;
            }).join(',');
            csvContent += escapedRow + '\n';
        });

        // 创建下载链接
        const blob = new Blob(['\uFEFF' + csvContent], { type: 'text/csv;charset=utf-8;' }); // BOM for Excel
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `blackbox_${currentStart}_to_${currentEnd}.csv`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        setStatus(`CSV 已导出 (${rows.length} 条)`, false);
    }

    // 事件绑定
    fetchCountBtn.addEventListener('click', () => fetchLogCount());

    fetchRangeBtn.addEventListener('click', () => {
        const start = parseInt(startInput.value, 10);
        const end = parseInt(endInput.value, 10);
        fetchLogs(start, end);
    });

    exportBtn.addEventListener('click', exportToCsv);

    // 可选：在页面加载后尝试获取一次总数
    window.addEventListener('load', () => {
        fetchLogCount();
        // 给输入框设置一些友好提示
        const countNum = countDisplay.innerText;
        if (countNum !== '—' && !isNaN(parseInt(countNum))) {
            const max = parseInt(countNum) - 1;
            endInput.max = max;
            startInput.max = max;
        }
    });

    // 当总数更新后，动态调整输入框的max
    const observer = new MutationObserver(() => {
        const val = countDisplay.innerText;
        if (val !== '—' && !isNaN(parseInt(val))) {
            const maxIdx = parseInt(val) - 1;
            endInput.max = maxIdx;
            startInput.max = maxIdx;
            // 如果当前end超过max，修正
            if (parseInt(endInput.value) > maxIdx) endInput.value = maxIdx;
            if (parseInt(startInput.value) > maxIdx) startInput.value = maxIdx;
        }
    });
    observer.observe(countDisplay, { childList: true, characterData: true, subtree: true });
})();