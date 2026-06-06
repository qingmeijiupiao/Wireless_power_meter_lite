# espnow_link

`espnow_link` 是主设备和无线开关共用的 ESP-NOW 链路组件，负责：

- ESP-NOW 生命周期、帧编解码、ACK、重传、去重和消息分发
- peer 运行期管理、LMK 与配对信息的 NVS 持久化
- 控制器配对窗口、无线开关逐信道发现和配对
- 已配对 peer 的加密信道探测与信道恢复
- RTT 估算、ACK RTO、自适应业务响应超时和诊断计数

产品业务消息及硬件回调不属于本组件。

## 结构

```text
espnow_link/
├── include/espnow_link.h
├── private_include/
│   ├── espnow_link_internal.h
│   ├── espnow_pairing_internal.h
│   └── espnow_protocol.h
└── src/
    ├── espnow_link.cpp
    ├── espnow_link_api.cpp
    ├── espnow_link_task.cpp
    ├── espnow_protocol.cpp
    ├── espnow_pairing.cpp
    ├── espnow_pairing_protocol.cpp
    └── espnow_peer_store.cpp
```

## 初始化和配对

```cpp
EspNowLink::init(EspNowLink::PairingRole::CONTROLLER);
EspNowLink::enter_pairing_mode(0); // 0 表示持续到成功配对或手动退出
```

无线开关使用 `REMOTE_SWITCH` 角色，并通过 `start_pairing()` 发起扫描。扫描优先尝试
已保存信道，再遍历当前国家码允许的信道。配对完成后双方保存 MAC、角色、LMK 和信道。

## 超时和信道恢复

可靠传输按 peer 维护平滑 RTT，并计算范围为 8 到 100 ms 的 ACK RTO。
`get_response_timeout_ms()` 根据请求重试次数、响应重试次数和业务处理预算返回等待时间，
业务层不应使用固定的长超时。

控制或读取超时后无需重新配对。`recover_peer_channel()` 保留原 LMK，在候选信道发送
加密可靠探测包；收到 transport ACK 即确认原 peer 已找到，并更新运行期 peer 和 NVS
中的信道。每信道等待时间同样由 peer RTO 计算。

## 约束

- 广播只能使用明文 `BEST_EFFORT`。
- 普通单播要求已注册加密 peer。
- 明文单播仅供配对协议显式使用。
- message handler 在 link 任务中执行，必须快速返回。
- NVS key 为 `now_peers`，LMK 不通过公共查询 API 暴露。

## 依赖

- ESP-IDF v6.0+
- `esp_wifi`
- `wifi_manager`
- `HXC_NVS`
- FreeRTOS
