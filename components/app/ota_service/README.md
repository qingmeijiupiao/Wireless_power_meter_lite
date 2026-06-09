# ota_service

远端 APP 固件检查与在线升级应用服务。组件通过 HTTPS 获取 OTA Manifest，复用
`ota_manager` 完成备用分区写入、镜像校验和启动分区切换。

## 版本检查

- Manifest 地址：
  `https://cdn.jsdelivr.net/gh/qingmeijiupiao/Wireless_power_meter_lite@firmware-dist/ota/latest.json`
- Manifest 由 Release 工作流生成，包含版本、固件大小和不可变下载 URL。
- 发布内容保存在独立的 `firmware-dist` 分支，不依赖开发者本地构建或手工同步。
- 仅当远端语义版本严格高于当前运行版本时允许在线升级。
- 相同版本和较低版本都返回 `up_to_date`；降级只能由用户手动上传 APP 固件。
- HTTPS 请求前要求系统时间有效，避免 TLS 证书有效期校验误判。

## 固件下载

固件使用 Manifest 下发的 jsDelivr commit SHA 固定地址下载。该地址指向
`firmware-dist` 中不可变的发布提交，不依赖 GitHub Release 代理。

下载使用 ESP-IDF HTTP Client 自动处理重定向，通过证书 Bundle 校验 TLS。
响应数据按块写入备用 OTA 分区，不在 RAM 中缓存完整固件。下载完成后校验
Manifest 声明的固件大小，并由 ESP-IDF OTA API 执行固件镜像校验。校验和激活
成功后等待 2 秒并自动重启。

每次尝试及其结果都通过 `diagnostic_log` 写入 ESP 日志，并由全局 Hook 自动持久化。
状态迁移附带快照，失败使用 `WARN` / `ERROR`，不会自动重试；用户可从 Web 或屏幕
重新发起。

## 状态

`idle`、`checking`、`update_available`、`up_to_date`、`downloading`、
`verifying`、`restarting`、`failed`。

<!-- dependency-links:start -->
## 依赖导航

工程内直接依赖：

- [`ota_manager`](../../middleware/ota_manager/README.md)（`middleware`）
- [`diagnostic_log`](../../common/diagnostic_log/README.md)（`common`）

> 本节按当前 `CMakeLists.txt` 的 `REQUIRES` / `PRIV_REQUIRES` 维护。
<!-- dependency-links:end -->
