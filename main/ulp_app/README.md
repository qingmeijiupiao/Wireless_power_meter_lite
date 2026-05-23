# ulp_app

LP 核应用程序，运行在 ESP32-C6 的 LP Core 上，负责 INA226 采样、电流校准补偿、电量/能量积分以及向 HP 核暴露 RTC 共享变量。主程序入口在 `ulp_main.cpp`，INA226 低功耗 I2C 驱动在 `ina226.hpp`，LP/HP 共享状态位定义在 `ulp_state.h`。

## 模块特点

- **LP 核独立采样**：INA226 连续转换，LP 核轮询 `CNVR` 转换完成位后读取电压与分流器寄存器
- **RTC 共享变量**：采样值、原始寄存器、积分值、状态位和校准参数放在 `.rtc.bss` 段，供 HP 核直接访问
- **整数电流校准**：使用 `current_base_K`、6 点非等间距插值和 `temperature_K` 完成无浮点温漂补偿
- **电量/能量积分**：以 `uA·ms` 和 `uA·uV·ms` 累加，跨阈值后更新 `meter_uah` 和 `meter_uwh`
- **溢出安全计时**：基于 20MHz CPU cycle 维护 `now_time_ms`，处理底层计数回绕

## 运行流程

```mermaid
flowchart TD
    Boot["main()"] --> LoadCalib["load_current_calib_params()<br/>加载 RTC 校准参数到插值器"]
    LoadCalib --> InaInit["ulp_ina226_init()<br/>reset / manufacturer check / config"]
    InaInit --> Ready["置位 ulp_run 与 ulp_ina226_init_ok"]
    Ready --> Loop["while(1) 主循环"]
    Loop --> Sample["ina226_run()<br/>CNVR ready 后读取 BUS 与 SHUNT"]
    Sample --> Timer["timer_run()<br/>更新 now_time_ms"]
    Timer --> Freq{"每 1000ms"}
    Freq -->|是| UpdateFreq["更新 core_run_freq_hz"]
    Freq -->|否| ReloadCheck
    UpdateFreq --> ReloadCheck{"每 20ms"}
    ReloadCheck -->|是| Reload["检查 ulp_reload_calib_params"]
    ReloadCheck -->|否| MeterCheck
    Reload --> MeterCheck{"每 10ms"}
    MeterCheck -->|是| Meter["update_meter()<br/>电量/能量积分"]
    MeterCheck -->|否| Inc["loop_times++"]
    Meter --> Inc
    Inc --> Loop
```

## 采样与校准数据流

```mermaid
flowchart LR
    INA["INA226"] -->|BUS_VOLTAGE| VRaw["voltage_register_raw"]
    INA -->|SHUNT_VOLTAGE| IRaw["shunt_register_raw"]
    VRaw --> V["voltage_uv = raw * 1250"]
    IRaw --> Dead{"abs(raw * current_base_K)<br/>小于死区?"}
    Dead -->|是| Zero["current_uA = 0"]
    Dead -->|否| Base["线性基准<br/>current_base_K * raw"]
    Base --> Interp["6 点插值修正<br/>offset_current_100uA * 100"]
    Interp --> Temp["温漂补偿<br/>temperature_K ppm/°C"]
    Temp --> Current["current_uA"]
    BoardTemp["Board_temperature<br/>HP 核写入"] --> Temp
    Calib["current_calib_params<br/>RTC 共享参数"] --> Base
    Calib --> Interp
    Calib --> Temp
```

## HP/LP 共享变量

```mermaid
classDiagram
    class ULP_CORE_STATE {
        +ulp_have_log : 1
        +ulp_i2c_init_err : 1
        +ulp_ina226_init_ok : 1
        +ulp_ina226_read_timeout : 1
        +ulp_run : 1
        +ulp_reload_calib_params : 1
    }
    class CurrentCalib_params_t {
        +uint16_t current_base_K
        +point_t points[6]
        +int16_t temperature_K
    }
    class RTC_Shared {
        +uint32_t voltage_uv
        +uint16_t voltage_register_raw
        +int32_t current_uA
        +int16_t shunt_register_raw
        +int32_t Board_temperature
        +int32_t meter_uah
        +int32_t meter_uwh
        +uint32_t core_run_freq_hz
    }
    RTC_Shared --> ULP_CORE_STATE
    RTC_Shared --> CurrentCalib_params_t
```

## 与 HP 核交互

```mermaid
sequenceDiagram
    participant HP as HP 核
    participant RTC as RTC 共享内存
    participant LP as LP 核 ulp_app
    participant INA as INA226

    HP->>RTC: 写入 current_calib_params
    HP->>LP: ulp_lp_core_run()
    LP->>INA: reset + configuration
    LP->>RTC: 置位 ulp_run / ulp_ina226_init_ok
    loop 主循环
        LP->>INA: 读取 BUS / SHUNT 寄存器
        LP->>RTC: 更新 voltage_uv / current_uA / raw register
        HP->>RTC: 读取采样值并写入 Board_temperature
    end
    HP->>RTC: 修改校准参数并置位 ulp_reload_calib_params
    LP->>RTC: 重新加载插值表并清除标志
```

## 文件说明

| 文件 | 作用 |
|------|------|
| `ulp_main.cpp` | LP 核主循环、INA226 调度、电流补偿、电量/能量积分 |
| `ina226.hpp` | LP Core I2C 版 INA226 寄存器读写与配置 |
| `ulp_Interp.hpp` | LP 核可用的固定容量非等间距插值器 |
| `ulp_state.h` | HP/LP 共享状态位定义 |

## 注意事项

- `voltage_uv` 实际由 INA226 bus voltage raw 乘以 `voltage_scale = 1250` 得到，HP 核在 `app_main.cpp` 中转换为 mV。
- `current_uA` 是已完成死区、插值和温漂补偿后的最终电流值，符号保留电流方向。
- `Board_temperature` 由 HP 核写入，单位为 0.01°C，LP 核用它做温漂补偿。
