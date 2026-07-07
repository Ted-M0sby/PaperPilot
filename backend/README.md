# PaperPilot 后端

PaperPilot 后端是“智阅文献伴侣”的本地文档问答服务，使用 C 语言实现，优先支持 Windows Server 部署。

## 主要职责

- 接收前端上传的文档内容。
- 将文档内容提取为可检索文本。
- 建立并复用本地索引。
- 根据用户问题检索相关上下文。
- 调用兼容 OpenAI 的 AI 接口生成回答。
- 提供浏览器前端所需的 HTTP API。

## 当前接口

- `GET /api/health`
- `POST /api/import`
- `POST /api/importdir`
- `POST /api/import_text`
- `POST /api/ask`
- `POST /api/answer`
- `GET /api/stats`

线上项目主要使用：

- `POST /api/import_text`
- `POST /api/answer`

## 支持的上传格式

当前前端允许上传：

- `txt`
- `docx`
- `ppt`
- `pptx`
- `pdf`

后端会接收前端传来的文件内容并导入本地索引。具体文本提取逻辑位于 `src/document.c`。

## 配置

复制配置示例：

```powershell
Copy-Item .\paperpilot.conf.example .\paperpilot.conf
```

推荐配置：

```ini
index_path=./data/index.dat
top_k=8
ai_enabled=1
ai_api_url=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
ai_model=qwen3.7-plus
ai_api_key=env:DASHSCOPE_API_KEY
```

不要将真实 API Key 写入仓库。使用环境变量：

```powershell
$env:DASHSCOPE_API_KEY = "你的 API Key"
```

## 构建

推荐使用 CMake Release 构建：

```powershell
.\scripts\build.ps1 -Config Release
```

也可以使用项目内的 gcc 构建脚本：

```powershell
.\scripts\build-gcc.ps1
```

## 启动

监听 `127.0.0.1:3000`：

```powershell
.\build\Release\paperpilot.exe serve 3000
```

或参考：

```powershell
.\Start-PaperPilot.example.ps1
```

生产环境中，Conflux 会将 `/api/*` 请求转发到该服务。

## 与 Conflux 的关系

PaperPilot 后端只负责业务处理。线上请求路径为：

```text
IIS /api/*
  -> Conflux 127.0.0.1:8080
  -> PaperPilot 127.0.0.1:3000
```

Conflux 负责网关层能力，包括反向代理、路由、指标、限流和超时控制。

## 运行数据

以下目录用于运行期数据，不建议提交仓库：

- `data/`
- `logs/`
- `tmp/`
- `build/`

