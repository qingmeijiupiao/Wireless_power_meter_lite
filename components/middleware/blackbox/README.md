# Blackbox

黑匣子日志系统中间件，支持字符串日志与类型化数据日志，与具体业务数据结构解耦。底层使用 `circular_flash_buffer` 实现循环存储，上层由 `blackbox_structured`（app 层）定义具体的结构化数据格式。

## 模块特点

- **数据结构解耦**：middleware 层只定义通用帧头和 payload 容器，不依赖具体业务结构
- **字符串自动切分**：超过 payload 长度的字符串自动拆分为多条 STRING 记录，最多 3 条（共 74 字符）
- **字符串自动拼接**：`read_text()` 自动还原多条 STRING 分片，并返回占用的原始记录数
- **类型化数据接口**：`append_typed()` 提供二进制 payload 写入，由上层定义具体结构
- **CRC8 校验**：每条记录末尾附加 CRC8 校验码，读取时自动验证完整性
- **LogType 可扩展**：枚举类型，当前支持 `STRING` 和 `STRUCTURED`，可按需扩展

## 数据帧格式

每条记录 **32 字节**，布局如下：

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 1 | sof | 帧头，固定为 0xAA |
| 1 | 1 | type | 日志类型，参见 `LogType` 枚举 |
| 2 | 4 | timestamp | 毫秒时间戳 |
| 6 | 25 | payload | 数据负载（联合体：`.bytes` / `.str`） |
| 31 | 1 | crc_checksum | CRC8 校验码 |

### 字符串日志切分规则

- `len < 25`：单条 STRING，payload 末尾有 `\0`
- `len ≥ 25`：拆分为多条 STRING，非末尾片段 payload 无 `\0`（通过此特征判断是否有续条），末尾片段有 `\0`
- 最多拆分 3 条，超长字符串截断到 74 字符

### LogType 枚举

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | `STRING` | 字符串日志 |
| 1 | `STRUCTURED` | 结构化数据日志（由 app 层定义 payload 格式） |

## 架构与层级关系

```mermaid
%%{init: { 'theme': 'base', 'themeVariables': { 'primaryColor': '#E3F2FD', 'primaryBorderColor': '#1E88E5', 'primaryTextColor': '#0D47A1', 'lineColor': '#37474F' } }}%%
flowchart TD
    APP["app/blackbox_structured<br/>定义 SnapshotV1"] -->|append_typed| MW["middleware/blackbox<br/>通用帧头 + CRC + 分片"]
    MW -->|write_block| BSP["bsp/circular_flash_buffer<br/>Flash 循环缓冲区"]
    MW -->|append_text| MW
```

- **bsp/circular_flash_buffer**：Flash 循环读写，与数据结构无关
- **middleware/blackbox**：通用帧格式、CRC 校验、字符串分片，不依赖具体业务结构
- **app/blackbox_structured**：定义版本化业务快照，调用 `append_typed()`

## 集成与使用

```cpp
#include "blackbox.h"

// 初始化（在 app_main 中调用一次）
Blackbox::init();

// 写入字符串日志（支持 printf 格式，自动分片）
Blackbox::append_text("Voltage: %dmV", 12000);
Blackbox::append_text("Over current protection triggered at %duA", 2500000);

// 读取日志
uint32_t count = Blackbox::count();
Blackbox::Record raw = Blackbox::read(0); // 0 = 最新

if (raw.header.type == Blackbox::LogType::STRING) {
    printf("日志: %s\n", raw.payload.str);
} else if (raw.header.type == Blackbox::LogType::STRUCTURED) {
    // 由 app 层解读 payload.bytes
}

// 批量读取时暂停写入
Blackbox::set_enabled(false);
for (uint32_t i = 0; i < count; i++) {
    auto entry = Blackbox::read(i);
    // ...
}
Blackbox::set_enabled(true);
```

### 读取自动拼接的字符串日志

`read_text(index)` 自动拼接被拆分的字符串日志，并返回本次读取占用的原始记录数。
传入的索引必须指向该字符串日志最新的一条记录。批量读取按从新到旧的顺序轮询时，
每次将索引增加 `record_count`：

```cpp
for (uint32_t index = 0; index < Blackbox::count();) {
    auto text = Blackbox::read_text(index);
    if (text.record_count != 0) {
        printf("%s\n", text.str);
        index += text.record_count;
        continue;
    }

    auto raw = Blackbox::read(index);
    // 处理非字符串记录
    ++index;
}
```

记录无效、记录不是字符串类型，或索引指向非末尾字符串分片时，
`read_text()` 返回的 `record_count` 为 `0`。

## API 参考

### `esp_err_t init()`

初始化黑匣子，内部调用 `CircularFlashBuffer::init()` 恢复写入位置。在 `app_main` 中调用一次。

### `esp_err_t append_text(const char *fmt, ...)`

写入字符串日志，支持 printf 格式化。自动分片（最多 3 条），超长截断。

### `esp_err_t append_typed(LogType type, const uint8_t* payload, size_t len)`

写入类型化二进制数据，由上层（如 `blackbox_structured`）调用。`len` 不得超过 `PAYLOAD_SIZE`。

### `uint32_t count()`

返回已写入的日志总条数（含分片条目）。

### `Record read(uint32_t index)`

按倒序读取指定索引的日志原始数据，`index=0` 为最新。自动验证 SOF 和 CRC8，校验失败返回全零。

### `TextRecord read_text(uint32_t index)`

按倒序索引读取字符串日志并自动拼接分片，`index` 必须指向字符串日志最新的一条记录。
返回值中的 `str` 为以 `\0` 结尾的完整字符串，`record_count` 为本次读取占用的原始记录数。
读取失败时 `record_count` 为 `0`。批量读取时应使用 `record_count` 跳过已经拼接的分片。

### `void set_enabled(bool enable)`

启用/禁用日志写入。禁用时自动写入一条状态变更日志。

## 环境与依赖

| 类别 | 要求 |
|------|------|
| 框架 | ESP-IDF v6.0+ |
| RTOS | FreeRTOS |
| 组件依赖 | `circular_flash_buffer`, `esp_timer`, `log` |
