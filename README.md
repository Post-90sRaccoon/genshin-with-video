# 原神攻略小窗

Windows x64 原生悬浮播放器。粘贴 B 站链接或 BV 号，选择分 P，在游戏旁边播放。C++ / Win32 界面，libmpv 播放并通过其内建 ytdl hook 调用 yt-dlp 在线解析，不常驻网页或 Python 服务。

当前状态：旧版 EXE 曾两次被 Defender 隔离。2026-11-01 已删除前端自制解析子进程管理代码，修正加载状态与恢复进度，新版通过离线回归、P2 的 720p/AAC 播放检查和 10 分钟运行采样；用户确认正式窗口画面和声音正常。本机专用工作集约 148–170 MiB。原神与小窗均以管理员权限运行后，用户确认游戏前台 F8、F9、F10 全部正常。独占全屏未单独验证。具体证据见 `docs/verification.md`，退出原因调查见 `docs/crash-investigation.md`。微软尚未确认误报。

## 下载

2026-11-05 本地修正：播放时自动更新即将到期的音视频地址，长时间暂停后继续播放会检查地址有效期，并保留进度。针对 15:55 画面冻结的调查与验证见 `docs/playback-freeze-investigation.md`。

👉 [前往下载 Windows x64 便携版](https://github.com/Post-90sRaccoon/genshin-with-video/releases)

进入发布页面，展开对应版本的 **Assets**，下载其中的 Windows x64 便携 ZIP 压缩包。

**Source code (zip)** 和 **Source code (tar.gz)** 是源码包，不包含可直接运行的完整程序。

下载后完整解压，打开文件夹中的 `GenshinVideo.exe`。
如果游戏以管理员权限运行，请先退出小窗，再通过 `Start-GameMode.cmd` 启动。

## 使用

打开 `dist/GenshinVideo/GenshinVideo.exe`，点击 **打开**。**选集**可切换路线，每个视频、每个 CID 分别保存进度。位置、尺寸和观看进度保存在程序旁的 `data` 文件夹。移动时请保留整个便携目录，目录需要可写。

**本机原神以管理员权限运行，请先退出已有小窗，再双击 `Start-GameMode.cmd`，确认 Windows UAC。** 这个入口让小窗以相同权限启动，启动后入口进程会退出。直接双击 EXE 使用普通权限，适合游戏也以普通权限运行的情况。若重复打开游戏模式入口，会提示先退出正在运行的小窗。

| 默认快捷键 | 操作 |
| --- | --- |
| F8 | 暂停 / 继续 |
| F9 / F10 | 倒退 / 前进 5 秒 |
| Ctrl+Alt+[ | 减速一档（最低 0.5×） |
| Ctrl+Alt+] | 加速一档（最高 2×） |
| Ctrl+Alt+0 | 恢复 1× |
| F7 | 隐藏并暂停 / 显示并保持暂停 |
| F6 | 开关鼠标穿透 |
| Ctrl+Alt+M | 显示 / 隐藏顶部菜单栏 |
| Ctrl+O / Ctrl+P / Ctrl+, | 打开 / 选集 / 设置（小窗前台时） |

拖动上方空白区域移动，拖动边缘缩放。右键菜单提供重试、从头播放、上一 P / 下一 P、退出。托盘图标可显示并解锁窗口，重新启动相同目录的程序也可找回窗口。

熟悉快捷键后，可按 **Ctrl+Alt+M** 隐藏顶部菜单栏，也可在小窗或托盘图标的右键菜单中切换 **显示菜单栏**。隐藏后保留底部播放信息，拖动底部状态栏即可移动窗口。切换不影响播放，程序会记住菜单栏状态；再次按快捷键或通过右键菜单可恢复。快捷键可在 **设置 → 菜单栏显示 / 隐藏** 中修改。

点击顶部的 **1×**（当前倍速）按钮，或右键 → **播放速度**，选择 **0.5×、0.75×、1×、1.25×、1.5×、1.75×、2×**。底部播放信息也会同步显示当前倍速。调整立即生效，启用音调校正，暂停时调整仍保持暂停。倍速会保存，切换分 P、重试、更新播放地址及下次启动都会沿用。进度和前后跳转仍按视频时间计算。

变速和菜单栏快捷键为全局按键，可在 **设置 → 快捷键** 中修改；发生按键占用时会提示。旧版五项或八项快捷键会保留并补上新增动作；如果旧自定义按键已占用了新默认值，新增动作会选用空闲组合键，实际结果以设置窗口为准。

在 **设置** 中调整按键、360p / 480p / 720p 画质上限及音量。选流优先上限以内的 H.264、30fps 及以下视频。按键冲突会提示，解除热键不可用时不能开启鼠标穿透。公开画质以每次实际返回为准，720p 上限不代表所有视频匿名可获取 720p。

登录信息为可选的 Netscape 格式 `cookies.txt` 文件，只保存所选文件路径；不自动读取浏览器账户，不提供内嵌登录。具体画质仍取决于账号的播放权限。

## 游戏与资源

优先使用窗口化 / 无边框游戏模式。独占全屏覆盖需要实际验证。程序使用 Windows 全局热键，不读写游戏进程、不自动操作游戏。

默认前向压缩包缓存 16 MiB、回退缓存 8 MiB，均不是整个进程的内存上限；DLL 磁盘大小也不是运行占用。实测结果见 `docs/verification.md`。画质直接影响解码开销。

## 命令行与构建

```powershell
& .\dist\GenshinVideo\GenshinVideo.exe 'https://www.bilibili.com/video/BV1BMgj6MEz4/?p=2'
```

源码构建需要 Visual Studio C++ Build Tools（已验证 VS2017 14.16）、Windows 10 SDK 和 PowerShell：

```powershell
.\scripts\setup-deps.ps1
.\scripts\build.ps1
.\scripts\build.ps1 -TestOnly        # 核心策略
.\scripts\build.ps1 -IntegrationOnly # 存储、网络句柄、控制器
.\build\integration.exe --network    # 实际音视频解码
.\tests\diagnostics_tests.ps1       # 退出证据关联回归
.\scripts\run-app.ps1 -WaitSession  # 持续运行，退出后收集本次原因
```

依赖读取 `deps.lock.json` 并校验 SHA256，不自动换成最新版本。缓存完整时可离线校验。输出为 `dist/GenshinVideo`。源码和依赖许可、来源见 `LICENSE`、`THIRD_PARTY.md` 和 `licenses`。

`integration.exe --network` 打开的是临时验证窗口，测试结束后会主动关闭。日常使用请打开上面的 `GenshinVideo.exe`。正式程序的 `data/session.jsonl` 只记录时间、进程号和事件，不记录视频地址或登录信息。
