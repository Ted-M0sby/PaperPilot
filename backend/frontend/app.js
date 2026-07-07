(function () {
  const MAX_FILE_SIZE = 20 * 1024 * 1024;
  const docInput = document.getElementById("doc");
  const questionInput = document.getElementById("question");
  const resultBox = document.getElementById("resultBox");
  const btnGenerate = document.getElementById("btnGenerate");
  const themeSelect = document.getElementById("themeSelect");
  const copyStatus = document.getElementById("copyStatus");
  const selectionContext = document.getElementById("selectionContext");
  const selectionContextText = document.getElementById("selectionContextText");
  const clearSelectionContext = document.getElementById("clearSelectionContext");
  const previewUi = {
    sidebar: document.getElementById("previewSidebar"),
    toggle: document.getElementById("previewToggle"),
    resizeHandle: document.getElementById("previewResizeHandle"),
    list: document.getElementById("previewFileList"),
    status: document.getElementById("previewStatus"),
    content: document.getElementById("previewContent")
  };
  const THEME_KEY = "paperpilot_theme";
  const SUPPORTED_EXTENSIONS = [".txt", ".docx", ".ppt", ".pptx", ".pdf"];
  const gatewayUi = {
    sidebar: document.getElementById("confluxSidebar"),
    toggle: document.getElementById("confluxToggle"),
    healthLight: document.getElementById("gatewayHealthLight"),
    healthText: document.getElementById("gatewayHealthText"),
    version: document.getElementById("gatewayVersion"),
    uptime: document.getElementById("gatewayUptime"),
    routeCount: document.getElementById("gatewayRouteCount"),
    updatedAt: document.getElementById("gatewayUpdatedAt"),
    totalRequests: document.getElementById("metricTotalRequests"),
    qps10: document.getElementById("metricQps10"),
    qps30: document.getElementById("metricQps30"),
    errors: document.getElementById("metricErrors"),
    upstreamAvg: document.getElementById("metricUpstreamAvg"),
    limitedIps: document.getElementById("metricLimitedIps"),
    rateLimitHits: document.getElementById("metricRateLimitHits"),
    routesList: document.getElementById("routesList"),
    error: document.getElementById("gatewayError")
  };
  let previewFiles = [];
  let activePreviewId = "";
  let previewRenderToken = 0;
  let previewResizeState = null;
  let selectedPreviewText = "";
  let importedFileId = "";
  let lastGatewayTouchToggle = 0;
  let gatewayAutoCollapseTimer = 0;

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

  function friendlyError(message) {
    const text = String(message || "").trim();
    const lower = text.toLowerCase();
    if (!text) {
      return "请求失败，请稍后重试";
    }
    if (lower === "bad gateway") {
      return "AI 服务响应超时或网关暂不可用，请稍后重试";
    }
    if (lower === "not found") {
      return "请求的接口不存在";
    }
    if (lower.includes("failed to fetch")) {
      return "网络请求失败，请检查服务器连接";
    }
    if (lower.includes("unexpected token")) {
      return "服务返回格式异常，请稍后重试";
    }
    return text;
  }

  function getFileExtension(filename) {
    const index = String(filename || "").lastIndexOf(".");
    return index >= 0 ? filename.slice(index).toLowerCase() : "";
  }

  function isSupportedFile(file) {
    return SUPPORTED_EXTENSIONS.includes(getFileExtension(file.name));
  }

  function getSelectedFile() {
    return docInput && docInput.files && docInput.files[0] ? docInput.files[0] : null;
  }

  function formatBytes(bytes) {
    const value = Number(bytes) || 0;
    if (value >= 1024 * 1024) {
      return `${(value / 1024 / 1024).toFixed(1)} MB`;
    }
    if (value >= 1024) {
      return `${Math.round(value / 1024)} KB`;
    }
    return `${value} B`;
  }

  function setPreviewStatus(message, isError) {
    if (!previewUi.status) {
      return;
    }
    previewUi.status.textContent = message;
    previewUi.status.classList.toggle("preview-error", Boolean(isError));
  }

  function clearPreviewContent() {
    if (previewUi.content) {
      previewUi.content.innerHTML = "";
    }
  }

  function updateSelectionContext() {
    if (!selectionContext || !selectionContextText) {
      return;
    }
    if (!selectedPreviewText) {
      selectionContext.classList.add("hidden");
      selectionContextText.textContent = "已引用预览区选中文本";
      return;
    }
    selectionContext.classList.remove("hidden");
    selectionContextText.textContent = `已引用预览区选中文本（${selectedPreviewText.length} 字）`;
  }

  function clearPreviewSelectionMarks() {
    if (!previewUi.content) {
      return;
    }
    previewUi.content.querySelectorAll(".preview-selection-mark").forEach((mark) => {
      mark.replaceWith(document.createTextNode(mark.textContent || ""));
    });
    previewUi.content.querySelectorAll(".preview-pdf-selection-mark").forEach((mark) => {
      mark.remove();
    });
    previewUi.content.normalize();
  }

  function clearSelectedPreviewText() {
    selectedPreviewText = "";
    clearPreviewSelectionMarks();
    updateSelectionContext();
  }

  function applySelectionMark(range) {
    clearPreviewSelectionMarks();
    const mark = document.createElement("mark");
    mark.className = "preview-selection-mark";
    try {
      range.surroundContents(mark);
    } catch (err) {
      const fragment = range.extractContents();
      mark.appendChild(fragment);
      range.insertNode(mark);
    }
  }

  function applyPdfSelectionMark(range) {
    clearPreviewSelectionMarks();
    const container = range.commonAncestorContainer.nodeType === Node.ELEMENT_NODE
      ? range.commonAncestorContainer
      : range.commonAncestorContainer.parentElement;
    const page = container ? container.closest(".preview-pdf-page") : null;
    if (!page) {
      return false;
    }
    const pageRect = page.getBoundingClientRect();
    Array.from(range.getClientRects()).forEach((rect) => {
      if (rect.width <= 0 || rect.height <= 0) {
        return;
      }
      const mark = document.createElement("span");
      mark.className = "preview-pdf-selection-mark";
      mark.style.left = `${rect.left - pageRect.left}px`;
      mark.style.top = `${rect.top - pageRect.top}px`;
      mark.style.width = `${rect.width}px`;
      mark.style.height = `${rect.height}px`;
      page.appendChild(mark);
    });
    return true;
  }

  function capturePreviewSelection() {
    if (!previewUi.content || previewResizeState) {
      return;
    }
    const selection = window.getSelection();
    if (!selection || selection.rangeCount === 0 || selection.isCollapsed) {
      return;
    }
    const range = selection.getRangeAt(0);
    if (!previewUi.content.contains(range.commonAncestorContainer)) {
      return;
    }
    const text = selection.toString().replace(/\s+/g, " ").trim();
    if (!text) {
      return;
    }
    selectedPreviewText = text.slice(0, 4000);
    if (!applyPdfSelectionMark(range)) {
      applySelectionMark(range);
    }
    selection.removeAllRanges();
    updateSelectionContext();
  }

  function buildQuestionWithSelection(question) {
    const answerStyle = "请输出约 600 字的完整回答：先给直接结论，再分段说明依据、原因、关键细节和必要补充；如果材料不足，请说明不足之处，但仍尽量基于已有内容分析。";
    if (!selectedPreviewText) {
      return `${answerStyle}\n\n用户问题：\n${question}`;
    }
    return `用户已经在预览页面中选中了下面这段原文。请优先并严格围绕这段选中文本回答，不要泛泛总结整篇文档；如果问题需要推断，请明确说明推断依据。\n\n${answerStyle}\n\n选中文本：\n${selectedPreviewText}\n\n用户问题：\n${question}`;
  }

  function setPreviewSidebarOpen(isOpen) {
    if (!previewUi.sidebar || !previewUi.toggle) {
      return;
    }
    previewUi.sidebar.classList.toggle("collapsed", !isOpen);
    previewUi.sidebar.classList.toggle("open", isOpen);
    previewUi.toggle.setAttribute("aria-expanded", String(isOpen));
  }

  function togglePreviewSidebar(event) {
    if (event) {
      event.preventDefault();
      event.stopPropagation();
    }
    const isCollapsed = previewUi.sidebar.classList.contains("collapsed");
    setPreviewSidebarOpen(isCollapsed);
  }

  function clampPreviewWidth(width) {
    const maxWidth = Math.max(300, window.innerWidth - 70);
    return Math.min(Math.max(width, 300), maxWidth);
  }

  function setPreviewWidth(width) {
    document.documentElement.style.setProperty("--preview-width", `${clampPreviewWidth(width)}px`);
  }

  function getActivePreviewEntry() {
    return previewFiles.find((entry) => entry.id === activePreviewId) || null;
  }

  function startPreviewResize(event) {
    if (isSmallViewport() || !previewUi.sidebar || !previewUi.resizeHandle) {
      return;
    }
    const point = event.touches && event.touches[0] ? event.touches[0] : event;
    const panel = previewUi.sidebar.querySelector(".preview-panel");
    if (!panel) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    previewResizeState = {
      startX: point.clientX,
      startWidth: panel.getBoundingClientRect().width
    };
    document.body.classList.add("preview-resizing");
  }

  function movePreviewResize(event) {
    if (!previewResizeState) {
      return;
    }
    const point = event.touches && event.touches[0] ? event.touches[0] : event;
    event.preventDefault();
    setPreviewWidth(previewResizeState.startWidth + point.clientX - previewResizeState.startX);
  }

  function finishPreviewResize() {
    if (!previewResizeState) {
      return;
    }
    previewResizeState = null;
    document.body.classList.remove("preview-resizing");
    const entry = getActivePreviewEntry();
    if (entry) {
      renderPreview(entry, true);
    }
  }

  function makePreviewId(file) {
    return [file.name, file.size, file.lastModified].join(":");
  }

  function makeImportId(file) {
    return makePreviewId(file);
  }

  function iconLabel(ext) {
    return ext.replace(".", "") || "file";
  }

  function renderPreviewFileList() {
    if (!previewUi.list) {
      return;
    }
    previewUi.list.innerHTML = "";
    if (!previewFiles.length) {
      const empty = document.createElement("p");
      empty.className = "preview-empty";
      empty.textContent = "选择文档后会显示在这里";
      previewUi.list.appendChild(empty);
      return;
    }
    previewFiles.forEach((entry) => {
      const item = document.createElement("button");
      const icon = document.createElement("span");
      const text = document.createElement("span");
      const name = document.createElement("span");
      const meta = document.createElement("span");
      item.type = "button";
      item.className = "preview-file-item";
      item.classList.toggle("active", entry.id === activePreviewId);
      icon.className = "preview-file-icon";
      icon.textContent = iconLabel(entry.ext);
      text.className = "preview-file-text";
      name.className = "preview-file-name";
      meta.className = "preview-file-meta";
      name.textContent = entry.file.name;
      meta.textContent = `${entry.ext.replace(".", "").toUpperCase()} · ${formatBytes(entry.file.size)}`;
      text.append(name, meta);
      item.append(icon, text);
      item.addEventListener("click", () => selectPreviewFile(entry.id));
      previewUi.list.appendChild(item);
    });
  }

  function upsertPreviewFile(file) {
    if (!file || !isSupportedFile(file)) {
      return;
    }
    const id = makePreviewId(file);
    const ext = getFileExtension(file.name);
    const existingIndex = previewFiles.findIndex((entry) => entry.id === id);
    const entry = { id, file, ext };
    if (existingIndex >= 0) {
      previewFiles[existingIndex] = entry;
    } else {
      previewFiles.push(entry);
    }
    activePreviewId = id;
    renderPreviewFileList();
    setPreviewSidebarOpen(true);
    renderPreview(entry);
  }

  function selectPreviewFile(id) {
    const entry = previewFiles.find((item) => item.id === id);
    if (!entry) {
      return;
    }
    activePreviewId = id;
    renderPreviewFileList();
    setPreviewSidebarOpen(true);
    renderPreview(entry);
  }

  function ensurePreviewStillCurrent(token) {
    return token === previewRenderToken;
  }

  async function renderTextPreview(file, token) {
    const text = await file.text();
    if (!ensurePreviewStillCurrent(token)) {
      return;
    }
    const pre = document.createElement("pre");
    pre.className = "preview-text";
    pre.textContent = text || "文件内容为空";
    clearPreviewContent();
    previewUi.content.appendChild(pre);
    setPreviewStatus("TXT 预览已加载", false);
  }

  function getDocxRenderer() {
    if (window.docx && typeof window.docx.renderAsync === "function") {
      return window.docx;
    }
    if (window.docxPreview && typeof window.docxPreview.renderAsync === "function") {
      return window.docxPreview;
    }
    return null;
  }

  async function renderDocxPreview(file, token) {
    const renderer = getDocxRenderer();
    if (!renderer) {
      throw new Error("docx-preview 未加载完成");
    }
    const wrap = document.createElement("div");
    wrap.className = "preview-docx";
    await renderer.renderAsync(await file.arrayBuffer(), wrap, null, {
      className: "docx",
      inWrapper: false,
      ignoreWidth: false,
      ignoreHeight: true,
      breakPages: false
    });
    if (!ensurePreviewStillCurrent(token)) {
      return;
    }
    clearPreviewContent();
    previewUi.content.appendChild(wrap);
    setPreviewStatus("DOCX 预览已加载", false);
  }

  async function renderPdfPage(pdf, pageNumber, token) {
    const page = await pdf.getPage(pageNumber);
    if (!ensurePreviewStillCurrent(token)) {
      return;
    }
    const canvas = document.createElement("canvas");
    const context = canvas.getContext("2d");
    const baseViewport = page.getViewport({ scale: 1 });
    const panelWidth = Math.max(240, previewUi.content.clientWidth - 4);
    const scale = panelWidth / baseViewport.width;
    const viewport = page.getViewport({ scale });
    const pageWrap = document.createElement("div");
    const textLayer = document.createElement("div");
    const textDivs = [];
    pageWrap.className = "preview-pdf-page";
    pageWrap.style.width = `${Math.floor(viewport.width)}px`;
    pageWrap.style.height = `${Math.floor(viewport.height)}px`;
    canvas.width = Math.floor(viewport.width);
    canvas.height = Math.floor(viewport.height);
    canvas.className = "preview-pdf-canvas";
    textLayer.className = "preview-pdf-text-layer";
    pageWrap.append(canvas, textLayer);
    const textContent = await page.getTextContent();
    await Promise.all([
      page.render({ canvasContext: context, viewport }).promise,
      window.pdfjsLib.renderTextLayer({
        textContentSource: textContent,
        container: textLayer,
        viewport,
        textDivs
      }).promise
    ]);
    if (!ensurePreviewStillCurrent(token)) {
      return;
    }
    return pageWrap;
  }

  async function renderPdfPreview(file, token) {
    if (!window.pdfjsLib) {
      throw new Error("pdfjs-dist 未加载完成");
    }
    if (window.pdfjsLib.GlobalWorkerOptions) {
      window.pdfjsLib.GlobalWorkerOptions.workerSrc = "https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js";
    }
    const pdf = await window.pdfjsLib.getDocument({ data: await file.arrayBuffer() }).promise;
    let pageNumber = 1;

    async function draw() {
      setPreviewStatus(`正在渲染 PDF 第 ${pageNumber} / ${pdf.numPages} 页...`, false);
      const page = await renderPdfPage(pdf, pageNumber, token);
      if (!page || !ensurePreviewStillCurrent(token)) {
        return;
      }
      const pager = document.createElement("div");
      const prev = document.createElement("button");
      const next = document.createElement("button");
      const label = document.createElement("span");
      pager.className = "preview-pager";
      prev.type = "button";
      next.type = "button";
      prev.textContent = "上一页";
      next.textContent = "下一页";
      label.textContent = `${pageNumber} / ${pdf.numPages}`;
      prev.disabled = pageNumber <= 1;
      next.disabled = pageNumber >= pdf.numPages;
      prev.addEventListener("click", () => {
        pageNumber -= 1;
        draw();
      });
      next.addEventListener("click", () => {
        pageNumber += 1;
        draw();
      });
      pager.append(prev, label, next);
      clearPreviewContent();
      previewUi.content.append(pager, page);
      setPreviewStatus("PDF 预览已加载", false);
    }

    await draw();
  }

  function decodeXmlText(value) {
    const box = document.createElement("textarea");
    box.innerHTML = String(value || "");
    return box.value;
  }

  async function renderPptPreview(file, ext, token) {
    const cover = document.createElement("div");
    const title = document.createElement("strong");
    const meta = document.createElement("p");
    const textBlock = document.createElement("pre");
    cover.className = "preview-cover";
    title.textContent = file.name;
    meta.textContent = ext === ".pptx" ? "首页内容降级预览" : "旧版 PPT 暂不支持前端解析";
    cover.append(title, meta);
    textBlock.className = "preview-text";

    if (ext === ".pptx" && window.JSZip) {
      const zip = await window.JSZip.loadAsync(await file.arrayBuffer());
      const firstSlide = zip.file("ppt/slides/slide1.xml");
      if (firstSlide) {
        const xml = await firstSlide.async("text");
        const parts = [];
        xml.replace(/<a:t>([\s\S]*?)<\/a:t>/g, (_, value) => {
          parts.push(decodeXmlText(value));
          return "";
        });
        textBlock.textContent = parts.length ? parts.join("\n") : "未提取到首页文字内容";
      } else {
        textBlock.textContent = "未找到首页幻灯片内容";
      }
    } else {
      textBlock.textContent = ext === ".pptx"
        ? "PPTX 文本提取组件未加载完成，请刷新后重试"
        : "请选择 PPTX 文件以获得首页文字预览；旧版 PPT 可继续用于问答索引。";
    }

    if (!ensurePreviewStillCurrent(token)) {
      return;
    }
    clearPreviewContent();
    previewUi.content.append(cover, textBlock);
    setPreviewStatus(ext === ".pptx" ? "PPTX 降级预览已加载" : "PPT 预览已降级", false);
  }

  async function renderPreview(entry, keepSelection) {
    if (!previewUi.content) {
      return;
    }
    const token = ++previewRenderToken;
    if (!keepSelection) {
      clearSelectedPreviewText();
    }
    clearPreviewContent();
    setPreviewStatus("正在加载预览...", false);
    try {
      if (entry.ext === ".txt") {
        await renderTextPreview(entry.file, token);
      } else if (entry.ext === ".docx") {
        await renderDocxPreview(entry.file, token);
      } else if (entry.ext === ".pdf") {
        await renderPdfPreview(entry.file, token);
      } else if (entry.ext === ".ppt" || entry.ext === ".pptx") {
        await renderPptPreview(entry.file, entry.ext, token);
      } else {
        throw new Error("暂不支持该格式预览");
      }
    } catch (err) {
      if (!ensurePreviewStillCurrent(token)) {
        return;
      }
      clearPreviewContent();
      setPreviewStatus(`预览加载失败，请重试：${friendlyError(err.message || err)}`, true);
    }
  }

  async function fileToBase64(file) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    let binary = "";
    const batchSize = 8192;
    for (let i = 0; i < bytes.length; i += batchSize) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + batchSize));
    }
    return btoa(binary);
  }

  function setText(node, value) {
    if (node) {
      node.textContent = value;
    }
  }

  function formatNumber(value, digits) {
    const number = Number(value);
    if (!Number.isFinite(number)) {
      return "--";
    }
    return number.toLocaleString("zh-CN", {
      maximumFractionDigits: typeof digits === "number" ? digits : 0
    });
  }

  function formatDuration(seconds) {
    const totalSeconds = Math.max(0, Math.floor(Number(seconds) || 0));
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    if (days > 0) {
      return `${days}天 ${hours}小时`;
    }
    if (hours > 0) {
      return `${hours}小时 ${minutes}分`;
    }
    return `${minutes}分`;
  }

  function formatLatency(value) {
    const seconds = Number(value);
    if (!Number.isFinite(seconds)) {
      return "--";
    }
    if (seconds < 1) {
      return `${formatNumber(seconds * 1000, 1)} ms`;
    }
    return `${formatNumber(seconds, 3)} s`;
  }

  function metricNameLooksLike(name, words) {
    const lower = name.toLowerCase();
    return words.every((word) => lower.includes(word));
  }

  function parsePrometheus(text) {
    const metrics = [];
    String(text || "").split(/\r?\n/).forEach((line) => {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith("#")) {
        return;
      }
      const match = trimmed.match(/^([a-zA-Z_:][a-zA-Z0-9_:]*)(?:\{([^}]*)\})?\s+(-?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)$/i);
      if (!match) {
        return;
      }
      const labels = {};
      if (match[2]) {
        match[2].replace(/([a-zA-Z_][a-zA-Z0-9_]*)="((?:\\"|[^"])*)"/g, (_, key, value) => {
          labels[key] = value.replace(/\\"/g, "\"");
          return "";
        });
      }
      metrics.push({
        name: match[1],
        labels,
        value: Number(match[3])
      });
    });
    return metrics;
  }

  function sumMetrics(metrics, predicate) {
    return metrics.reduce((total, metric) => predicate(metric) ? total + metric.value : total, 0);
  }

  function firstMetric(metrics, predicates) {
    for (const predicate of predicates) {
      const metric = metrics.find(predicate);
      if (metric) {
        return metric.value;
      }
    }
    return NaN;
  }

  function deriveGatewayMetrics(metrics) {
    const totalRequests = sumMetrics(metrics, (metric) =>
      metricNameLooksLike(metric.name, ["request"]) &&
      !metricNameLooksLike(metric.name, ["duration"]) &&
      !metricNameLooksLike(metric.name, ["latency"]) &&
      !metricNameLooksLike(metric.name, ["qps"]) &&
      !metricNameLooksLike(metric.name, ["rate_limit"])
    );
    const errors4xx = sumMetrics(metrics, (metric) =>
      metricNameLooksLike(metric.name, ["request"]) &&
      String(metric.labels.status || metric.labels.code || "").startsWith("4")
    ) || firstMetric(metrics, [(metric) => metricNameLooksLike(metric.name, ["4xx"])]);
    const errors5xx = sumMetrics(metrics, (metric) =>
      metricNameLooksLike(metric.name, ["request"]) &&
      String(metric.labels.status || metric.labels.code || "").startsWith("5")
    ) || firstMetric(metrics, [(metric) => metricNameLooksLike(metric.name, ["5xx"])]);
    const upstreamErrors = firstMetric(metrics, [
      (metric) => metricNameLooksLike(metric.name, ["upstream", "errors"]),
      (metric) => metricNameLooksLike(metric.name, ["upstream", "error"])
    ]);
    const upstreamSum = firstMetric(metrics, [
      (metric) => metricNameLooksLike(metric.name, ["upstream", "duration", "sum"]),
      (metric) => metricNameLooksLike(metric.name, ["upstream", "latency", "sum"])
    ]);
    const upstreamCount = firstMetric(metrics, [
      (metric) => metricNameLooksLike(metric.name, ["upstream", "duration", "count"]),
      (metric) => metricNameLooksLike(metric.name, ["upstream", "latency", "count"])
    ]);
    const latencyMsAvg = firstMetric(metrics, [
      (metric) => metricNameLooksLike(metric.name, ["latency", "ms", "avg"])
    ]);

    return {
      version: firstMetric(metrics, [(metric) => metricNameLooksLike(metric.name, ["version"])]),
      uptime: firstMetric(metrics, [
        (metric) => metricNameLooksLike(metric.name, ["uptime"]),
        (metric) => metricNameLooksLike(metric.name, ["start_time"])
      ]),
      totalRequests,
      qps10: firstMetric(metrics, [
        (metric) => metricNameLooksLike(metric.name, ["qps"]) && metricNameLooksLike(metric.name, ["10"]),
        (metric) => metricNameLooksLike(metric.name, ["requests", "10s"])
      ]),
      qps30: firstMetric(metrics, [
        (metric) => metricNameLooksLike(metric.name, ["qps"]) && metricNameLooksLike(metric.name, ["30"]),
        (metric) => metricNameLooksLike(metric.name, ["requests", "30s"])
      ]),
      errors: Number.isFinite(upstreamErrors) ? upstreamErrors : errors4xx + errors5xx,
      upstreamAvg: Number.isFinite(upstreamSum) && Number.isFinite(upstreamCount) && upstreamCount > 0
        ? upstreamSum / upstreamCount
        : Number.isFinite(latencyMsAvg)
          ? latencyMsAvg / 1000
          : firstMetric(metrics, [
            (metric) => metricNameLooksLike(metric.name, ["upstream", "avg"]),
            (metric) => metricNameLooksLike(metric.name, ["upstream", "average"]),
            (metric) => metricNameLooksLike(metric.name, ["upstream", "response"])
          ]),
      limitedIps: firstMetric(metrics, [
        (metric) => metricNameLooksLike(metric.name, ["limited", "ip"]),
        (metric) => metricNameLooksLike(metric.name, ["rate", "limit", "ip"])
      ]),
      rateLimitHits: firstMetric(metrics, [
        (metric) => metricNameLooksLike(metric.name, ["rate", "limit", "hit"]),
        (metric) => metricNameLooksLike(metric.name, ["limit", "trigger"])
      ])
    };
  }

  function normalizeRoutes(data) {
    if (Array.isArray(data)) {
      return data;
    }
    if (data && Array.isArray(data.routes)) {
      return data.routes;
    }
    if (data && data.data && Array.isArray(data.data.routes)) {
      return data.data.routes;
    }
    return [];
  }

  function setGatewayHealth(ok, message) {
    if (!gatewayUi.healthLight || !gatewayUi.healthText) {
      return;
    }
    gatewayUi.healthLight.classList.toggle("ok", ok);
    gatewayUi.healthLight.classList.toggle("bad", !ok);
    gatewayUi.healthText.textContent = ok ? "正常" : "异常";
    setText(gatewayUi.error, ok ? "" : message);
  }

  function renderRoutes(routes) {
    if (!gatewayUi.routesList) {
      return;
    }
    gatewayUi.routesList.innerHTML = "";
    if (!routes.length) {
      const empty = document.createElement("p");
      empty.className = "empty-routes";
      empty.textContent = "暂无路由数据";
      gatewayUi.routesList.appendChild(empty);
      return;
    }
    routes.forEach((route, index) => {
      const prefix = route.prefix || route.path || route.match || "/";
      const upstream = route.upstream || route.target || route.backend || "--";
      const policy = route.lb_policy || route.lbPolicy || route.policy || "first";
      const details = document.createElement("details");
      const summary = document.createElement("summary");
      const body = document.createElement("div");
      const upstreamRow = document.createElement("div");
      const upstreamCode = document.createElement("code");
      const policyRow = document.createElement("div");
      const policyCode = document.createElement("code");
      details.className = "route-item";
      details.open = index === 0;
      summary.textContent = prefix;
      body.className = "route-body";
      upstreamRow.append("upstream");
      upstreamCode.textContent = upstream;
      upstreamRow.appendChild(upstreamCode);
      policyRow.append("lb_policy");
      policyCode.textContent = policy;
      policyRow.appendChild(policyCode);
      body.append(upstreamRow, policyRow);
      details.append(summary, body);
      gatewayUi.routesList.appendChild(details);
    });
  }

  function updateGatewayPanel(metrics, routes) {
    const derived = deriveGatewayMetrics(metrics);
    const versionMetric = metrics.find((metric) => metricNameLooksLike(metric.name, ["version"]));
    const versionLabel = versionMetric && (versionMetric.labels.version || versionMetric.labels.semver);
    const uptimeMetric = metrics.find((metric) => metricNameLooksLike(metric.name, ["uptime"]) || metricNameLooksLike(metric.name, ["start_time"]));
    const isStartTime = uptimeMetric && metricNameLooksLike(uptimeMetric.name, ["start_time"]);
    const uptimeSeconds = isStartTime ? Date.now() / 1000 - uptimeMetric.value : derived.uptime;

    setText(gatewayUi.version, versionLabel || (Number.isFinite(derived.version) ? String(derived.version) : "C++20"));
    setText(gatewayUi.uptime, formatDuration(uptimeSeconds));
    setText(gatewayUi.routeCount, formatNumber(routes.length));
    setText(gatewayUi.updatedAt, new Date().toLocaleTimeString("zh-CN", { hour12: false }));
    setText(gatewayUi.totalRequests, formatNumber(derived.totalRequests));
    setText(gatewayUi.qps10, formatNumber(derived.qps10, 2));
    setText(gatewayUi.qps30, formatNumber(derived.qps30, 2));
    setText(gatewayUi.errors, formatNumber(derived.errors));
    setText(gatewayUi.upstreamAvg, formatLatency(derived.upstreamAvg));
    setText(gatewayUi.limitedIps, formatNumber(derived.limitedIps));
    setText(gatewayUi.rateLimitHits, formatNumber(derived.rateLimitHits));
    renderRoutes(routes);
    setGatewayHealth(true, "");
  }

  async function refreshGatewayPanel() {
    if (!gatewayUi.sidebar) {
      return;
    }
    try {
      const metricsRes = await fetch(apiUrl("/metrics"), { cache: "no-store" });
      if (!metricsRes.ok) {
        throw new Error(`/metrics HTTP ${metricsRes.status}`);
      }
      const metrics = parsePrometheus(await metricsRes.text());
      let routes = [];
      try {
        const routesRes = await fetch(apiUrl("/admin/routes"), { cache: "no-store" });
        if (!routesRes.ok) {
          throw new Error(`/admin/routes HTTP ${routesRes.status}`);
        }
        routes = normalizeRoutes(await routesRes.json());
      } catch (err) {
      }
      updateGatewayPanel(metrics, routes);
    } catch (err) {
      setGatewayHealth(false, friendlyError(err.message || err));
      setText(gatewayUi.updatedAt, new Date().toLocaleTimeString("zh-CN", { hour12: false }));
    }
  }

  function isSmallViewport() {
    return window.matchMedia("(max-width: 767px)").matches;
  }

  function clearGatewayAutoCollapse() {
    if (gatewayAutoCollapseTimer) {
      window.clearTimeout(gatewayAutoCollapseTimer);
      gatewayAutoCollapseTimer = 0;
    }
  }

  function scheduleGatewayAutoCollapse() {
    clearGatewayAutoCollapse();
    gatewayAutoCollapseTimer = window.setTimeout(() => {
      setGatewaySidebarOpen(false);
    }, 60000);
  }

  function setGatewaySidebarOpen(isOpen) {
    if (!gatewayUi.sidebar || !gatewayUi.toggle) {
      return;
    }
    gatewayUi.sidebar.classList.toggle("collapsed", !isOpen);
    gatewayUi.sidebar.classList.toggle("open", isOpen);
    gatewayUi.toggle.setAttribute("aria-expanded", String(isOpen));
    if (isOpen) {
      scheduleGatewayAutoCollapse();
    } else {
      clearGatewayAutoCollapse();
    }
  }

  function toggleGatewaySidebar(event) {
    if (event) {
      if (event.type === "click" && Date.now() - lastGatewayTouchToggle < 500) {
        return;
      }
      if (event.type === "touchend") {
        lastGatewayTouchToggle = Date.now();
      }
      event.preventDefault();
      event.stopPropagation();
    }
    const isCollapsed = gatewayUi.sidebar.classList.contains("collapsed");
    setGatewaySidebarOpen(isCollapsed);
  }

  async function apiPost(path, payload) {
    const url = apiUrl(path);
    const res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    const text = await res.text();
    let data;
    try {
      data = text ? JSON.parse(text) : {};
    } catch (err) {
      throw new Error(friendlyError(text || "服务返回了非 JSON 响应"));
    }
    if (!res.ok || !data.ok) {
      throw new Error(friendlyError(data.output || data.message || "请求失败"));
    }
    return data;
  }

  async function doGenerate() {
    const file = getSelectedFile();
    const question = questionInput && questionInput.value.trim() ? questionInput.value.trim() : "";

    if (!file) {
      setStatus("请先选择 txt、docx、ppt 或 pdf 文件", true);
      return;
    }
    if (!isSupportedFile(file)) {
      setStatus("仅支持 txt、docx、ppt、pdf 文件", true);
      return;
    }
    if (file.size <= 0) {
      setStatus("文件为空，请选择有内容的文档", true);
      return;
    }
    if (file.size > MAX_FILE_SIZE) {
      setStatus("文件过大，请使用小于 20MB 的文档", true);
      return;
    }
    if (!question) {
      setStatus("请先输入问题", true);
      return;
    }

    try {
      const ext = getFileExtension(file.name);
      const importId = makeImportId(file);
      if (importedFileId !== importId) {
        const payload = { filename: file.name };
        if (ext === ".txt") {
          const text = await file.text();
          if (!text || !text.trim()) {
            setStatus("文件内容为空，请更换文档", true);
            return;
          }
          payload.content = text;
        } else {
          payload.contentBase64 = await fileToBase64(file);
        }

        setStatus("正在导入文档...", false);
        await apiPost("/api/import_text", payload);
        importedFileId = importId;
      } else {
        setStatus("已使用当前文档索引，跳过重复导入", false);
      }

      setStatus(selectedPreviewText ? "正在基于选中文本请求 AI 回答..." : "正在请求 AI 回答...", false);
      const data = await apiPost("/api/answer", { question: buildQuestionWithSelection(question) });
      setResult(data.output || "AI 回答完成");
      setStatus("AI 回答完成", false);
    } catch (err) {
      setStatus("AI 回答失败", true);
      setResult(friendlyError(err.message || err));
    }
  }

  if (btnGenerate) {
    btnGenerate.addEventListener("click", doGenerate);
  }

  if (docInput) {
    docInput.addEventListener("change", () => {
      const file = docInput.files && docInput.files[0] ? docInput.files[0] : null;
      if (!file) {
        return;
      }
      if (!isSupportedFile(file)) {
        setStatus("仅支持 txt、docx、ppt、pptx、pdf 文件", true);
        return;
      }
      importedFileId = "";
      upsertPreviewFile(file);
    });
  }

  if (previewUi.toggle && previewUi.sidebar) {
    previewUi.toggle.addEventListener("click", togglePreviewSidebar);
    previewUi.sidebar.addEventListener("click", (event) => {
      event.stopPropagation();
    });
    document.addEventListener("click", () => {
      if (isSmallViewport()) {
        setPreviewSidebarOpen(false);
      }
    });
  }

  if (previewUi.resizeHandle) {
    previewUi.resizeHandle.addEventListener("mousedown", startPreviewResize);
    previewUi.resizeHandle.addEventListener("touchstart", startPreviewResize, { passive: false });
    document.addEventListener("mousemove", movePreviewResize);
    document.addEventListener("touchmove", movePreviewResize, { passive: false });
    document.addEventListener("mouseup", finishPreviewResize);
    document.addEventListener("touchend", finishPreviewResize);
    window.addEventListener("resize", () => {
      if (!isSmallViewport()) {
        const panel = previewUi.sidebar && previewUi.sidebar.querySelector(".preview-panel");
        if (panel) {
          setPreviewWidth(panel.getBoundingClientRect().width);
        }
      }
    });
  }

  if (previewUi.content) {
    previewUi.content.addEventListener("mouseup", () => {
      window.setTimeout(capturePreviewSelection, 0);
    });
    previewUi.content.addEventListener("touchend", () => {
      window.setTimeout(capturePreviewSelection, 80);
    });
  }

  if (clearSelectionContext) {
    clearSelectionContext.addEventListener("click", clearSelectedPreviewText);
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

  if (gatewayUi.toggle && gatewayUi.sidebar) {
    gatewayUi.toggle.addEventListener("click", toggleGatewaySidebar);
    gatewayUi.toggle.addEventListener("touchend", toggleGatewaySidebar, { passive: false });
    gatewayUi.sidebar.addEventListener("click", (event) => {
      event.stopPropagation();
    });
    gatewayUi.sidebar.addEventListener("touchend", (event) => {
      event.stopPropagation();
    }, { passive: true });
    document.addEventListener("click", () => {
      if (isSmallViewport()) {
        setGatewaySidebarOpen(false);
      }
    });
    document.addEventListener("touchend", () => {
      if (isSmallViewport()) {
        setGatewaySidebarOpen(false);
      }
    }, { passive: true });
  }

  try {
    applyTheme(localStorage.getItem(THEME_KEY) || "aurora");
  } catch (err) {
    applyTheme("aurora");
  }

  setStatus("等待操作", false);
  refreshGatewayPanel();
  window.setInterval(refreshGatewayPanel, 3000);
})();
