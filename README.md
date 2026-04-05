# PaperPilot (C MVP)

PaperPilot is a Windows-first C command-line literature companion.

Frontend prototype is now plain HTML/CSS in `frontend/`.

## Current Milestone
- CMake-based build
- CLI entry with `help`, `import`, and `ask` commands
- Import pipeline: read text and write chunked local index file
- Ask pipeline: BM25-style ranking over local index and return Top-3 chunks
- Basic unit test through CTest

## Build

```powershell
./scripts/build.ps1 -Config Debug
```

If CMake is unavailable but MinGW `gcc` exists:

```powershell
./scripts/build-gcc.ps1
```

## Run

```powershell
./build/paperpilot help
./build/paperpilot import sample.txt
./build/paperpilot importdir ./papers
./build/paperpilot ask "What is the main contribution?"
./build/paperpilot answer "What is the main contribution?"
./build/paperpilot stats
./build/paperpilot serve 8080
```

## Configuration

Create `paperpilot.conf` from `paperpilot.conf.example` and tune:
- `index_path` local index file path
- `top_k` number of returned chunks (1-10)
- `ai_enabled` set 1 to enable local AI call
- `ai_api_url` OpenAI-compatible endpoint
- `ai_model` model name
- `ai_api_key` plain token or `env:YOUR_ENV_NAME`

Aliyun DashScope example:

```ini
ai_enabled=1
ai_api_url=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
ai_model=qwen-plus
ai_api_key=env:DASHSCOPE_API_KEY
```

## Web Mode (Direct in Browser)

Start local web service:

```powershell
./build/paperpilot serve 8080
```

Or one-click PowerShell startup:

```powershell
./scripts/start-web.ps1 -Port 8080
```

With API key in same command:

```powershell
./scripts/start-web.ps1 -Port 8080 -ApiKey "your_dashscope_api_key"
```

Then open:

```text
http://127.0.0.1:8080
```

You can run all features from the page directly:
- import selected text file
- import directory by path
- ask retrieval question
- answer with AI
- show stats

## Notes
- Current retrieval is BM25-style lexical ranking for MVP.
- Next step is to upgrade scoring and support richer document formats.
