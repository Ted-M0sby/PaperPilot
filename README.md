# 智阅文献伴侣（PaperPilot）

智阅文献伴侣是一个部署在 Windows Server + IIS 上的本地文献问答系统。项目由三部分组成：

- `frontend/`：IIS 托管的原生 HTML/CSS/JavaScript 前端。
- `backend/`：PaperPilot C 后端，负责文档导入、本地索引、检索和 AI 回答。
- `conflux/`：Conflux C++20 API Gateway，负责 `/api/*` 反向代理、路由管理、指标统计和限流。

线上访问链路：

```text
浏览器
  -> IIS 静态站点 frontend/
  -> IIS web.config 将 /api/* 转发到 http://127.0.0.1:8080
  -> Conflux 按 routes.yaml 将 /api/* 转发到 http://127.0.0.1:3000
  -> PaperPilot 后端处理 /api/import_text 和 /api/answer
```

## 当前功能

- 支持上传 `txt`、`docx`、`ppt`、`pptx`、`pdf` 文档。
- 主流程保持四步：选文档、提问题、生成回答、查看结果。
- 前端上传文件后调用 `/api/import_text` 建立本地索引。
- 用户提问时调用 `/api/answer`，后端基于本地索引和 AI 接口生成回答。
- 左侧文件预览侧边栏支持折叠、展开和拖拽调整宽度。
- 文件预览支持：
  - `txt`：纯文本预览。
  - `docx`：使用 `docx-preview` 前端渲染。
  - `ppt` / `pptx`：提取幻灯片文字做简化预览。
  - `pdf`：使用 `pdf.js` 渲染页面，并叠加文字层支持选中文字。
- 用户可在预览区选择文字，选中文本会作为问题上下文传给 AI。
- AI 回答默认按约 600 字生成，优先围绕问题和选中文本回答。
- 同一文件连续提问时，前端会复用已导入索引，避免重复导入。
- 右侧 Conflux 面板展示网关健康状态、请求量、延迟、限流和路由信息。

## 目录结构

```text
.
├── frontend/
│   ├── index.html
│   ├── styles.css
│   ├── app.js
│   ├── web.config
│   └── dist/
├── backend/
│   ├── src/
│   ├── include/
│   ├── scripts/
│   ├── frontend/
│   ├── CMakeLists.txt
│   ├── paperpilot.conf.example
│   └── README.md
├── conflux/
│   ├── src/
│   ├── configs/
│   ├── tests/
│   ├── third_party/
│   ├── CMakeLists.txt
│   └── README.md
└── README.md
```

说明：压缩包已排除运行数据、日志、临时文件、用户上传文档和构建缓存，包括 `backend/data/`、`backend/logs/`、`backend/tmp/`、`backend/build/`、`conflux/build/`。

## 前端部署

将 `frontend/` 内容部署到 IIS 站点目录，例如：

```text
C:\inetpub\wwwroot
```

`frontend/web.config` 中包含：

- HTTPS 跳转。
- 静态资源 MIME 类型。
- 安全响应头。
- `/api/*`、`/metrics`、`/admin/routes`、`/conflux/*` 到 Conflux 的反向代理规则。

生产环境需要 IIS 安装 URL Rewrite，并启用 ARR 反向代理能力。

## 后端 PaperPilot

PaperPilot 后端是 C 语言实现的本地服务。主要接口：

- `POST /api/import_text`：接收前端上传的文件内容，提取文本并写入本地索引。
- `POST /api/answer`：根据问题和本地索引上下文调用兼容 OpenAI 的 AI 接口。
- `GET /api/health`：健康检查。

### 配置

复制示例配置：

```powershell
Copy-Item .\backend\paperpilot.conf.example .\backend\paperpilot.conf
```

按需修改：

```ini
index_path=./data/index.dat
top_k=8
ai_enabled=1
ai_api_url=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
ai_model=qwen3.7-plus
ai_api_key=env:DASHSCOPE_API_KEY
```

不要把真实 API Key 写入仓库。推荐用环境变量：

```powershell
$env:DASHSCOPE_API_KEY = "你的 API Key"
```

### 构建与启动

在 `backend/` 目录执行：

```powershell
.\scripts\build.ps1 -Config Release
```

启动后端服务，监听 `127.0.0.1:3000`：

```powershell
.\build\Release\paperpilot.exe serve 3000
```

## Conflux API Gateway

Conflux 是项目中的 C++20 API 网关。它在本项目中的作用：

- 统一承接 IIS 转发过来的 `/api/*` 请求。
- 按 `conflux/configs/routes.yaml` 将请求转发给 PaperPilot 后端。
- 暴露 `/metrics` 给前端展示请求量、延迟和服务质量。
- 暴露 `/admin/routes` 给前端展示当前路由。
- 提供限流、超时、负载均衡和访问日志能力。

当前路由配置示例：

```yaml
routes:
  - id: paperpilot-api
    path_prefix: /api
    strip_prefix: false
    priority: 200
    targets:
      - http://127.0.0.1:3000
```

### 构建与启动

在 `conflux/` 目录执行：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

启动网关，监听 `127.0.0.1:8080`：

```powershell
$env:NEXUS_ROUTES_FILE = ".\configs\routes.yaml"
$env:NEXUS_RATELIMIT_ENABLE = "true"
$env:NEXUS_PROXY_TIMEOUT_MS = "90000"
.\build\Release\conflux.exe
```

如果构建生成的是 `build\conflux.exe`，按实际输出路径启动即可。

## 本地联调顺序

1. 设置 `DASHSCOPE_API_KEY` 环境变量。
2. 启动 PaperPilot 后端：`127.0.0.1:3000`。
3. 启动 Conflux 网关：`127.0.0.1:8080`。
4. 将 `frontend/` 放到 IIS 站点目录。
5. 打开站点，上传文档并提问。

## 手动验证清单

1. 打开页面，确认四步主流程正常显示。
2. 上传 `txt`、`docx`、`pptx`、`pdf` 文件，确认左侧文件列表自动追加文件项。
3. 点击左侧文件项，确认侧边栏能切换预览。
4. 拖动侧边栏右边缘，确认预览宽度可以调整。
5. 在 TXT、DOCX、PPTX 或文字型 PDF 中选择文字，确认问题区上方出现“已引用预览区选中文本”。
6. 输入问题并生成回答，确认 AI 会围绕问题和选中文本回答。
7. 打开 Conflux 面板，确认能看到健康状态、请求量、路由和指标。

## 注意事项

- 扫描版 PDF 没有内嵌文字，前端无法直接选择文字，需要 OCR 才能支持。
- 前端使用 CDN 加载 JSZip、docx-preview 和 pdf.js；如果部署环境不能访问外网，需要将依赖下载到本地并修改 `frontend/index.html`。
- 压缩包不包含生产环境 API Key、用户上传文档、索引数据、日志和构建产物。
