# 第三方组件

- **libmpv**：提交 `e471496c56`，shinchiro Windows x86_64 构建 `20261021`。由 [mpv 官方安装页](https://mpv.io/installation/)列出的第三方构建，包含 FFmpeg 等媒体依赖。[构建配方与依赖源码来源](https://github.com/shinchiro/mpv-winbuild-cmake/tree/20261021)，[mpv 对应源码](https://github.com/mpv-player/mpv/tree/e471496c56)。版权、许可见 `licenses/mpv-*`。
- **yt-dlp**：官方独立可执行文件 `2026.10.26`。[源码与构建说明](https://github.com/yt-dlp/yt-dlp/tree/2026.10.26)。主项目使用 Unlicense，独立可执行文件包含其他许可的依赖，详见上游许可章节与 `licenses/yt-dlp-LICENSE`。
- **nlohmann/json**：`3.11.3`，MIT，Copyright (c) 2013-2023 Niels Lohmann，仅构建使用。[对应源码](https://github.com/nlohmann/json/tree/v3.11.3)，许可见 `licenses/json-LICENSE.MIT`。
- **7-Zip 7zr**：`26.04`，仅构建时解压，不随播放器运行。[官方来源](https://www.7-zip.org/)，许可见 `licenses/7zip-license.txt`。执行前校验锁定 SHA256。

准确下载地址和校验值在 `deps.lock.json`。原始 libmpv 归档保存在源码工作区的 `third_party/downloads`，构建头文件、脚本和依赖锁也保留在工作区。应用自身源码使用 GPL-3.0-or-later，全文见 `LICENSE`。
