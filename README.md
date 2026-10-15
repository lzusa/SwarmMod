# SwarmMod

东方非想天则（Touhou Hisoutensoku）的游戏状态检测 Mod，供支持自动推送功能的 Swarm UI 识别实际对战。

Mod 在本地报告游戏角色、场景、连接编号和心跳；HTTP 推送由 Swarm UI 完成。单独安装此 Mod 不会向 Konni 或其他网站发送请求。

## 安装

1. 退出游戏，将发布包内的 `modules/SwarmMod` 复制到游戏的 `modules` 目录。
2. 使用普通 SWRSToys 时，在 `SWRSToys.ini` 的 `[Module]` 中加入：

   ```ini
   SwarmMod=modules/SwarmMod/SwarmMod.dll
   ```

3. 使用 SokuModLoader 时，保留同目录的 `mod.json`，在模块管理中启用 SwarmMod；模块路径为 `modules/SwarmMod/SwarmMod.dll`，优先级为 `-60000`。不要只修改加载器自动生成的 SWRSToys.ini。
4. 重新启动游戏，配合支持本协议的 Swarm UI 使用。

Swarm UI 的自动推送开关首次默认关闭，可记住设置并配置目标接口。作为主机通过 Swarm 中转进入实际对战后，由 UI 推送；客户端、观战、单机和录像不触发。连续再战沿用同一连接编号。建议关闭 SokuLobbies 自带的 `ReportToKonni`，避免重复注册。

## 构建与测试

需要 Windows、Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、CMake，以及 Git。输出为 32 位 DLL。

在仓库根目录运行：

```powershell
git clone https://github.com/SokuDev/SokuLib.git deps/SokuLib
git -C deps/SokuLib checkout 0794becf1f578b32329604483be2112fe0afd4ee
./build.ps1
```

也可指定已有依赖：`./build.ps1 -SokuLibRoot C:/path/to/SokuLib`。

脚本编译 DLL、执行状态测试，并将安装文件写入 `dist/modules/SwarmMod/`。测试覆盖连接、连续再战、断线重连、角色切换和 JSON 转义。

## 本地协议

为兼容已有 Swarm UI，数据路径保留为 `%LOCALAPPDATA%/swarmmod/status.json`。每 500 ms 原子替换一次：

| 字段 | 含义 |
| --- | --- |
| `protocol` | 协议版本，目前为 `1` |
| `version` | Mod 版本 |
| `pid`、`instance` | 游戏进程与本次运行标识 |
| `heartbeat` | 递增心跳编号 |
| `responsive` | 游戏回调是否仍在更新；十秒无更新则为 false |
| `connection` | 对局连接编号，连续再战保持不变 |
| `role` | `host`、`client`、`spectator`、`none` |
| `phase` | `idle`、`select`、`loading`、`battle` |
| `profile_name` | UTF-8 玩家档案名 |

Mod 不改变游戏数据或 UDP 流量，场景钩子串联已有回调。适用于 SokuLib 支持的游戏版本；与其他 Mod 的兼容性及观战可达性仍需实际环境验证。目前面向单个运行中的游戏实例。

## 依赖

使用 [SokuDev/SokuLib](https://github.com/SokuDev/SokuLib)，其 MIT 许可随安装文件一同提供，见 [SokuLib-LICENSE.txt](SokuLib-LICENSE.txt)。
