# OnePlus & Realme 屏幕刷新率超频模块 - [OnePlus-Realme-Screen-R efresh-Rate-Overclocking]

> **🔔 关于此分支**
> * **本分支维护者**：[某钻/mouzuan]
> * **原项目作者**：慕容茹艳 (酷安 @慕容雪绒)
> * **项目关系**：此仓库是基于[原项目](https://github.com/murongruyan/murongchaopin)的 **分支（Fork）**，遵循 **GPL 3.0 协议**。
> * **主要修改**：只保留修改dtbo部分，其余全部剔除。
> * **重要**：原始版权、许可证及致谢均归属于原项目作者及贡献者。
> ---

## 简介
这是一个专为 OnePlus 和 Realme 设备设计的 KernelSU/Magisk 模块，解锁屏幕刷新率限制，支持更多高刷档位。通过修改 DTBO (Device Tree Blob Overlay)，本模块可以让您的设备支持自定义的高刷新率，带来更流畅的视觉体验。

## 功能特性
- **多档位刷新率支持**：144Hz 165Hz 175Hz 185Hz（具体能使用刷新率取决于屏幕体质和驱动支持）。
- **WebUI 管理界面**：内置功能强大的 Web 管理界面，无需复杂的命令行操作。
- **自定义配置**：
  - 支持查看当前支持的刷新率节点。
  - 支持手动添加自定义刷新率节点。
  - 支持删除不需要的刷新率节点。
- **安全机制**：
  - 自动备份原厂 DTBO，随时可以恢复到出厂状态。
  - 模块卸载功能。

## 安装与使用
1. **下载与安装**：
   - 在 KernelSU 或 Magisk 管理器中刷入本模块的 ZIP 包。
   - 重启手机以生效。

2. **使用管理界面**：
   - 打开 KernelSU/Magisk 应用。
   - 进入“模块”页面。
   - 找到“一加/真我超频dtbo模块”。
   - 点击模块卡片上的“操作”或“WebUI”按钮（取决于管理器版本）。
   - 在弹出的 Web 界面中进行刷新率管理、ADFR 设置或恢复操作。

## 注意事项
- **风险提示**：修改屏幕刷新率和系统底层参数存在一定风险，可能导致屏幕显示异常、耗电增加或系统不稳定。请务必在操作前备份重要数据。
- **黑屏处理**：如果应用新的刷新率后出现黑屏，请尝试强制重启手机。如果问题依旧，请进入安全模式。
- **兼容性**：本模块目前仅适配 **OnePlus Ace 3V(PJF110)**。其他 OnePlus/Realme 机型请谨慎测试。

## 更新日志
请查看 [update.json](https://raw.githubusercontent.com/murongruyan/murongchaopin/main/update.json) 获取最新版本信息。

## 开源协议
本项目采用 [GPL 3.0 License](LICENSE) 开源。

## 致谢
感谢所有为本项目提供测试和建议的朋友。
- **上游项目**（https://github.com/murongruyan/murongchaopin/）
- **酷安穆远星**（http://www.coolapk.com/u/28719807）
- **GitHub开源项目**（https://github.com/KOWX712/Tricky-Addon-Update-Target-List）
- **酷安大肥鱼** (http://www.coolapk.com/u/951790)
- **bybycode**（http://www.coolapk.com/u/716079）
- **破星**（http://www.coolapk.com/u/21669766）
- **酷安望月古川** (http://www.coolapk.com/u/843974)
- **qq傻瓜我爱你呀** (QQ: 3844041986)
- **COPG开源项目**（https://github.com/AlirezaParsi/COPG）