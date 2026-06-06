# espnow_service

`espnow_service` 只实现 Wireless Power Meter 产品业务协议：

- 开关控制请求与响应
- 实时数据读取请求与响应
- 周期数据上报
- 业务 payload 编解码
- 业务任务队列和产品回调分发

配对、PING、信道扫描、peer/LMK 持久化、RTT 和信道恢复均由 `espnow_link` 提供。

## 结构

```text
espnow_service/
├── include/espnow_service.h
├── private_include/espnow_service_internal.h
└── src/
    ├── espnow_service_business.cpp
    ├── espnow_service_business_protocol.cpp
    └── espnow_service_product.cpp
```

`espnow_service_product.cpp` 将通用业务回调接到功率输出、全局测量状态和能量计。

## 消息

| ID | 语义 |
|---|---|
| `0x0200` | 可靠开关控制请求 |
| `0x0201` | 可靠开关控制响应 |
| `0x0210` | 可靠实时数据请求 |
| `0x0211` | 可靠实时数据响应 |
| `0x0212` | 尽力周期数据上报 |

请求和响应使用非零 `request_id` 关联。协议逐字段使用小端固定宽度编码，不发送 C++
结构体的原始内存。

## 初始化

必须先初始化 link：

```cpp
EspNowLink::init(EspNowLink::PairingRole::CONTROLLER);
EspNowService::init();
```

link handler 只解码并投递事件；开关操作、数据采集和用户回调在独立业务任务中执行。

## 依赖

- `espnow_link`
- 功率计产品回调所需的 app 组件
- FreeRTOS
