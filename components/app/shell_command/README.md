# shell_command

Shell 命令注册模块，在 `ShellCommand` 命名空间内集中管理所有应用层命令的注册与实现。

## 模块特点

- **集中管理**：所有命令在 `init()` 中统一注册，添加命令只需修改一处
- **Lambda 实现**：命令处理函数以 lambda 表达式内联，命令定义与实现紧邻，可读性高
- **标准注释**：每条命令遵循统一注释模板，包含用途、用法、参数和注意事项

## 集成与使用

```cpp
#include "shell_command.h"

// 在 shell 初始化之后调用
Shell::instance().init();
ShellCommand::init();
```

## 添加新命令

在 `shell_command.cpp` 的 `init()` 函数中，按以下模板追加：

```cpp
/**
 * @brief  <命令名> - <简要描述>
 * @usage  <命令名> [参数列表]
 * @param  <参数1> - <参数说明>
 * @note   <注意事项>
 */
shell.register_command(ShellCommand_t("<命令名>", "<help文本>", "<hint文本>",
    [](int argc, char** argv) -> int {
        // 命令实现
        return 0;
    }));
```

### 示例：添加带参数的命令

```cpp
/**
 * @brief  echo - 回显输入文本
 * @usage  echo <text>
 * @param  text - 要回显的文本
 */
shell.register_command(ShellCommand_t("echo", "Echo input text", "<text>",
    [](int argc, char** argv) -> int {
        for (int i = 1; i < argc; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");
        return 0;
    }));
```

## 已注册命令

| 命令 | 说明 | 参数 |
|------|------|------|
| `reboot` | 重启设备 | 无 |
| `timestamp` | 获取系统时间戳(微秒) | 无 |
| `version` | 获取固件版本号 | 无 |

## 环境与依赖

- **软件**：ESP-IDF v5.x、C++11
- **组件依赖**：`shell`
