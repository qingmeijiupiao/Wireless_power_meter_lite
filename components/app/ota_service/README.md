# ota_service

远端 APP 固件检查与在线升级应用服务。组件通过 HTTPS 获取发布配置，复用
`ota_manager` 完成备用分区写入、镜像校验和启动分区切换。

## 版本检查

- 配置地址：
  `https://cdn.jsdelivr.net/gh/qingmeijiupiao/Wireless_power_meter_lite/.github/workflows/config.toml`
- 从 `firmware_images_url` 的 `/download/vX.Y.Z/` 路径段提取版本。
- 仅当远端语义版本严格高于当前运行版本时允许在线升级。
- 相同版本和较低版本都返回 `up_to_date`；降级只能由用户手动上传 APP 固件。
- HTTPS 请求前要求系统时间有效，避免 TLS 证书有效期校验误判。

## 固件下载

按顺序尝试以下地址：

1. GitHub Release
2. `gh-proxy.com`
3. `ghproxy.net`

每次尝试及其结果都会写入 ESP 日志和黑匣子。三个地址全部失败后进入
`failed` 状态，不自动重试；用户可从 Web 或屏幕重新发起。

下载使用 ESP-IDF HTTP Client 自动处理重定向，通过证书 Bundle 校验 TLS。
响应数据按块写入备用 OTA 分区，不在 RAM 中缓存完整固件。下载、校验和激活
成功后等待 2 秒并自动重启。

## 状态

`idle`、`checking`、`update_available`、`up_to_date`、`downloading`、
`verifying`、`restarting`、`failed`。

<!-- dependency-links:start -->
## 依赖导航

工程内直接依赖：

- [`blackbox_service`](../blackbox_service/README.md)（`app`）
- [`ota_manager`](../../middleware/ota_manager/README.md)（`middleware`）

> 本节按当前 `CMakeLists.txt` 的 `REQUIRES` / `PRIV_REQUIRES` 维护。
<!-- dependency-links:end -->
