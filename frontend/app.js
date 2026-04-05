(function () {
  const MAX_FILE_SIZE = 20 * 1024 * 1024;
  const docInput = document.getElementById("doc");
  const questionInput = document.getElementById("question");
  const resultBox = document.getElementById("resultBox");
  const btnGenerate = document.getElementById("btnGenerate");
  const themeSelect = document.getElementById("themeSelect");
  const copyStatus = document.getElementById("copyStatus");
  const THEME_KEY = "paperpilot_theme";

  function apiUrl(path) {
    return path;
  }

  function setStatus(message, isError) {
    if (!copyStatus) {
      return;
    }
    copyStatus.textContent = message;
    copyStatus.style.color = isError ? "#b00020" : "#59607a";
  }

  function setResult(text) {
    if (resultBox) {
      resultBox.value = text || "";
    }
  }

  function applyTheme(theme) {
    const safeTheme = theme || "aurora";
    document.body.setAttribute("data-theme", safeTheme);
    if (themeSelect) {
      themeSelect.value = safeTheme;
    }
  }

  async function apiPost(path, payload) {
    const url = apiUrl(path);
    const res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok || !data.ok) {
      throw new Error(data.output || data.message || "请求失败");
    }
    return data;
  }

  async function doGenerate() {
    const file = docInput && docInput.files && docInput.files[0] ? docInput.files[0] : null;
    const question = questionInput && questionInput.value.trim() ? questionInput.value.trim() : "";

    if (!file) {
      setStatus("请先选择 txt 文件", true);
      return;
    }
    if (file.size <= 0) {
      setStatus("文件为空，请选择有内容的 txt 文件", true);
      return;
    }
    if (file.size > MAX_FILE_SIZE) {
      setStatus("文件过大，请使用小于 20MB 的 txt 文件", true);
      return;
    }
    if (!question) {
      setStatus("请先输入问题", true);
      return;
    }

    try {
      const text = await file.text();
      if (!text || !text.trim()) {
        setStatus("文件内容为空，请更换文档", true);
        return;
      }
      setStatus("正在导入文档...", false);
      await apiPost("/api/import_text", {
        filename: file.name,
        content: text
      });
      setStatus("正在请求 AI 回答...", false);
      const data = await apiPost("/api/answer", { question });
      setResult(data.output || "AI 回答完成");
      setStatus("AI 回答完成", false);
    } catch (err) {
      setStatus("AI 回答失败", true);
      setResult(String(err.message || err));
    }
  }

  if (btnGenerate) {
    btnGenerate.addEventListener("click", doGenerate);
  }

  if (themeSelect) {
    themeSelect.addEventListener("change", () => {
      const selected = themeSelect.value || "aurora";
      applyTheme(selected);
      try {
        localStorage.setItem(THEME_KEY, selected);
      } catch (err) {
      }
    });
  }

  try {
    applyTheme(localStorage.getItem(THEME_KEY) || "aurora");
  } catch (err) {
    applyTheme("aurora");
  }

  setStatus("等待操作", false);
})();
