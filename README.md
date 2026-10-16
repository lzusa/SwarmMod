# SwarmMod 0.1.0

东方非想天则的 Swarm 中转对局自动推送 Mod。游戏状态检测和 HTTP PUT 均在 DLL 内完成，无需辅助 EXE。本仓库只包含 Mod，需配合支持配置同步的 Swarm UI 使用。

## 使用

1. 退出游戏，将发布包的 modules/SwarmMod 放入游戏目录，替换 DLL 和 mod.json。已有 SwarmMod.ini 请保留。
2. 启用 modules/SwarmMod/SwarmMod.dll。SokuModLoader 在 ModLoaderSettings.json 中启用此路径，优先级 -60000；普通 SWRSToys 在 [Module] 中加入 SwarmMod=modules/SwarmMod/SwarmMod.dll。
3. 将 swarm-ui.exe 放在游戏目录运行，或先启动游戏，让 Swarm 自动定位并记住目录。
4. 独立开启“IP用于大厅”“IP推送网页”，然后创建中转。两个开关默认关闭，记住各自偏好。已有中转时开启也会立即同步。

大厅开关写入 modules/SokuLobbiesMod/SokuLobbies.ini 的 [Lobby] HostIP。网页开关写入 modules/SwarmMod/SwarmMod.ini，由 Mod 在主机实际对战时后台推送。客户端、观战、单机和录像不触发；同一连接连续再战不重复注册；重新连接、更换中转或接口可重新注册。失败最多尝试三次，间隔 15 秒、60 秒。

## 配置与清理

SwarmMod.ini 位于 DLL 同目录：

```ini
[AutoPush]
Enabled=0
RelayAddress=
Endpoint=https://konni.delthas.fr/games
Session=
```

Endpoint 可自行编辑，Swarm 保留此值。目标须兼容 Konni PUT JSON。其余字段由新版 Swarm 同步，请勿手动填写 Session。

Swarm 启动时清空已定位目录的旧大厅和 Mod 地址。重建中转前、停止中转及正常退出时清空本实例同步的地址；关闭对应开关也会清空对应地址。开关偏好保留。

Mod 推送前检查 Swarm 的进程会话；崩溃或强制退出后不使用残留 IP。只有 Mod 而无新版 Swarm 时不推送。未装大厅只禁用大厅开关；未装 SwarmMod 0.1.0 只禁用网页开关，不创建虚假模块目录。旧版不支持配置同步协议的 Mod 必须替换为此包中的 DLL 和 mod.json。

Swarm 优先使用唯一运行中的游戏目录，其次自身游戏目录、已记住有效目录；找不到或多个候选时不猜选，可把 Swarm 放到目标目录。偏好保存在 %LOCALAPPDATA%/swarmmod/swarm-ui.ini 的 [Sync] 中。

Mod 同目录 SwarmMod-result.ini 保存最近请求的 HTTP 状态及 Success。2xx 不保证网站收录或观战可达；已发出的请求无法通过开关撤回。

## 构建和验证

Mod 需要 Visual Studio 2022 C++、Windows SDK、CMake、SokuDev/SokuLib，运行 ./build.ps1 -SokuLibRoot C:/path/to/SokuLib。依赖提交 0794becf1f578b32329604483be2112fe0afd4ee。

可自行获取依赖后构建：

```powershell
git clone https://github.com/SokuDev/SokuLib.git deps/SokuLib
git -C deps/SokuLib checkout 0794becf1f578b32329604483be2112fe0afd4ee
./build.ps1
```

产物位于 `dist/modules/SwarmMod/`。构建脚本运行状态测试，并附带依赖许可。libcurl 和 mbedTLS 由 CMake 下载固定版本并校验 SHA-256。

本地 HTTP 集成测试（两个终端）：

```powershell
# 终端 1
python tests/http_server.py

# 终端 2
cmake --build build --config Release --target swarmmod_http_tests
./build/Release/swarmmod_http_tests.exe http://127.0.0.1:18349/games
```

状态测试覆盖连接、再战、角色切换、地址校验、重试和去重；HTTP 测试覆盖 PUT、JSON 转义、Unicode、配置关闭及会话失效。游戏内兼容性和实际 Konni 观战仍需在目标环境验证。

当前版本在 SwarmMod.dll 的后台线程中直接使用静态链接的 libcurl 8.23.0 + mbedTLS 3.6.7 发送 HTTPS PUT，不再需要辅助 EXE。证书链及主机名校验保持开启，信任根取自 Windows 证书库；系统手动代理可自动读取。构建首次会下载并校验固定版本依赖。

依赖许可随构建产物附带：SokuLib 为 MIT，libcurl 为 curl 许可，mbedTLS 使用 Apache-2.0 许可选项；完整许可见对应 `*-LICENSE.txt` 文件。
