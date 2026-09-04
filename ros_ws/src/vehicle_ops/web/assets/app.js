const $ = (id) => document.getElementById(id);
const state = { mobilityPlugins: [], modelCatalog: { mobility: { plugins: [], active_plugin_id: "" }, models: [] }, datasetCatalog: { episodes: [], exports: [], lerobot: [], archives: [] }, datasetFailureLogJobId: "", datasetFailureLog: "", selectedLerobotPath: "", token: sessionStorage.getItem("vehicleOpsToken") || "", cameraSequence: 0, pipelineCameraSequence: 0, cameraStatus: null, pipelineCameraActive: false, selectedJobId: "", jobs: [], debugRunId: "", debugResult: null, debugImageUrls: {}, pipelineTrace: null, pipelineHistory: [], selectedPipelineStageId: "", selectedPipelineHistoryId: "", inspectorMode: "live", components: [], componentProfiles: [], selectedComponentId: "", storage: null, storageItems: [], selectedStorageCategory: "", systemMode: "", policyStatus: null, systemStatus: null, runtimeSwitchJobId: "", runtimeSwitchTarget: "", runtimeSwitchNotified: false, debugInputSource: "camera", debugUpload: null, observationWidth: 640, observationHeight: 480, statusRequestActive: false };
const text = (id, value) => { $(id).textContent = value ?? "—"; };
const number = (value, digits = 1) => Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : "--";
const milliseconds = (value) => {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) return "--";
  return numeric > 0 && numeric < 0.1 ? numeric.toFixed(3) : numeric.toFixed(1);
};
function setMetricTooltip(valueId, heading, rows) {
  const value = $(valueId);
  const card = value?.closest(".metric");
  if (!card) return;
  const details = [heading, ...rows.filter(Boolean)].join("\n");
  card.dataset.tooltip = details;
  card.tabIndex = 0;
  card.setAttribute("aria-label", details.replaceAll("\n", "，"));
}
function initMetricTooltip() {
  const tooltip = document.createElement("div");
  tooltip.id = "metricTooltip";
  tooltip.className = "metric-tooltip";
  tooltip.setAttribute("role", "tooltip");
  document.body.appendChild(tooltip);
  let activeCard = null;
  const hide = () => {
    activeCard = null;
    tooltip.classList.remove("visible");
  };
  const position = (card) => {
    if (!card?.dataset.tooltip) return;
    activeCard = card;
    tooltip.textContent = card.dataset.tooltip;
    tooltip.classList.add("visible");
    const cardRect = card.getBoundingClientRect();
    const tooltipRect = tooltip.getBoundingClientRect();
    const margin = 12;
    const gap = 10;
    const left = Math.min(
      Math.max(cardRect.left + cardRect.width / 2 - tooltipRect.width / 2, margin),
      window.innerWidth - tooltipRect.width - margin
    );
    let top = cardRect.bottom + gap;
    if (top + tooltipRect.height > window.innerHeight - margin) {
      top = cardRect.top - tooltipRect.height - gap;
    }
    tooltip.style.left = `${left}px`;
    tooltip.style.top = `${Math.max(margin, top)}px`;
  };
  document.addEventListener("pointerover", (event) => {
    const card = event.target.closest?.(".metric[data-tooltip]");
    if (card) position(card);
  });
  document.addEventListener("pointerout", (event) => {
    const card = event.target.closest?.(".metric[data-tooltip]");
    if (card && !card.contains(event.relatedTarget)) hide();
  });
  document.addEventListener("focusin", (event) => {
    const card = event.target.closest?.(".metric[data-tooltip]");
    if (card) position(card);
  });
  document.addEventListener("focusout", (event) => {
    const card = event.target.closest?.(".metric[data-tooltip]");
    if (card && !card.contains(event.relatedTarget)) hide();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") hide();
  });
  window.addEventListener("resize", () => activeCard && position(activeCard));
  window.addEventListener("scroll", () => activeCard && position(activeCard), true);
}
function toast(message, error = false) {
  const node = $("toast");
  node.textContent = message;
  node.className = `toast show${error ? " error" : ""}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => node.className = "toast", 3200);
}
function openToken() {
  $("tokenPanel").classList.add("open");
  $("panelBackdrop").classList.add("open");
  $("tokenInput").value = state.token;
}
function closeToken() {
  $("tokenPanel").classList.remove("open");
  $("panelBackdrop").classList.remove("open");
}
async function post(path, body = "") {
  if (!state.token) { openToken(); throw new Error("请先填写操作令牌"); }
  const response = await fetch(path, { method: "POST", headers: { "Content-Type": "text/plain;charset=UTF-8", "X-Ops-Token": state.token }, body });
  const result = await response.json();
  if (!response.ok || !result.success) { throw new Error(result.message || `HTTP ${response.status}`); }
  return result;
}
function freshness(item) {
  if (!item?.received) return "OFFLINE";
  return item.fresh ? "ONLINE" : "STALE";
}
function setView(name) {
  document.querySelectorAll(".view-panel").forEach((panel) => panel.classList.toggle("active", panel.id === `view-${name}`));
  document.querySelectorAll("[data-view]").forEach((button) => button.classList.toggle("active", button.dataset.view === name));
  history.replaceState(null, "", `#${name}`);
  if (name === "storage") refreshStorage(true);
  if (name === "tools" || name === "capture") refreshDatasets(true);
}
function badge(node, healthy, yes, no) {
  node.className = `pill ${healthy ? "success" : "warning"}`;
  node.innerHTML = `<i></i>${healthy ? yes : no}`;
}
function frameTime(value) {
  if (!Number.isFinite(Number(value)) || Number(value) <= 0) return "--:--:--.---";
  return new Date(Number(value)).toLocaleTimeString("zh-CN", {
    hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit", fractionalSecondDigits: 3
  });
}
function renderCameraView(camera, ids, sequenceKey, enabled = true) {
  const received = Boolean(camera?.received && enabled);
  $(ids.overlay).classList.toggle("visible", received);
  $(ids.overlay).classList.toggle("stale", received && !camera?.fresh);
  text(ids.label, camera?.content_analyzed && !camera?.content_valid ? "黑帧异常" : (camera?.fresh ? "实时快照" : "最后快照"));
  $(ids.image).style.display = received ? "block" : "none";
  $(ids.empty).style.display = received ? "none" : "grid";
  const sequence = Number(camera?.frame_sequence || 0);
  const image = $(ids.image);
  if (!received || sequence <= 0 || sequence === state[sequenceKey] || image.dataset.loading === "true") return;
  state[sequenceKey] = sequence;
  image.dataset.loading = "true";
  image.onload = () => {image.dataset.loading = "false";};
  image.onerror = () => {
    image.dataset.loading = "false";
    if (state[sequenceKey] === sequence) state[sequenceKey] = 0;
  };
  image.src = `/api/camera/front.jpg?t=${sequence}`;
  text(ids.time, frameTime(camera.frame_received_at_ms));
  text(ids.sequence, `帧 #${sequence}`);
  $(ids.overlay).classList.remove("updated");
  void $(ids.overlay).offsetWidth;
  $(ids.overlay).classList.add("updated");
}
function updateDebugModeGuard(system) {
  const mode = system?.mode_name || "UNKNOWN";
  const received = Boolean(system?.received);
  const shadowReady = received && mode === "VLA_SHADOW";
  const assistedReady = received && mode === "VLA_ASSISTED";
  const policyReady = state.policyStatus?.state_name === "READY";
  state.systemMode = mode;
  $("debugModeGuard").dataset.state = shadowReady || assistedReady ? "ready" : (received ? "blocked" : "unknown");
  text("debugModeState", shadowReady ? "VLA_SHADOW \u00b7 \u9884\u89c8\u6a21\u5f0f" : assistedReady ? "VLA_ASSISTED \u00b7 \u5355\u6b65\u6267\u884c\u6a21\u5f0f" : `${mode} \u00b7 \u5c1a\u672a\u9009\u62e9\u8c03\u8bd5\u6a21\u5f0f`);
  text("debugModeHint", shadowReady ? (policyReady ? "\u53ef\u51bb\u7ed3 Observation\u3001\u9884\u5904\u7406\u548c\u63a8\u7406\uff1b\u786e\u8ba4\u8f93\u51fa\u540e\u53ef\u8fdb\u5165\u5355\u6b65\u6a21\u5f0f" : "\u5f71\u5b50\u6a21\u5f0f\u53ef\u6b63\u5e38\u9884\u89c8\uff1b\u5f53\u524d Provider \u6216\u6a21\u578b\u5c1a\u672a\u5c31\u7eea\uff0c\u6682\u4e0d\u80fd\u8fdb\u5165\u5355\u6b65\u6a21\u5f0f") : assistedReady ? "\u53ef\u6267\u884c\u5df2\u786e\u8ba4\u7684\u5355\u6b65\u547d\u4ee4\uff1b\u547d\u4ee4\u4ecd\u7ecf\u8fc7 Control Mux \u548c Safety Guard" : "\u5148\u8fdb\u5165\u5f71\u5b50\u6a21\u5f0f\u8fdb\u884c\u9884\u89c8\uff0c\u786e\u8ba4\u6a21\u578b\u548c\u63d2\u4ef6\u8f93\u51fa\u540e\u518d\u8fdb\u5165\u5355\u6b65\u6267\u884c\u6a21\u5f0f");
  $("enterShadowMode").disabled = !received || (shadowReady && !policyReady);
  $("enterShadowMode").textContent = shadowReady ? (policyReady ? "\u8fdb\u5165\u5355\u6b65\u6267\u884c\u6a21\u5f0f" : "\u7b49\u5f85\u7b56\u7565\u5c31\u7eea") : assistedReady ? "\u8fd4\u56de\u5f71\u5b50\u6a21\u5f0f" : "\u8fdb\u5165\u5f71\u5b50\u8c03\u8bd5\u6a21\u5f0f";
  const executeControl = $("debugExecutionControl");
  if (executeControl) executeControl.style.display = assistedReady ? "grid" : "none";
  const executeToggle = $("debugExecuteCommand");
  if (executeToggle) {
    executeToggle.disabled = !assistedReady;
    if (!assistedReady) executeToggle.checked = false;
  }
}
function policyRuntimeType(policy) {
  const provider = String(policy?.provider_id || "").toLowerCase();
  if (provider.includes("smolvla")) return "smolvla";
  if (provider.includes("mock")) return "mock";
  return "unknown";
}
function updatePolicyRuntimeCard(policy = state.policyStatus, system = state.systemStatus) {
  state.policyStatus = policy || null;
  state.systemStatus = system || null;
  const current = policyRuntimeType(policy);
  const switching = Boolean(state.runtimeSwitchJobId);
  const shadowReady = system?.mode_name === "VLA_SHADOW";
  const card = $("policyRuntimeCard");
  card.dataset.state = switching ? "switching" : (policy?.state_name === "READY" ? "ready" : "warning");
  text("policyRuntimeName", switching
    ? `正在切换到 ${state.runtimeSwitchTarget === "smolvla" ? "SmolVLA" : "Mock"}`
    : `${policy?.provider_id || "Provider 未连接"} / ${policy?.model_id || "--"}`);
  text("policyRuntimeHint", switching ? "正在停止旧 Runtime、启动目标 Runtime 并等待健康检查"
    : (!shadowReady ? "请先进入 VLA_SHADOW 模式后再切换"
      : (policy?.message || "切换只影响 Shadow 推理，不发布底盘控制命令")));
  document.querySelectorAll("[data-runtime-provider]").forEach((button) => {
    const selected = button.dataset.runtimeProvider === current;
    button.disabled = switching || !shadowReady || selected;
    button.textContent = selected
      ? (current === "smolvla" ? "SmolVLA 使用中" : "Mock 使用中")
      : (button.dataset.runtimeProvider === "smolvla" ? "切换 SmolVLA" : "切换 Mock");
  });
}
function render(data) {
  badge($("connectionBadge"), true, "API ONLINE", "连接中");
  state.policyStatus = data.policy || null;
  state.systemStatus = data.system || null;
  updatePolicyRuntimeCard(data.policy, data.system);
  text("modeName", data.system?.mode_name || "NO STATE");
  updateDebugModeGuard(data.system);
  text("systemMessage", data.system?.message || "尚未收到 Supervisor 状态");
  text("detailSupervisor", data.system?.mode_name ? `${data.system.mode_name} · ${data.system.control_source || "--"}` : "OFFLINE");
  text("detailObservation", data.observation?.ready ? "READY" : freshness(data.observation));
  text("detailPolicy", data.policy?.state_name || freshness(data.policy));
  $("modeOrb").classList.toggle("danger", ["SAFE_STOP", "FAULT"].includes(data.system?.mode_name));
  text("observationState", data.observation?.ready ? "READY" : freshness(data.observation));
  text("observationMeta", data.observation?.message || "等待 Observation Monitor");
  text("policyState", data.policy?.state_name || freshness(data.policy));
  text("policyMeta", data.policy?.model_id || data.policy?.message || "等待 Provider");
  text("episodeState", data.episode?.state_name || freshness(data.episode));
  text("episodeMeta", data.episode?.message || "等待 Episode Recorder");
  state.observationWidth = Number(data.observation?.image_width || 640);
  state.observationHeight = Number(data.observation?.image_height || 480);
  const totalMemory = Number(data.host?.memory_total_mb || 0);
  const usedMemory = totalMemory - Number(data.host?.memory_available_mb || 0);
  const memoryRatio = totalMemory ? usedMemory / totalMemory * 100 : NaN;
  const swapUsed = Number(data.host?.swap_total_mb || 0) - Number(data.host?.swap_free_mb || 0);
  text("cpuState", `${number(data.host?.cpu_usage_percent, 0)}%`);
  text("cpuMeta", `${data.host?.cpu_cores || "--"} 核 · ${number(data.host?.cpu_temperature_c, 1)}°C · Load ${number(data.host?.load_one, 2)}`);
  text("memoryState", Number.isFinite(memoryRatio) ? `${memoryRatio.toFixed(0)}%` : "--");
  text("hostMeta", `${number(usedMemory / 1024, 1)} / ${number(totalMemory / 1024, 1)} GB · Swap ${number(swapUsed / 1024, 1)} GB`);
  const gpuAvailable = data.host?.gpu_available === true;
  text("gpuState", gpuAvailable ? `${number(data.host?.gpu_usage_percent, 0)}%` : "N/A");
  text("gpuMeta", gpuAvailable
    ? `${number(data.host?.gpu_frequency_mhz, 0)} MHz · ${number(data.host?.gpu_temperature_c, 1)}°C · ${data.host?.gpu_memory_mode || "unknown"}`
    : "当前平台未提供 GPU 指标");
  setMetricTooltip("observationState", "Observation / 观测输入", [
    `状态：${$("observationState").textContent}`,
    `说明：${$("observationMeta").textContent}`,
    `图像：${data.observation?.image_width || "--"} × ${data.observation?.image_height || "--"}`
  ]);
  setMetricTooltip("policyState", "Policy Runtime / 策略运行时", [
    `状态：${$("policyState").textContent}`,
    `Provider：${data.policy?.provider_id || "--"}`,
    `模型：${data.policy?.model_id || "--"}`,
    `推理延迟：${number(data.policy?.inference_latency_ms, 1)} ms`,
    `说明：${data.policy?.message || "--"}`
  ]);
  setMetricTooltip("episodeState", "Episode / 数据记录", [
    `状态：${$("episodeState").textContent}`,
    `Episode：${data.episode?.episode_id || "未开始"}`,
    `任务：${data.episode?.task || "--"}`,
    `消息 / 图像：${data.episode?.message_count || 0} / ${data.episode?.image_count || 0}`,
    `说明：${data.episode?.message || "--"}`
  ]);
  setMetricTooltip("cpuState", "处理器 CPU", [
    `使用率：${number(data.host?.cpu_usage_percent, 0)}%`,
    `核心数：${data.host?.cpu_cores || "--"}`,
    `温度：${number(data.host?.cpu_temperature_c, 1)}°C`,
    `一分钟负载：${number(data.host?.load_one, 2)}`
  ]);
  setMetricTooltip("memoryState", "统一内存 RAM", [
    `使用率：${Number.isFinite(memoryRatio) ? memoryRatio.toFixed(0) : "--"}%`,
    `已用 / 总量：${number(usedMemory / 1024, 1)} / ${number(totalMemory / 1024, 1)} GB`,
    `Swap 已用：${number(swapUsed / 1024, 1)} GB`,
    `GPU 内存模式：${data.host?.gpu_memory_mode || "unknown"}`
  ]);
  setMetricTooltip("gpuState", "图形处理器 GPU", [
    `可用：${gpuAvailable ? "是" : "否"}`,
    `指标后端：${data.host?.gpu_backend || "unavailable"}`,
    `使用率：${gpuAvailable ? `${number(data.host?.gpu_usage_percent, 0)}%` : "N/A"}`,
    `频率：${gpuAvailable ? `${number(data.host?.gpu_frequency_mhz, 0)} MHz` : "N/A"}`,
    `温度：${gpuAvailable ? `${number(data.host?.gpu_temperature_c, 1)}°C` : "N/A"}`,
    `显存模式：${data.host?.gpu_memory_mode || "unknown"}`
  ]);
  text("cameraResolution", `${data.observation?.image_width || "--"} × ${data.observation?.image_height || "--"}`);
  text("cameraAge", data.camera?.age_ms >= 0 ? `更新 ${number(data.camera.age_ms / 1000, 1)}s 前` : "更新时间 --");
  text("calibrationState", data.observation?.camera_calibrated ? "已标定" : "未标定");
  const cameraReceived = Boolean(data.camera?.received);
  const cameraContentValid = !data.camera?.content_analyzed || data.camera?.content_valid;
  state.cameraStatus = data.camera || null;
  badge($("cameraBadge"), Boolean(data.camera?.fresh && cameraContentValid), "实时快照",
    data.camera?.content_analyzed && !data.camera?.content_valid ? "黑帧异常" : (cameraReceived ? "画面停滞" : "无画面"));
  renderCameraView(data.camera, {
    image: "cameraImage", empty: "cameraEmpty", overlay: "cameraLiveOverlay",
    label: "cameraLiveLabel", time: "cameraFrameTime", sequence: "cameraFrameSequence"
  }, "cameraSequence");
  renderCameraView(data.camera, {
    image: "pipelineLiveImage", empty: "pipelineLiveEmpty", overlay: "pipelineCameraLiveOverlay",
    label: "pipelineCameraLiveLabel", time: "pipelineCameraFrameTime", sequence: "pipelineCameraFrameSequence"
  }, "pipelineCameraSequence", state.pipelineCameraActive);
  const recording = data.episode?.state_name === "RECORDING";
  $("recordDot").classList.toggle("active", recording);
  text("currentEpisode", data.episode?.episode_id || "—");
  text("episodeElapsed", `${number(data.episode?.elapsed_seconds || 0, 1)} s`);
  text("episodeCounts", `${data.episode?.message_count || 0} / ${data.episode?.image_count || 0}`);
  text("shadowSamples", data.shadow?.sample_count || 0);
  text("linearMae", number(data.shadow?.linear_mae, 4));
  text("angularMae", number(data.shadow?.angular_mae, 4));
  text("safetyState", data.safety?.active ? "ACTIVE STOP" : "NORMAL");
  $("safetyState").style.color = data.safety?.active ? "var(--danger)" : "var(--lime)";
  text("lastUpdate", `最后更新 ${new Date().toLocaleTimeString()}`);
}async function refresh() {
  if (state.statusRequestActive) return;
  state.statusRequestActive = true;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  try {
    const response = await fetch("/api/status", { cache: "no-store", signal: controller.signal });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch (error) {
    badge($("connectionBadge"), false, "API ONLINE", "API OFFLINE");
    text("systemMessage", error.name === "AbortError" ? "????????????" : error.message);
  } finally {
    clearTimeout(timeout);
    state.statusRequestActive = false;
  }
}
$("episodeForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    const body = [$("episodeId").value, $("episodeTask").value, $("operatorId").value].join("\u001f");
    const result = await post("/api/episode/start", body);
    toast(result.message); refresh();
  } catch (error) { toast(error.message, true); }
});
$("stopEpisode").addEventListener("click", async () => {
  try { const result = await post("/api/episode/stop"); toast(result.message); refresh(); }
  catch (error) { toast(error.message, true); }
});
$("taskForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  try { const result = await post("/api/task", $("taskText").value); toast(result.message); }
  catch (error) { toast(error.message, true); }
});
$("safeStop").addEventListener("click", async () => {
  if (!confirm("确认请求 SAFE_STOP？车辆 Supervisor 将切换到安全停车模式。")) return;
  try { const result = await post("/api/safe-stop", `vehicle_ops_console\u001f${$("safeReason").value}`); toast(result.message); refresh(); }
  catch (error) { toast(error.message, true); }
});
document.querySelectorAll("[data-view]").forEach((button) => button.addEventListener("click", () => setView(button.dataset.view)));
document.querySelectorAll("[data-open-view]").forEach((button) => button.addEventListener("click", () => setView(button.dataset.openView)));
$("refreshDatasets")?.addEventListener("click", () => refreshDatasets(true));
$("tokenButton").addEventListener("click", openToken);
$("tokenClose").addEventListener("click", closeToken);
$("panelBackdrop").addEventListener("click", closeToken);
$("saveToken").addEventListener("click", () => {
  state.token = $("tokenInput").value.trim();
  sessionStorage.setItem("vehicleOpsToken", state.token);
  closeToken(); toast("操作令牌已保存到当前浏览器会话");
});
function commandFor(type, input, output) {
  if (type === "dataset.inspect") return `./scripts/inspect_dataset.sh \
  ${input} \
  ${output}`;
  if (type === "dataset.split") return `./scripts/split_vehicle_dataset.sh \
  ${output} \
  ${input}`;
  if (type === "dataset.convert_lerobot") return `./scripts/convert_lerobot_dataset.sh \
  ${output} \
  ${input}`;
  return "";
}
function updateCommand() {
  $("generatedCommand").textContent = commandFor(
    $("datasetTool").value, $("datasetInput").value.trim(), $("datasetOutput").value.trim());
}
async function jobFetch(path, options = {}) {
  if (!state.token) throw new Error("请先填写操作令牌");
  const response = await fetch(path, {
    cache: "no-store",
    ...options,
    headers: { "X-Ops-Token": state.token, ...(options.headers || {}) }
  });
  const contentType = response.headers.get("content-type") || "";
  const result = contentType.includes("application/json") ? await response.json() : await response.text();
  if (!response.ok) throw new Error(result.message || `HTTP ${response.status}`);
  return result;
}
function stateLabel(value) {
  return ({ queued: "排队", running: "运行中", succeeded: "成功", failed: "失败", cancelled: "已取消" })[value] || value;
}
function renderJobList() {
  const list = $("jobList");
  list.replaceChildren();
  if (!state.jobs.length) {
    const empty = document.createElement("div");
    empty.className = "job-empty";
    empty.innerHTML = "<strong>暂无任务</strong><span>从左侧执行一个白名单任务</span>";
    list.appendChild(empty);
    return;
  }
  state.jobs.forEach((job) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `job-row${state.selectedJobId === job.id ? " active" : ""}`;
    const copy = document.createElement("div");
    const title = document.createElement("strong");
    const meta = document.createElement("span");
    const status = document.createElement("b");
    title.textContent = job.job_type;
    meta.textContent = `${job.id} · ${job.requested_at || "--"}`;
    status.className = `job-state ${job.state}`;
    status.textContent = stateLabel(job.state);
    copy.append(title, meta);
    button.append(copy, status);
    button.addEventListener("click", () => selectJob(job.id));
    list.appendChild(button);
  });
}
function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 ** 2) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 ** 3) return `${(bytes / 1024 ** 2).toFixed(1)} MB`;
  return `${(bytes / 1024 ** 3).toFixed(2)} GB`;
}
async function runDatasetAction(button, busyLabel, action) {
  if (!button || button.disabled) return;
  const originalLabel = button.textContent;
  button.disabled = true;
  button.textContent = busyLabel;
  try { await action(); } finally { button.disabled = false; button.textContent = originalLabel; }
}
function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (character) => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"})[character]);
}
function datasetCard(item, category) {
  const card = document.createElement("div");
  card.className = `dataset-item${item.protected ? " protected" : ""}`;
  const datasetState = category === "lerobot" ? (item.training_ready ? "\u8bad\u7ec3\u53ef\u7528" : "\u9700\u6821\u9a8c") : item.format;
  const qualityButton = category === "exports" || category === "lerobot" ? '<button class="button ghost compact-button" data-dataset-quality>\u8d28\u91cf\u68c0\u67e5</button>' : "";
  card.innerHTML = `<div class="dataset-item-main"><div><strong title="${escapeHtml(item.name)}">${escapeHtml(item.name)}</strong><small>${escapeHtml(datasetState)} \u00b7 ${formatBytes(item.bytes)} \u00b7 ${item.frame_count || 0} \u5e27</small></div></div><div class="dataset-item-actions"><button class="button ghost compact-button" data-dataset-detail>\u8be6\u60c5/\u7b5b\u9009</button>${qualityButton}${category === "episodes" && !item.protected ? '<button class="button secondary compact-button" data-dataset-export>\u751f\u6210\u4e2d\u95f4\u6570\u636e</button>' : ""}${category === "exports" ? '<button class="button secondary compact-button" data-dataset-convert>\u8f6c LeRobot</button>' : ""}${category === "lerobot" ? '<button class="button secondary compact-button" data-dataset-archive>\u751f\u6210\u8bad\u7ec3\u5305</button>' : ""}</div>`;
  card.querySelector("[data-dataset-detail]").addEventListener("click", () => showDatasetDetail(item.path));
  card.querySelector("[data-dataset-quality]")?.addEventListener("click", (event) => runDatasetAction(event.currentTarget, "\u68c0\u67e5\u4e2d\u2026", () => inspectDataset(item)));
  card.querySelector("[data-dataset-export]")?.addEventListener("click", (event) => runDatasetAction(event.currentTarget, "\u5904\u7406\u4e2d\u2026", () => exportEpisode(item)));
  card.querySelector("[data-dataset-convert]")?.addEventListener("click", (event) => runDatasetAction(event.currentTarget, "\u8f6c\u6362\u4e2d\u2026", () => convertDataset(item)));
  card.querySelector("[data-dataset-archive]")?.addEventListener("click", (event) => runDatasetAction(event.currentTarget, "\u751f\u6210\u4e2d\u2026", () => archiveLerobot(item)));
  return card;
}
function renderDatasetList(id, countId, items, category) {
  const list = $(id); list.replaceChildren(); text(countId, `${items.length} \u9879`);
  if (!items.length) { list.innerHTML = `<div class="job-empty"><strong>\u6682\u65e0\u8bb0\u5f55</strong><span>\u5b8c\u6210\u91c7\u96c6\u6216\u8f6c\u6362\u540e\u4f1a\u663e\u793a\u5728\u8fd9\u91cc</span></div>`; return; }
  items.forEach((item) => list.appendChild(datasetCard(item, category)));
}
function parseFrameIndexes(value) {
  const indexes = new Set();
  for (const part of value.split(",").map((item) => item.trim()).filter(Boolean)) {
    const match = part.match(/^(\d+)(?:-(\d+))?$/);
    if (!match) throw new Error(`\u65e0\u6548\u5e27\u683c\u5f0f\uff1a${part}`);
    const startIndex = Number(match[1]);
    const endIndex = Number(match[2] ?? match[1]);
    if (endIndex < startIndex || endIndex - startIndex > 5000) throw new Error(`\u65e0\u6548\u5e27\u8303\u56f4\uff1a${part}`);
    for (let index = startIndex; index <= endIndex; index += 1) indexes.add(index);
  }
  if (indexes.size > 10000) throw new Error("\u65e0\u6548\u5e27\u6570\u91cf\u4e0d\u80fd\u8d85\u8fc7 10000");
  return [...indexes].sort((left, right) => left - right);
}
async function saveDatasetReview(path) {
  if (!state.token) { openToken(); throw new Error("\u8bf7\u5148\u586b\u5199\u64cd\u4f5c\u4ee4\u724c"); }
  const result = await jobFetch("/api/datasets/review", {
    method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({path, status: $("datasetReviewStatus").value, note: $("datasetReviewNote").value.trim(), excluded_frames: parseFrameIndexes($("datasetReviewFrames").value)})
  });
  toast("\u4eba\u5de5\u7b5b\u9009\u7ed3\u679c\u5df2\u4fdd\u5b58");
  await showDatasetDetail(result.path);
}
async function showDatasetDetail(path) {
  try {
    const detail = await fetch(`/api/datasets/detail/${encodeURIComponent(path)}`, { cache: "no-store" }).then(async (response) => { const data = await response.json(); if (!response.ok) throw new Error(data.message || `HTTP ${response.status}`); return data; });
    const files = (detail.files || []).slice(0, 8).map((file) => `${file.path} (${formatBytes(file.bytes)})`).join("\n");
    const report = detail.quality_report;
    const counts = report?.issue_counts || {};
    const reportHtml = report ? `<div class="quality-summary ${report.training_readiness ? "ready" : "blocked"}"><strong>${report.training_readiness ? "\u8d28\u91cf\u68c0\u67e5\u901a\u8fc7" : "\u8d28\u91cf\u68c0\u67e5\u672a\u901a\u8fc7"}</strong><span>${report.frame_count || 0} \u5e27 \u00b7 blocker ${counts.blocker || 0} \u00b7 error ${counts.error || 0} \u00b7 warning ${counts.warning || 0}</span><ul>${(report.issues || []).slice(0, 6).map((issue) => `<li><b>${escapeHtml(issue.severity)}</b>${escapeHtml(issue.message)}</li>`).join("") || "<li>\u672a\u53d1\u73b0\u95ee\u9898</li>"}</ul></div>` : '<div class="quality-summary pending"><strong>\u5c1a\u65e0\u8d28\u91cf\u62a5\u544a</strong><span>\u70b9\u51fb\u6570\u636e\u5361\u7247\u7684\u201c\u8d28\u91cf\u68c0\u67e5\u201d\u751f\u6210\u62a5\u544a</span></div>';
    const review = detail.review || {status: "pending", note: "", excluded_frames: []};
    $("datasetDetail").innerHTML = `<strong>${escapeHtml(detail.name)}</strong><span>${escapeHtml(detail.path)} \u00b7 ${formatBytes(detail.bytes)} \u00b7 ${detail.file_count} \u4e2a\u6587\u4ef6</span>${reportHtml}<div class="dataset-review-form"><label>\u4eba\u5de5\u7ed3\u8bba<select id="datasetReviewStatus"><option value="pending">\u5f85\u590d\u6838</option><option value="accepted">\u901a\u8fc7</option><option value="rejected">\u62d2\u7edd</option></select></label><label>\u6392\u9664\u5e27<input id="datasetReviewFrames" value="${escapeHtml((review.excluded_frames || []).join(","))}" placeholder="\u4f8b\u5982 3,8-12"></label><label class="wide">\u5907\u6ce8<textarea id="datasetReviewNote" rows="2" placeholder="\u8bb0\u5f55\u62d2\u7edd\u539f\u56e0\u6216\u573a\u666f\u8bf4\u660e">${escapeHtml(review.note || "")}</textarea></label><button class="button secondary compact-button" id="saveDatasetReview">\u4fdd\u5b58\u7b5b\u9009</button></div><pre>${escapeHtml(files || "\u76ee\u5f55\u4e3a\u7a7a")}</pre>`;
    $("datasetReviewStatus").value = review.status || "pending";
    $("saveDatasetReview").addEventListener("click", () => saveDatasetReview(detail.path).catch((error) => toast(error.message, true)));
  } catch (error) { toast(error.message, true); }
}
async function inspectDataset(item) {
  const stamp = new Date().toISOString().replace(/[-:TZ.]/g, "").slice(0, 14);
  await createJob("dataset.inspect", {dataset: item.path, output: `run/test/dataset-quality/${item.name}-${stamp}`});
  toast("\u8d28\u91cf\u68c0\u67e5\u4efb\u52a1\u5df2\u63d0\u4ea4\uff0c\u5b8c\u6210\u540e\u6253\u5f00\u8be6\u60c5\u67e5\u770b\u62a5\u544a");
}
async function refreshDatasets(showError = false) {
  try {
    const data = await fetch("/api/datasets", { cache: "no-store" }).then(async (response) => { const value = await response.json(); if (!response.ok) throw new Error(value.message || `HTTP ${response.status}`); return value; });
    badge($("datasetApiBadge"), true, "数据在线", "数据离线");
    renderDatasetList("episodeDatasetList", "episodeDatasetCount", data.episodes || [], "episodes");
    renderDatasetList("exportDatasetList", "exportDatasetCount", data.exports || [], "exports");
    state.lerobotItems = data.lerobot || []; renderDatasetList("lerobotDatasetList", "lerobotDatasetCount", state.lerobotItems, "lerobot");
    state.datasetCatalog = { episodes: data.episodes || [], exports: data.exports || [], lerobot: state.lerobotItems, archives: data.archives || [] };
    renderDatasetWorkflow(data.episodes || [], data.exports || [], state.lerobotItems, data.archives || []);
  } catch (error) { badge($("datasetApiBadge"), false, "数据在线", "数据离线"); if (showError) toast(error.message, true); }
}
async function downloadDatasetArchive(name) {
  if (!state.token) { openToken(); return; }
  try {
    const response = await fetch(`/api/datasets/download/${encodeURIComponent(name)}`, {headers: {"X-Ops-Token": state.token}});
    if (!response.ok) { const result = await response.json(); throw new Error(result.message || `HTTP ${response.status}`); }
    const blob = await response.blob();
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = name;
    link.click();
    URL.revokeObjectURL(url);
  } catch (error) { toast(error.message, true); }
}
function latestDatasetJob(jobType) {
  return (state.jobs || []).filter((job) => job.job_type === jobType).sort((a, b) => String(b.requested_at || b.created_at || b.id).localeCompare(String(a.requested_at || a.created_at || a.id))).at(0);
}
function datasetJobSuffix(job) {
  if (!job) return "";
  return `\u00b7 ${stateLabel(job.state)}`;
}
async function refreshDatasetFailureLog() {
  const types = ["dataset.export_episode", "dataset.convert_lerobot", "dataset.archive_lerobot"];
  const failed = (state.jobs || []).filter((job) => types.includes(job.job_type) && job.state === "failed").sort((a, b) => String(b.requested_at || b.created_at || b.id).localeCompare(String(a.requested_at || a.created_at || a.id))).at(0);
  if (!failed) { state.datasetFailureLogJobId = ""; state.datasetFailureLog = ""; return; }
  if (failed.id === state.datasetFailureLogJobId) return;
  state.datasetFailureLogJobId = failed.id;
  try { state.datasetFailureLog = await jobFetch(`/api/jobs/${encodeURIComponent(failed.id)}/log`); }
  catch (error) { state.datasetFailureLog = error.message; }
}
function renderDatasetWorkflow(episodes, exports, lerobot, archives) {
  const stageJobs = { episodes: latestDatasetJob("dataset.export_episode"), exports: latestDatasetJob("dataset.export_episode"), lerobot: latestDatasetJob("dataset.convert_lerobot"), archive: latestDatasetJob("dataset.archive_lerobot") };
  ["episodes", "exports", "lerobot", "archive"].forEach((stage) => { const node = document.querySelector(`[data-flow-stage="${stage}"]`); if (node) node.classList.remove("active", "success", "failed"); });
  text("datasetFlowEpisodes", `${episodes.length} \u9879 ? ${episodes.length ? "\u53ef\u751f\u6210\u4e2d\u95f4\u6570\u636e" : "\u7b49\u5f85\u91c7\u96c6\u8bb0\u5f55"}${datasetJobSuffix(stageJobs.episodes)}`);
  text("datasetFlowExports", `${exports.length} \u9879 ? ${exports.length ? "\u53ef\u8f6c\u6362\u4e3a LeRobot" : "\u7b49\u5f85\u751f\u6210\u4e2d\u95f4\u6570\u636e"}${datasetJobSuffix(stageJobs.exports)}`);
  text("datasetFlowLerobot", `${lerobot.length} \u9879 ? ${lerobot.length ? "\u53ef\u751f\u6210\u8bad\u7ec3\u5305" : "\u7b49\u5f85\u8f6c\u6362"}${datasetJobSuffix(stageJobs.lerobot)}`);
  text("datasetFlowArchive", `${archives.length} \u4e2a ? ${archives.length ? "\u5df2\u751f\u6210\uff0c\u53ef\u4e0b\u8f7d" : "\u7b49\u5f85\u751f\u6210"}${datasetJobSuffix(stageJobs.archive)}`);
  Object.entries(stageJobs).forEach(([stage, job]) => {
    const node = document.querySelector(`[data-flow-stage="${stage}"]`);
    if (!node || !job) return;
    if (["queued", "running"].includes(job.state)) node.classList.add("active");
    else if (job.state === "succeeded") node.classList.add("success");
    else if (["failed", "cancelled"].includes(job.state)) node.classList.add("failed");
  });
  const status = $("datasetWorkflowStatus");
  if (!status) return;
  const jobs = state.jobs || [];
  const latest = latestDatasetJob;
  const active = ["dataset.export_episode", "dataset.convert_lerobot", "dataset.archive_lerobot"].map(latest).find((job) => job && ["queued", "running"].includes(job.state));
  const failed = ["dataset.export_episode", "dataset.convert_lerobot", "dataset.archive_lerobot"].map(latest).find((job) => job && job.state === "failed");
  status.replaceChildren();
  status.classList.remove("error");
  if (active) {
    status.append(document.createTextNode(`\u6b63\u5728${active.job_type === "dataset.export_episode" ? "\u751f\u6210\u4e2d\u95f4\u6570\u636e" : active.job_type === "dataset.convert_lerobot" ? "\u8f6c\u6362 LeRobot \u6570\u636e\u96c6" : "\u751f\u6210\u8bad\u7ec3\u5305"} · ${stateLabel(active.state)}}`));
    return;
  }
  if (failed) {
    status.classList.add("error");
    const title = document.createElement("strong");
    title.textContent = `\u6700\u8fd1\u4efb\u52a1\u5931\u8d25\uff1a${failed.job_type === "dataset.export_episode" ? "\u4e2d\u95f4\u6570\u636e\u751f\u6210" : failed.job_type === "dataset.convert_lerobot" ? "LeRobot \u8f6c\u6362" : "\u8bad\u7ec3\u5305\u751f\u6210"}`;
    const detail = document.createElement("pre");
    detail.className = "dataset-workflow-error-detail";
    detail.textContent = (state.datasetFailureLog || "\u4efb\u52a1\u65e5\u5fd7\u6682\u65e0\u8f93\u51fa").trim().slice(-1600);
    status.append(title, detail);
    return;
  }
  if (archives.length) {
    const latestArchive = archives[0];
    status.append(document.createTextNode(`\u8bad\u7ec3\u5305\u5df2\u751f\u6210\uff1a${latestArchive.name} · ${formatBytes(latestArchive.bytes)} `));
    const button = document.createElement("button");
    button.className = "button ghost compact-button";
    button.textContent = "\u4e0b\u8f7d";
    button.addEventListener("click", () => downloadDatasetArchive(latestArchive.name));
    status.append(button);
    return;
  }
  if (lerobot.length) { status.textContent = "LeRobot \u6570\u636e\u96c6\u5df2\u5c31\u7eea\uff0c\u53ef\u5728\u53f3\u4fa7\u5361\u7247\u751f\u6210\u8bad\u7ec3\u5305\u3002"; return; }
  if (exports.length) { status.textContent = "\u4e2d\u95f4\u6570\u636e\u5df2\u5c31\u7eea\uff0c\u53ef\u5728\u4e2d\u95f4\u6570\u636e\u5361\u7247\u8f6c\u6362\u4e3a LeRobot\u3002"; return; }
  status.textContent = "\u8bf7\u4ece\u4e0b\u65b9\u539f\u59cb Episode \u5f00\u59cb\u751f\u6210\u4e2d\u95f4\u6570\u636e\u3002";
}
async function archiveLerobot(item) {
  if (!state.token) { openToken(); return; }
  const datasets = [item.path];
  const name = item.name;
  try {
    await createJob("dataset.archive_lerobot", { datasets, output: `run/ops/exports/${name}.zip` });
    toast("训练包生成任务已提交");
  } catch (error) { toast(error.message, true); }
}async function exportEpisode(item) {
  if (!state.token) { openToken(); return; }
  try { await createJob("dataset.export_episode", { episode: item.path, output: `datasets/exports/${item.name}` }); toast("中间数据生成任务已提交"); await refreshDatasets(); } catch (error) { toast(error.message, true); }
}
async function convertDataset(item) {
  if (!state.token) { openToken(); return; }
  try { await createJob("dataset.convert_lerobot", { dataset: item.path, output: `datasets/lerobot/${item.name}` }); await refreshDatasets(); } catch (error) { toast(error.message, true); }
}async function refreshJobs(showError = false) {
  if (!state.token) {
    $("jobApiBadge").className = "pill neutral";
    $("jobApiBadge").innerHTML = "<i></i>需要令牌";
    return;
  }
  try {
    const result = await jobFetch("/api/jobs");
    state.jobs = result.jobs || [];
    const runningSwitch = state.jobs.find((job) => job.job_type === "policy.runtime_switch" && ["queued", "running"].includes(job.state));
    if (!state.runtimeSwitchJobId && runningSwitch) {
      state.runtimeSwitchJobId = runningSwitch.id;
      state.runtimeSwitchTarget = runningSwitch.parameters?.provider || "";
      state.runtimeSwitchNotified = false;
    }
    if (state.runtimeSwitchJobId) {
      const switchJob = state.jobs.find((job) => job.id === state.runtimeSwitchJobId);
      if (switchJob && ["succeeded", "failed", "cancelled"].includes(switchJob.state)) {
        if (!state.runtimeSwitchNotified) {
          toast(switchJob.state === "succeeded"
            ? `Policy Runtime 已切换到 ${switchJob.parameters?.provider === "smolvla" ? "SmolVLA" : "Mock"}`
            : "Policy Runtime 切换失败，已尝试回滚到 Mock；请查看任务日志", switchJob.state !== "succeeded");
          state.runtimeSwitchNotified = true;
        }
        state.runtimeSwitchJobId = "";
        state.runtimeSwitchTarget = "";
      }
    }
    updatePolicyRuntimeCard();
    await refreshDatasetFailureLog();
    const catalog = state.datasetCatalog || { episodes: [], exports: [], lerobot: [], archives: [] };
    renderDatasetWorkflow(catalog.episodes, catalog.exports, catalog.lerobot, catalog.archives);
    $("jobApiBadge").className = "pill success";
    $("jobApiBadge").innerHTML = "<i></i>JOB API ONLINE";
    renderJobList();
    if (state.selectedJobId) await refreshSelectedJob();
  } catch (error) {
    $("jobApiBadge").className = "pill warning";
    $("jobApiBadge").innerHTML = "<i></i>JOB API ERROR";
    if (showError) toast(error.message, true);
  }
}
async function selectJob(jobId) {
  state.selectedJobId = jobId;
  renderJobList();
  await refreshSelectedJob(true);
}
async function refreshSelectedJob(showError = false) {
  if (!state.selectedJobId || !state.token) return;
  try {
    const [job, log] = await Promise.all([
      jobFetch(`/api/jobs/${encodeURIComponent(state.selectedJobId)}`),
      jobFetch(`/api/jobs/${encodeURIComponent(state.selectedJobId)}/log`)
    ]);
    $("selectedJobTitle").textContent = job.job_type;
    $("selectedJobState").className = `job-state ${job.state}`;
    $("selectedJobState").textContent = stateLabel(job.state);
    $("jobLog").textContent = log || "任务尚未产生输出。";
    $("cancelJob").disabled = ["policy.runtime_switch", "policy.model_activate"].includes(job.job_type) || !["queued", "running"].includes(job.state);
  } catch (error) {
    if (showError) toast(error.message, true);
  }
}
async function createJob(jobType, parameters = {}) {
  const job = await jobFetch("/api/jobs", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ job_type: jobType, parameters })
  });
  state.selectedJobId = job.id;
  toast(`任务已提交：${job.job_type}`);
  await refreshJobs(true);
}
function modelPlugin(pluginId) {
  return (state.modelCatalog.mobility?.plugins || []).find((plugin) => plugin.id === pluginId) || null;
}
function modelPluginName(pluginId) {
  const plugin = modelPlugin(pluginId);
  return plugin?.display_name || pluginId || "未激活";
}
function modelCompatibilityText(compatibility) {
  const labels = {
    compatible: ["兼容", "当前插件可以安全解释该模型输出。"],
    incompatible: ["插件不兼容", "当前插件不能解释该模型 Action Schema，请切换到推荐插件。"],
    no_active_plugin: ["尚未激活插件", "激活模型前必须先激活一个兼容的 Mobility Plugin。"],
    unsupported_schema: ["没有可用插件", "当前平台没有插件支持该模型 Action Schema。"],
    missing_action_descriptor: ["动作协议未知", "模型缺少 Action Descriptor，激活后只能使用 Zero Adapter，不会输出真实控制。"],
    invalid_action_descriptor: ["动作清单无效", "模型 Action Descriptor 缺少 Schema，禁止激活。"],
    manifest_error: ["模型清单损坏", "无法读取 vehicle_model_manifest.json，禁止激活。"]
  };
  return labels[compatibility?.status] || ["待检查", "等待模型与插件兼容性检查。"];
}
function updateModelCompatibilitySummary(result) {
  const summary = $("modelCompatibilitySummary");
  const activeId = result.mobility?.active_plugin_id || "";
  const plugin = modelPlugin(activeId);
  summary.className = `model-compatibility-overview ${activeId ? "ready" : "warning"}`;
  summary.innerHTML = activeId
    ? `<strong>当前动作插件：${escapeHtml(modelPluginName(activeId))}</strong><span>${escapeHtml(plugin?.selection_hint || "系统将根据模型 Action Schema 判断兼容性。")}</span>`
    : `<strong>尚未激活 Mobility Plugin</strong><span>带 Action Descriptor 的训练模型暂时不能激活，请先选择系统推荐插件。</span>`;
}
function modelCard(model) {
  const card = document.createElement("div");
  const manifest = model.manifest || {};
  const action = manifest.action || {};
  const compatibility = model.compatibility || {};
  const runtime = model.runtime_compatibility || {};
  const [statusTitle, statusDescription] = modelCompatibilityText(compatibility);
  const recommended = (compatibility.recommended_plugin_ids || []).map(modelPluginName);
  const features = (action.features || []).slice().sort((left, right) => Number(left.index) - Number(right.index));
  const featureText = features.length ? features.map((feature) => `${feature.name}${feature.unit ? ` [${feature.unit}]` : ""}`).join(" · ") : "未声明";
  const canActivate = model.activatable && !model.active && model.valid && compatibility.activation_allowed !== false;
  const blockedButton = model.activatable && !model.active && model.valid && !canActivate;
  const statusClass = compatibility.status === "compatible" ? "ready" : compatibility.status === "missing_action_descriptor" ? "warning" : "blocked";
  const runtimeClass = runtime.activation_allowed === false ? "blocked" : runtime.status === "requires_runtime_check" ? "warning" : "ready";
  card.className = `model-item${model.active ? " active" : ""}${model.valid ? "" : " invalid"}`;
  card.innerHTML = `<div class="model-main"><strong>${escapeHtml(model.provider)} / ${escapeHtml(model.version)}${model.active ? " · 当前" : ""}</strong><small>${escapeHtml(manifest.model_id || "本地模型")} @ ${escapeHtml(manifest.revision || "--")} · ${formatBytes(model.bytes)}</small><span>训练数据：${escapeHtml(manifest.dataset_id || "未关联")}</span><div class="model-runtime-contract"><span><b>推理后端</b>${escapeHtml(runtime.backend || "pytorch")}</span><span><b>权重精度</b>${escapeHtml(runtime.weight_precision || "mixed")}</span><span><b>激活精度</b>${escapeHtml(runtime.activation_precision || "bfloat16")}</span></div><div class="model-compatibility ${runtimeClass}"><strong>运行时兼容性</strong><span>${escapeHtml(runtime.message || "等待检查")}</span></div><div class="model-action-contract"><span><b>Action Schema</b>${escapeHtml(compatibility.action_schema || "未声明")}</span><span><b>输出字段</b>${escapeHtml(featureText)}</span><span><b>当前插件</b>${escapeHtml(modelPluginName(compatibility.active_plugin_id))}</span><span><b>推荐插件</b>${escapeHtml(recommended.join("、") || "无")}</span></div><div class="model-compatibility ${statusClass}"><strong>${escapeHtml(statusTitle)}</strong><span>${escapeHtml(statusDescription)}</span></div></div><div class="model-actions">${canActivate ? '<button class="button secondary compact-button" data-model-activate>激活</button>' : ""}${model.activatable && runtime.weight_precision !== "int8" ? '<button class="button secondary compact-button" data-model-int8>生成 INT8</button>' : ""}${blockedButton ? '<button class="button secondary compact-button" disabled title="模型与当前运行时或 Mobility Plugin 不兼容">无法激活</button>' : ""}</div>`;
  card.querySelector("[data-model-activate]")?.addEventListener("click", async () => {
    if (!confirm(`确认激活模型 ${model.version}？\n\nAction Schema：${compatibility.action_schema || "未声明"}\n当前插件：${modelPluginName(compatibility.active_plugin_id)}\n\n系统将重启 SmolVLA Runtime；失败时自动恢复上一版本。`)) return;
    try { await createJob("policy.model_activate", {provider: model.provider, version: model.version}); }
    catch (error) { toast(error.message, true); if (!state.token) openToken(); }
  });
  card.querySelector("[data-model-int8]")?.addEventListener("click", async () => {
    const targetVersion = prompt("请输入 INT8 模型版本名", `${model.version}-int8`)?.trim();
    if (!targetVersion) return;
    try {
      await createJob("policy.model_variant", {provider: model.provider, source_version: model.version, target_version: targetVersion, precision: "int8"});
    } catch (error) { toast(error.message, true); if (!state.token) openToken(); }
  });
  return card;
}
async function refreshModels(showError = false) {
  try {
    const result = await fetch("/api/models", {cache: "no-store"}).then(async (response) => { const data = await response.json(); if (!response.ok) throw new Error(data.message || `HTTP ${response.status}`); return data; });
    state.modelCatalog = result;
    updateModelCompatibilitySummary(result);
    const list = $("modelList"); list.replaceChildren();
    if (!(result.models || []).length) list.innerHTML = '<div class="job-empty"><strong>\u5c1a\u65e0\u6a21\u578b</strong><span>\u4f7f\u7528\u4e0a\u65b9\u8868\u5355\u5b89\u88c5\u7b2c\u4e00\u4e2a\u7248\u672c</span></div>';
    else (result.models || []).forEach((model) => list.appendChild(modelCard(model)));
  } catch (error) { if (showError) toast(error.message, true); }
}
$("modelInstallForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  const localSource = $("modelSource").value === "local";
  const sourceValue = $("modelRepository").value.trim();
  const common = {provider: $("modelProvider").value, version: $("modelVersion").value.trim(), dataset_id: $("modelDatasetId").value.trim()};
  if (!common.version || !sourceValue) {toast(localSource ? "请填写版本名和本地模型目录" : "请填写版本名和模型仓库", true); return;}
  const parameters = localSource ? {...common, source_path: sourceValue} : {...common, model_id: sourceValue, revision: $("modelRevision").value.trim() || "main"};
  if (!confirm(localSource ? `确认从车端目录导入 ${sourceValue}？\n\n导入不会自动切换当前模型。` : `确认下载并安装 ${parameters.model_id}@${parameters.revision}？\n\n安装不会自动切换当前模型。`)) return;
  try { await createJob(localSource ? "policy.model_import" : "policy.model_install", parameters); }
  catch (error) { toast(error.message, true); if (!state.token) openToken(); }
});
$("modelSource").addEventListener("change", () => {
  const localSource = $("modelSource").value === "local";
  $("modelRepositoryLabel").firstChild.textContent = localSource ? "本地模型目录" : "模型仓库";
  $("modelRepository").placeholder = localSource ? "/data/models/my-smolvla" : "组织/仓库";
  $("modelRevisionLabel").hidden = localSource;
});
$("refreshModels").addEventListener("click", () => refreshModels(true));

["datasetInput", "datasetOutput", "datasetTool"].forEach((id) => $(id).addEventListener("input", updateCommand));
$("copyCommand").addEventListener("click", async () => {
  try { await navigator.clipboard.writeText($("generatedCommand").textContent); toast("命令已复制"); }
  catch { toast("浏览器禁止剪贴板访问，请手动复制", true); }
});
$("jobForm").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    await createJob($("datasetTool").value, {
      dataset: $("datasetInput").value.trim(), output: $("datasetOutput").value.trim()
    });
  } catch (error) { toast(error.message, true); if (!state.token) openToken(); }
});
document.querySelectorAll("[data-policy-job]").forEach((button) => button.addEventListener("click", async () => {
  try { await createJob(button.dataset.policyJob); }
  catch (error) { toast(error.message, true); if (!state.token) openToken(); }
}));
async function switchPolicyRuntime(provider) {
  if (!state.token) { openToken(); throw new Error("请先填写操作令牌"); }
  if (state.systemMode !== "VLA_SHADOW") throw new Error("请先进入 VLA_SHADOW 模式");
  const displayName = provider === "smolvla" ? "SmolVLA" : "Mock";
  if (!confirm(`确认切换到 ${displayName} Runtime？

切换期间 Shadow 推理会短暂中断；SmolVLA 启动失败时会自动回滚 Mock。`)) return;
  const job = await jobFetch("/api/jobs", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ job_type: "policy.runtime_switch", parameters: { provider } })
  });
  state.runtimeSwitchJobId = job.id;
  state.runtimeSwitchTarget = provider;
  state.runtimeSwitchNotified = false;
  state.selectedJobId = job.id;
  updatePolicyRuntimeCard();
  toast(`已提交 ${displayName} Runtime 切换任务`);
  await refreshJobs(true);
}
document.querySelectorAll("[data-runtime-provider]").forEach((button) => button.addEventListener("click", () => {
  switchPolicyRuntime(button.dataset.runtimeProvider).catch((error) => toast(error.message, true));
}));
$("refreshJobs").addEventListener("click", () => refreshJobs(true));
$("cancelJob").addEventListener("click", async () => {
  if (!state.selectedJobId || !confirm("确认取消当前任务？正在运行的子进程组将收到 SIGTERM。")) return;
  try {
    await jobFetch(`/api/jobs/${encodeURIComponent(state.selectedJobId)}/cancel`, { method: "POST" });
    toast("已提交取消请求");
    await refreshJobs(true);
  } catch (error) { toast(error.message, true); }
});
function pipelineStatusClass(status) {
  return ({ LIVE: "live", READY: "ready", RUNNING: "running", STALE: "stale", SKIPPED: "skipped", REJECTED: "rejected", FAILED: "failed" })[status] || "waiting";
}
const pipelineStageNames = {
  sensor_capture: { zh: "传感器采集", en: "Sensor Capture" },
  observation_health: { zh: "输入健康检查", en: "Observation Health" },
  observation_assembly: { zh: "观测数据组装", en: "Observation Assembly" },
  contract_validation: { zh: "数据契约校验", en: "Contract Validation" },
  model_runtime: { zh: "模型运行时", en: "Model Runtime" },
  policy_output: { zh: "策略动作输出", en: "Policy Output" },
  action_runtime: { zh: "动作运行时", en: "Action Runtime" },
  control_mux: { zh: "控制指令仲裁", en: "Control Arbitration" },
  safety_guard: { zh: "安全防护", en: "Safety Guard" },
  shadow_evaluation: { zh: "影子模式评估", en: "Shadow Evaluation" },
  trace_recording: { zh: "数据追踪记录", en: "Episode / Trace" }
};
const pipelineStatusNames = {
  WAITING: { zh: "等待数据", en: "WAITING" },
  LIVE: { zh: "实时", en: "LIVE" },
  READY: { zh: "就绪", en: "READY" },
  RUNNING: { zh: "运行中", en: "RUNNING" },
  STALE: { zh: "数据过期", en: "STALE" },
  SKIPPED: { zh: "已跳过", en: "SKIPPED" },
  REJECTED: { zh: "已拒绝", en: "REJECTED" },
  FAILED: { zh: "失败", en: "FAILED" }
};
const pipelineStageDescriptions = {
  sensor_capture: "正在接收前视相机压缩图像。",
  observation_health: "正在检查相机、里程计、IMU 和标定状态。",
  observation_assembly: "正在把图像、车辆状态和任务文本组装为统一观测数据。",
  contract_validation: "正在校验观测 Schema、图像、任务和状态有效性。",
  model_runtime: "正在检查模型服务、模型版本和推理运行状态。",
  policy_output: "正在检查模型生成的动作序列及其数据新鲜度。",
  action_runtime: "正在把策略动作序列转换为车辆候选速度指令。",
  control_mux: "正在根据当前控制模式选择候选控制来源。",
  safety_guard: "正在检查安全规则和最终速度指令。",
  shadow_evaluation: "正在比较模型预测动作与车辆实际执行动作。",
  trace_recording: "正在检查 Episode 和链路追踪记录状态。"
};
function pipelineStageName(stage) {
  return pipelineStageNames[stage?.stage_id] || { zh: stage?.label || "未知阶段", en: stage?.label || "Unknown Stage" };
}
function pipelineStatusName(status) {
  return pipelineStatusNames[status] || { zh: "未知状态", en: status || "UNKNOWN" };
}
function contractValidationSummary(stage) {
  const detail = stage?.detail || {};
  const failures = [];
  if (detail.schema_valid === false) failures.push("Schema 版本不匹配");
  if (detail.image_valid === false) failures.push("前视图像缺失或为空");
  if (detail.task_valid === false) failures.push("任务文本为空");
  if (!failures.length) return "数据契约校验通过，Observation 可以交给 Policy Provider。";
  const validParts = [];
  if (detail.schema_valid === true) validParts.push("Schema");
  if (detail.image_valid === true) validParts.push("图像");
  const validText = validParts.length ? "；" + validParts.join("、") + "已通过" : "";
  return "校验未通过：" + failures.join("、") + validText + "。";
}
function contractCheckRow(label, status, detail, action = "") {
  const row = document.createElement("div");
  row.className = "contract-check " + status;
  const icon = document.createElement("i");
  icon.textContent = status === "pass" ? "✓" : status === "warn" ? "!" : "×";
  const copy = document.createElement("span");
  const title = document.createElement("strong");
  const message = document.createElement("small");
  title.textContent = label;
  message.textContent = detail;
  copy.append(title, message);
  row.append(icon, copy);
  if (action) {
    const hint = document.createElement("em");
    hint.textContent = action;
    row.appendChild(hint);
  }
  return row;
}
function renderContractDiagnosis(stage) {
  const panel = $("contractDiagnosis");
  if (stage?.stage_id !== "contract_validation") {
    panel.hidden = true;
    panel.replaceChildren();
    return;
  }
  const detail = stage.detail || {};
  panel.hidden = false;
  panel.replaceChildren();
  const heading = document.createElement("div");
  heading.className = "contract-diagnosis-heading";
  const headingTitle = document.createElement("strong");
  const headingText = document.createElement("span");
  headingTitle.textContent = "契约检查明细";
  headingText.textContent = "阻断项必须修复；状态字段是诊断信息，由具体 Provider 决定是否必需。";
  heading.append(headingTitle, headingText);
  const checks = document.createElement("div");
  checks.className = "contract-check-list";
  checks.append(
    contractCheckRow("Schema 版本", detail.schema_valid ? "pass" : "fail",
      detail.schema_valid ? "vehicle.observation.v1，版本正确" : "期望 vehicle.observation.v1，请检查 Observation Adapter"),
    contractCheckRow("前视图像", detail.image_valid ? "pass" : "fail",
      detail.image_valid ? "压缩图像存在且包含有效字节" : "未收到有效图像，请检查相机和 Observation 链路"),
    contractCheckRow("任务文本", detail.task_valid ? "pass" : "fail",
      detail.task_valid ? "Observation.task 已设置" : "当前任务为空，模型不知道需要完成什么任务",
      detail.task_valid ? "" : "阻断推理"));
  const validCount = Number(detail.state_valid_count || 0);
  const totalCount = Number(detail.state_total_count || 0);
  const stateStatus = totalCount > 0 && validCount === totalCount ? "pass" : "warn";
  checks.append(contractCheckRow("车辆状态", stateStatus,
    validCount + " / " + (totalCount || "--") + " 个状态字段有效",
    stateStatus === "warn" ? "非通用阻断项" : ""));
  panel.append(heading, checks);
  if (detail.task_valid === false) {
    const repair = document.createElement("form");
    repair.className = "contract-repair";
    const label = document.createElement("label");
    const labelText = document.createElement("span");
    const input = document.createElement("input");
    const button = document.createElement("button");
    labelText.textContent = "设置实时任务";
    input.id = "contractTaskInput";
    input.maxLength = 500;
    input.placeholder = "例如：向前行驶并避开障碍物";
    button.className = "button primary";
    button.type = "submit";
    button.textContent = "发布任务并重新校验";
    input.value = $("debugTask")?.value.trim() || $("taskText")?.value.trim() || "move forward and avoid obstacles";
    label.append(labelText, input);
    repair.append(label, button);
    repair.addEventListener("submit", async (event) => {
      event.preventDefault();
      const value = input.value.trim();
      if (!value) { toast("请输入任务文本", true); input.focus(); return; }
      button.disabled = true;
      button.textContent = "正在发布…";
      try {
        const result = await post("/api/task", value);
        if ($("taskText")) $("taskText").value = value;
        if ($("debugTask")) $("debugTask").value = value;
        toast(result.message || "任务已发布，正在等待新 Observation");
        setTimeout(() => refreshPipeline(true), 500);
      } catch (error) {
        toast(error.message, true);
        button.disabled = false;
        button.textContent = "发布任务并重新校验";
      }
    });
    panel.appendChild(repair);
  }
}
function pipelineStageDescription(stage) {
  const base = pipelineStageDescriptions[stage?.stage_id] || "正在读取当前阶段状态。";
  if (stage?.stage_id === "contract_validation") return contractValidationSummary(stage);
  if (["FAILED", "REJECTED", "STALE"].includes(stage?.status) && stage?.message) return base + " 当前异常：" + stage.message;
  return base;
}
function setInspectorMode(mode) {
  state.inspectorMode = mode;
  document.querySelectorAll("[data-inspector-mode]").forEach((button) => {
    const active = button.dataset.inspectorMode === mode;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", active ? "true" : "false");
  });
  document.querySelectorAll("[data-inspector-panel]").forEach((panel) => panel.classList.toggle("active", panel.dataset.inspectorPanel === mode));
  if (mode === "history") refreshPipelineHistory(true);
  if (mode === "components") refreshComponents(true);
}
function stageById(trace, stageId) {
  return trace?.stages?.find((stage) => stage.stage_id === stageId);
}
function renderPipelineStageDetail(stage) {
  if (!stage) return;
  state.selectedPipelineStageId = stage.stage_id;
  document.querySelectorAll(".pipeline-stage-node").forEach((node) => node.classList.toggle("selected", node.dataset.stageId === stage.stage_id));
  const name = pipelineStageName(stage);
  const statusName = pipelineStatusName(stage.status);
  text("pipelineStageLabel", name.zh);
  text("pipelineStageEnglish", name.en);
  text("pipelineStageComponent", `节点 / 组件：${stage.component}`);
  text("pipelineStageInput", stage.input_summary || "--");
  text("pipelineStageOutput", stage.output_summary || "--");
  text("pipelineStageLatency", Number(stage.latency_ms) >= 0 ? `${number(stage.latency_ms, 1)} ms` : "--");
  text("pipelineStageAge", Number(stage.age_seconds) >= 0 ? `${number(stage.age_seconds, 2)} s` : "--");
  const messageBox = $("pipelineStageMessage");
  const chineseMessage = document.createElement("strong");
  const originalMessage = document.createElement("span");
  chineseMessage.textContent = pipelineStageDescription(stage);
  originalMessage.textContent = stage.message || "No stage message";
  messageBox.replaceChildren(chineseMessage, originalMessage);
  renderContractDiagnosis(stage);
  $("pipelineStageStatus").className = `job-state pipeline-${pipelineStatusClass(stage.status)}`;
  $("pipelineStageStatus").textContent = `${statusName.zh} · ${statusName.en}`;
  $("pipelineStageDetail").textContent = JSON.stringify(stage.detail || {}, null, 2);
}
function renderPipelineTrace(trace) {
  state.pipelineTrace = trace;
  const stages = trace.stages || [];
  const hasFailure = stages.some((stage) => ["FAILED", "REJECTED"].includes(stage.status));
  const stale = Number(trace.received_age_ms) > 3000;
  $("pipelineLiveState").className = `pill ${hasFailure ? "warning" : stale ? "neutral" : "success"}`;
  $("pipelineLiveState").innerHTML = `<i></i>${hasFailure ? "需要检查 · CHECK" : stale ? "追踪过期 · STALE" : "实时追踪 · LIVE"}`;
  text("pipelineTraceId", trace.trace_id || "--");
  text("pipelineObservationId", trace.observation_id || "--");
  text("pipelineProvider", [trace.provider_id, trace.model_id].filter(Boolean).join(" / ") || "--");
  text("pipelineReceivedAge", `${number((trace.received_age_ms || 0) / 1000, 1)} s 前`);
  const rail = $("pipelineStageRail");
  rail.replaceChildren();
  stages.forEach((stage, index) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `pipeline-stage-node ${pipelineStatusClass(stage.status)}`;
    button.dataset.stageId = stage.stage_id;
    const name = pipelineStageName(stage);
    const statusName = pipelineStatusName(stage.status);
    const sequence = document.createElement("span");
    const chinese = document.createElement("strong");
    const english = document.createElement("em");
    const chineseStatus = document.createElement("small");
    const englishStatus = document.createElement("i");
    sequence.textContent = index + 1;
    chinese.textContent = name.zh;
    english.textContent = name.en;
    chineseStatus.textContent = statusName.zh;
    englishStatus.textContent = statusName.en;
    button.append(sequence, chinese, english, chineseStatus, englishStatus);
    button.addEventListener("click", () => renderPipelineStageDetail(stage));
    rail.appendChild(button);
  });
  const selected = stageById(trace, state.selectedPipelineStageId) || stages.find((stage) => ["FAILED", "REJECTED", "STALE"].includes(stage.status)) || stages[0];
  renderPipelineStageDetail(selected);
  const action = stageById(trace, "policy_output");
  const safety = stageById(trace, "safety_guard");
  const shadow = stageById(trace, "shadow_evaluation");
  text("pipelineActionSummary", action ? `${pipelineStatusName(action.status).zh} · ${action.output_summary}` : "等待策略动作输出");
  text("pipelineSafetySummary", safety ? `${pipelineStatusName(safety.status).zh} · ${pipelineStageDescription(safety)}` : "等待安全防护结果");
  text("pipelineShadowSummary", shadow ? `${pipelineStatusName(shadow.status).zh} · ${shadow.output_summary}` : "等待影子评估结果");
  const camera = stageById(trace, "sensor_capture");
  state.pipelineCameraActive = Boolean(camera && ["LIVE", "READY"].includes(camera.status));
  renderCameraView(state.cameraStatus, {
    image: "pipelineLiveImage", empty: "pipelineLiveEmpty", overlay: "pipelineCameraLiveOverlay",
    label: "pipelineCameraLiveLabel", time: "pipelineCameraFrameTime", sequence: "pipelineCameraFrameSequence"
  }, "pipelineCameraSequence", state.pipelineCameraActive);
}
function activeDebugStatusName(status) {
  return ({RUNNING:"运行中", PAUSED:"已暂停", STOPPED:"已停止", TIMED_OUT:"已超时", FAILED:"失败", SUCCEEDED:"任务成功", IDLE:"未启动"})[status] || status || "未启动";
}
function renderActiveDebug(session) {
  state.activeDebug = session;
  const status = session?.status || "IDLE";
  const running = status === "RUNNING";
  const paused = status === "PAUSED";
  const terminal = ["STOPPED", "TIMED_OUT", "FAILED", "SUCCEEDED"].includes(status);
  const badge = $("activeDebugBadge");
  if (!badge) return;
  badge.className = `pill ${running ? "success" : terminal ? "warning" : "neutral"}`;
  badge.innerHTML = `<i></i>${activeDebugStatusName(status)} · ${status}`;
  text("activeDebugStatus", activeDebugStatusName(status));
  text("activeDebugElapsed", `${number(session?.elapsed_seconds || 0, 1)} s`);
  text("activeDebugCount", session?.steps || 0);
  text("activeDebugObservation", session?.last_observation_id || "--");
  text("activeDebugReason", session?.stop_reason || "--");
  $("activeDebugStart").disabled = running || paused;
  $("activeDebugPause").disabled = !running;
  $("activeDebugResume").disabled = !paused;
  $("activeDebugStop").disabled = !(running || paused);
}
async function refreshActiveDebug(showError = false) {
  try {
    const response = await fetch("/api/active-debug/session", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    renderActiveDebug(await response.json());
  } catch (error) { if (showError) toast(error.message, true); }
}
async function activeDebugAction(action) {
  try {
    const result = await post(`/api/active-debug/session/${action}`);
    toast(result.message);
    await refreshActiveDebug(true);
  } catch (error) { toast(error.message, true); }
}
$("activeDebugStart")?.addEventListener("click", async () => {
  try {
    const task = $("activeDebugTask").value.trim();
    if (!task) throw new Error("请先输入任务描述");
    const result = await post("/api/active-debug/sessions", JSON.stringify({
      task, mode: "shadow", hz: Number($("activeDebugHz").value),
      max_duration_seconds: Number($("activeDebugDuration").value), max_steps: Number($("activeDebugSteps").value)
    }));
    toast(result.message); await refreshActiveDebug(true);
  } catch (error) { toast(error.message, true); }
});
$("activeDebugPause")?.addEventListener("click", () => activeDebugAction("pause"));
$("activeDebugResume")?.addEventListener("click", () => activeDebugAction("resume"));
$("activeDebugStop")?.addEventListener("click", () => activeDebugAction("stop"));
async function refreshPipeline(showError = false) {
  try {
    const response = await fetch("/api/pipeline/live", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    renderPipelineTrace(await response.json());
  } catch (error) {
    $("pipelineLiveState").className = "pill warning";
    $("pipelineLiveState").innerHTML = "<i></i>追踪离线 · OFFLINE";
    if (showError) toast(error.message, true);
  }
}
function renderPipelineHistory() {
  const list = $("pipelineHistoryList");
  list.replaceChildren();
  if (!state.pipelineHistory.length) {
    list.innerHTML = '<div class="job-empty"><strong>暂无 Trace</strong><span>等待新的 Observation</span></div>';
    return;
  }
  state.pipelineHistory.forEach((trace) => {
    const button = document.createElement("button");
    button.type = "button";
    const failed = (trace.stages || []).some((stage) => ["FAILED", "REJECTED"].includes(stage.status));
    button.className = `pipeline-history-row${state.selectedPipelineHistoryId === trace.trace_id ? " active" : ""}`;
    button.innerHTML = `<span><strong>${trace.observation_id || trace.trace_id}</strong><small>${trace.provider_id || "--"} · ${(trace.stages || []).length} 个阶段 / stages</small></span><b class="job-state pipeline-${failed ? "rejected" : "ready"}">${failed ? "检查 · CHECK" : "正常 · READY"}</b>`;
    button.addEventListener("click", () => {
      state.selectedPipelineHistoryId = trace.trace_id;
      renderPipelineHistory();
      text("pipelineHistoryTitle", trace.observation_id || trace.trace_id);
      $("pipelineHistoryStatus").className = `job-state pipeline-${failed ? "rejected" : "ready"}`;
      $("pipelineHistoryStatus").textContent = failed ? "检查 · CHECK" : "正常 · READY";
      $("pipelineHistoryJson").textContent = JSON.stringify(trace, null, 2);
    });
    list.appendChild(button);
  });
}
async function refreshPipelineHistory(showError = false) {
  try {
    const response = await fetch("/api/pipeline/history", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    state.pipelineHistory = (await response.json()).traces || [];
    renderPipelineHistory();
  } catch (error) { if (showError) toast(error.message, true); }
}
function clearDebugError() {
  $("debugError").hidden = true;
  text("debugErrorMessage", "--");
}
function showDebugError(error) {
  $("debugError").hidden = false;
  text("debugErrorMessage", error.message || String(error));
}function setDebugBusy(busy) {
  const uploadMissing = state.debugInputSource === "upload" && !state.debugUpload;
  $("captureDebug").disabled = busy || uploadMissing;
  $("preprocessDebug").disabled = busy || !state.debugRunId;
  $("inferenceDebug").disabled = busy || !state.debugRunId;
}
function setDebugStage(stage, label, stateClass = "running") {
  const stages = ["Capture", "Preprocess", "Inference", "Denormalize", "Schema", "Plugin", "Mux", "Safety", "Final"];
  const activeIndex = ({ capture: 0, preprocess: 1, inference: 2 })[stage] ?? 0;
  const completedIndex = stateClass === "succeeded" ? ({ capture: 0, preprocess: 1, inference: 8 })[stage] : activeIndex - 1;
  stages.forEach((name, index) => {
    const node = $("debugStep" + name);
    if (!node) return;
    node.classList.toggle("active", stateClass === "running" && index === activeIndex);
    node.classList.toggle("completed", index <= completedIndex);
    node.classList.toggle("failed", stateClass === "failed" && index === activeIndex);
  });
  $("debugStageState").className = `job-state ${stateClass}`;
  $("debugStageState").textContent = label;
}
function setDebugInputSource(source) {
  state.debugInputSource = source === "upload" ? "upload" : "camera";
  document.querySelectorAll("[data-debug-source]").forEach((button) => {
    const active = button.dataset.debugSource === state.debugInputSource;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", String(active));
  });
  $("debugUploadPanel").hidden = state.debugInputSource !== "upload";
  text("debugOriginalCaption", state.debugInputSource === "upload" ? "冻结的用户上传图像" : "冻结的原始相机帧");
  $("captureDebug").textContent = state.debugInputSource === "upload" ? "使用上传图片" : "采集当前帧";
  setDebugBusy(false);
}
function clearDebugUpload() {
  state.debugUpload = null;
  $("debugUploadInput").value = "";
  $("debugUploadPreview").removeAttribute("src");
  $("debugUploadPreview").style.display = "none";
  $("debugUploadPlaceholder").style.display = "block";
  text("debugUploadName", "尚未选择图片");
  text("debugUploadInfo", "将归一到当前相机分辨率并转换为 JPEG");
  $("clearDebugUpload").disabled = true;
  setDebugBusy(false);
}
async function prepareDebugUpload(file) {
  if (!file) return;
  if (!file.type.startsWith("image/")) throw new Error("请选择 JPG、PNG 或 WebP 图片");
  if (file.size > 12 * 1024 * 1024) throw new Error("原始图片不能超过 12 MB");
  const bitmap = await createImageBitmap(file);
  const originalWidth = bitmap.width;
  const originalHeight = bitmap.height;
  const width = Math.max(1, state.observationWidth || 640);
  const height = Math.max(1, state.observationHeight || 480);
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  context.fillStyle = "#000";
  context.fillRect(0, 0, width, height);
  const scale = Math.min(width / originalWidth, height / originalHeight);
  const drawWidth = Math.round(originalWidth * scale);
  const drawHeight = Math.round(originalHeight * scale);
  context.drawImage(bitmap, Math.round((width - drawWidth) / 2), Math.round((height - drawHeight) / 2), drawWidth, drawHeight);
  bitmap.close();
  let quality = .9;
  let dataUrl = canvas.toDataURL("image/jpeg", quality);
  while (dataUrl.length > 900000 && quality > .55) {
    quality -= .1;
    dataUrl = canvas.toDataURL("image/jpeg", quality);
  }
  const base64 = dataUrl.slice(dataUrl.indexOf(",") + 1);
  if (base64.length > 900000) throw new Error("图片压缩后仍然过大，请选择尺寸更小的图片");
  state.debugUpload = { base64, name: file.name, width, height, originalWidth, originalHeight };
  $("debugUploadPreview").src = dataUrl;
  $("debugUploadPreview").style.display = "block";
  $("debugUploadPlaceholder").style.display = "none";
  text("debugUploadName", file.name);
  text("debugUploadInfo", `${originalWidth} × ${originalHeight} → ${width} × ${height} · JPEG · ${Math.round(base64.length * .75 / 1024)} KB`);
  $("clearDebugUpload").disabled = false;
  setDebugBusy(false);
}
function resetDebugImages() {
  [["original", "debugOriginalImage", "debugOriginalEmpty"], ["processed", "debugProcessedImage", "debugProcessedEmpty"]]
    .forEach(([kind, imageId, emptyId]) => {
      if (state.debugImageUrls[kind]) URL.revokeObjectURL(state.debugImageUrls[kind]);
      delete state.debugImageUrls[kind];
      $(imageId).removeAttribute("src");
      $(imageId).style.display = "none";
      $(emptyId).style.display = "grid";
    });
  $("pipelineProcessedImage").removeAttribute("src");
  $("pipelineProcessedImage").style.display = "none";
  $("pipelineProcessedEmpty").style.display = "grid";
}
async function loadDebugImage(kind, imageId, emptyId) {
  if (!state.debugRunId) return;
  const response = await fetch(`/api/vla-debug/runs/${encodeURIComponent(state.debugRunId)}/${kind}.jpg`, {
    cache: "no-store", headers: { "X-Ops-Token": state.token }
  });
  if (!response.ok) return;
  if (state.debugImageUrls[kind]) URL.revokeObjectURL(state.debugImageUrls[kind]);
  state.debugImageUrls[kind] = URL.createObjectURL(await response.blob());
  $(imageId).src = state.debugImageUrls[kind];
  $(imageId).style.display = "block";
  if (kind === "processed") {
    $("pipelineProcessedImage").src = state.debugImageUrls[kind];
    $("pipelineProcessedImage").style.display = "block";
    $("pipelineProcessedEmpty").style.display = "none";
  }
  $(emptyId).style.display = "none";
}
async function loadMobilityPlugins() {
  const select = $("debugMobilityPlugin");
  if (!select) return;
  try {
    const response = await fetch("/api/mobility/plugins", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    state.mobilityPlugins = await response.json();
    state.mobilityPlugins.forEach((plugin) => {
      const option = document.createElement("option");
      option.value = plugin.id;
      option.textContent = `${plugin.id} · ${plugin.runtime_adapter || "adapter"}`;
      select.appendChild(option);
    });
    updateMobilityPluginInfo();
  } catch (error) {
    const info = $("debugMobilityPluginInfo");
    if (info) info.textContent = `插件列表读取失败：${error.message}`;
  }
}
function updateMobilityPluginInfo() {
  const select = $("debugMobilityPlugin");
  const info = $("debugMobilityPluginInfo");
  if (!select || !info) return;
  const plugin = state.mobilityPlugins.find((item) => item.id === select.value);
  if (!plugin) {
    info.textContent = "自动跟随模型 Schema；单步调试只做转换预览，不改变正式运行插件。";
    return;
  }
  const accepts = (plugin.accepts || []).map((item) => item.schema).filter(Boolean).join(", ");
  info.textContent = `${plugin.runtime_adapter || "adapter"} · 支持输入：${accepts || "未声明"} · 仅预览，不发布控制命令`;
}function debugJsonValue(value) {
  if (value === undefined || value === null) return "--";
  return JSON.stringify(value, null, 2);
}
function renderDebugSteps(result) {
  const preprocessing = result.preprocessing || null;
  const modelInputs = preprocessing?.model_inputs || null;
  const normalized = result.raw_output?.normalized_action_chunk || null;
  const denormalized = result.raw_output?.denormalized_actions || null;
  const interpreted = result.interpreted_output || null;
  const plugin = result.mobility_plugin || null;
  const command = plugin?.commands?.[0] || null;
  const execution = result.single_step_execution || { executed: false, message: "Preview only" };
  const latency = result.latency_ms || {};
  const stages = [
    { number: 1, title: "\u51bb\u7ed3\u8f93\u5165 / Frozen Observation", input: { observation: result.observation || null }, output: { observation: result.observation || null }, time: null },
    { number: 2, title: "\u9884\u5904\u7406 / Preprocess", input: { observation: result.observation || null }, output: preprocessing, time: latency.preprocessing },
    { number: 3, title: "\u6a21\u578b\u63a8\u7406 / Inference", input: { model_inputs: modelInputs }, output: normalized, time: latency.inference },
    { number: 4, title: "\u53cd\u5f52\u4e00\u5316 / Denormalize", input: normalized, output: denormalized, time: latency.postprocessing },
    { number: 5, title: "Action Schema \u7ed1\u5b9a / Schema Binding", input: denormalized, output: interpreted, time: null },
    { number: 6, title: `\u8f66\u578b\u63d2\u4ef6\u8f6c\u6362 / Mobility Plugin \u00b7 ${plugin?.plugin_id || "\u672a\u9009\u62e9"}`, input: interpreted, output: plugin, time: null },
    { number: 7, title: "\u63a7\u5236\u4ef2\u88c1\u9884\u89c8 / Control Mux", input: { mobility_command: command }, output: { selected_source: "vla", command, published: false }, time: null },
    { number: 8, title: "\u5b89\u5168\u8fb9\u754c\u9884\u89c8 / Safety Guard", input: { selected_command: command }, output: { command, executed: execution.executed, safety_chain: "preview-only" }, time: null },
    { number: 9, title: "\u6700\u7ec8\u547d\u4ee4\u9884\u89c8 / Final cmd_vel", input: { safety_output: command }, output: { command, sent: execution.executed, execution }, time: null }
  ];
  const container = $("debugStageDetails");
  if (!container) return;
  container.replaceChildren();
  stages.forEach((stage) => {
    const details = document.createElement("details");
    details.className = "debug-stage-item";
    details.open = stage.number <= (result.stage === "preprocess" ? 2 : 9);
    const summary = document.createElement("summary");
    const badge = document.createElement("span"); badge.className = "debug-stage-number"; badge.textContent = String(stage.number);
    const title = document.createElement("strong"); title.textContent = stage.title;
    const timing = document.createElement("small"); timing.textContent = stage.time === null || stage.time === undefined ? "\u672a\u5355\u72ec\u8ba1\u65f6" : `${milliseconds(stage.time)} ms`;
    summary.append(badge, title, timing); details.appendChild(summary);
    const body = document.createElement("div"); body.className = "debug-stage-io";
    [["\u8f93\u5165 / INPUT", stage.input], ["\u8f93\u51fa / OUTPUT", stage.output]].forEach(([label, value]) => {
      const block = document.createElement("div"); block.className = "debug-stage-block";
      const heading = document.createElement("span"); heading.textContent = label;
      const code = document.createElement("pre"); code.textContent = debugJsonValue(value);
      block.append(heading, code); body.appendChild(block);
    });
    details.appendChild(body); container.appendChild(details);
  });
}
function renderDebugResult(result) {
  state.debugResult = result;
  $("debugJson").textContent = JSON.stringify(result, null, 2);
  renderDebugSteps(result);
  $("copyDebugJson").disabled = false;
  text("debugProvider", `${result.provider_id || "--"} / ${result.model_id || "--"}`);
  const plugin = result.mobility_plugin;
  const pluginTitle = plugin?.plugin_id ? `车型插件转换 / Mobility Plugin · ${plugin.plugin_id}` : "车型插件转换 / Mobility Plugin";
  const stageTitle = $("debugStageDetails")?.querySelector?.(".debug-stage-item:nth-child(7) summary strong");
  if (stageTitle) stageTitle.textContent = pluginTitle;
  const pluginInfo = $("debugMobilityPluginInfo");
  if (pluginInfo && plugin) pluginInfo.textContent = `${plugin.plugin_id || "插件"} · ${plugin.compatible ? "已完成转换预览" : (plugin.message || "不兼容当前 Action Schema")}`;
  text("debugLatency", `${milliseconds(result.latency_ms?.total)} ms`);
  const normalized = result.raw_output?.normalized_action_chunk;
  const denormalized = result.raw_output?.denormalized_actions?.values || [];
  const interpreted = result.interpreted_output?.twist_actions || [];
  text("debugActionShape", normalized?.shape ? `Shape ${normalized.shape.join(" × ")}` : "Shape --");
  const body = $("debugActionRows");
  body.replaceChildren();
  const normalizedValues = normalized?.values || [];
  const count = Math.max(normalizedValues.length, denormalized.length, interpreted.length);
  if (!count) {
    const row = body.insertRow();
    const cell = row.insertCell(); cell.colSpan = 4;
    cell.textContent = result.stage === "preprocess" ? "预处理完成；该阶段不会运行模型。" : "没有动作输出。";
    return;
  }
  for (let index = 0; index < count; index++) {
    const row = body.insertRow();
    row.insertCell().textContent = index;
    const raw = row.insertCell();
    const normalizedLine = document.createElement("code");
    const denormalizedLine = document.createElement("code");
    normalizedLine.textContent = `N ${JSON.stringify(normalizedValues[index] || [])}`;
    denormalizedLine.textContent = denormalized[index]
      ? `D ${JSON.stringify(denormalized[index])}`
      : "D — 仅原始输出（超出执行预览窗口）";
    raw.append(normalizedLine, denormalizedLine);
    row.insertCell().textContent = number(interpreted[index]?.linear_x, 6);
    row.insertCell().textContent = number(interpreted[index]?.angular_z, 6);
  }
}
async function enterShadowDebugMode() {
  if (!state.token) { openToken(); throw new Error("请先填写操作令牌"); }
  if (!confirm("确认进入 VLA_SHADOW 影子调试模式？\n\n该模式允许模型推理和结果对照，但不会把 VLA 输出发布到底盘。")) return;
  const button = $("enterShadowMode");
  button.disabled = true;
  button.textContent = "正在切换…";
  clearDebugError();
  try {
    const result = await jobFetch("/api/vla-debug/enter-shadow", { method: "POST" });
    toast(result.message || "已进入影子调试模式");
    await refresh();
  } catch (error) {
    showDebugError(error);
    toast(error.message, true);
    button.disabled = false;
    button.textContent = "重新进入影子模式";
  }
}
async function switchDebugMode() {
  if (!state.token) { openToken(); throw new Error("\u8bf7\u5148\u586b\u5199\u64cd\u4f5c\u4ee4\u724c"); }
  const assisted = state.systemMode === "VLA_ASSISTED";
  const target = assisted ? "\u5f71\u5b50\u8c03\u8bd5" : "\u5355\u6b65\u6267\u884c";
  if (!confirm(`\u786e\u8ba4\u5207\u6362\u5230${target}\u6a21\u5f0f\uff1f`)) return;
  const button = $("enterShadowMode");
  button.disabled = true;
  clearDebugError();
  try {
    const endpoint = assisted ? "/api/vla-debug/enter-shadow" : "/api/vla-debug/enter-single-step";
    const result = await jobFetch(endpoint, { method: "POST" });
    toast(result.message || `\u5df2\u5207\u6362\u5230${target}\u6a21\u5f0f`);
    await refresh();
  } catch (error) {
    showDebugError(error);
    toast(error.message, true);
  } finally {
    button.disabled = false;
  }
}
async function captureDebugObservation() {
  clearDebugError();
  if (!["VLA_SHADOW", "VLA_ASSISTED"].includes(state.systemMode)) {
    throw new Error("Current mode is not available for debug capture");
  }
  if (state.debugInputSource === "camera" && state.cameraStatus?.content_analyzed && !state.cameraStatus?.content_valid) {
    throw new Error("前视相机正在持续输出近乎全黑的画面，请检查镜头遮挡、USB 连接，或在组件控制中重启前视相机");
  }
  if (!state.token) { openToken(); throw new Error("请先填写操作令牌"); }
  setDebugBusy(true); setDebugStage("capture", "采集中");
  resetDebugImages();
  text("debugOriginalEmpty", "正在冻结当前 Observation");
  text("debugProcessedEmpty", "采集完成后，请点击“只执行预处理”生成右侧图像");
  try {
    const task = $("debugTask").value.trim();
    if (state.debugInputSource === "upload" && !state.debugUpload) {
      throw new Error("请先选择一张用于调试的图片");
    }
    const upload = state.debugInputSource === "upload" ? state.debugUpload : null;
    const result = await jobFetch("/api/vla-debug/capture", upload ? {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ task, image_base64: upload.base64, image_name: upload.name })
    } : {
      method: "POST", headers: { "Content-Type": "text/plain;charset=UTF-8" }, body: task
    });
    state.debugRunId = result.run_id;
    text("debugRunId", result.run_id);
    $("debugJson").textContent = JSON.stringify(result.observation, null, 2);
    $("copyDebugJson").disabled = false;
    $("debugActionRows").innerHTML = '<tr><td colspan="4">输入已冻结，可以执行预处理或单次推理。</td></tr>';
    await loadDebugImage("original", "debugOriginalImage", "debugOriginalEmpty");
    text("debugOriginalCaption", result.observation?.input_source === "upload" ? "冻结的用户上传图像" : "冻结的原始相机帧");
    setDebugStage("capture", result.observation?.input_source === "upload" ? "上传图已冻结" : "输入已冻结", "succeeded");
    toast(result.message);
  } finally { setDebugBusy(false); }
}
async function runDebugStage(stage) {
  clearDebugError();
  if (!state.debugRunId) throw new Error("请先采集当前帧");
  setDebugBusy(true);
  setDebugStage(stage, stage === "preprocess" ? "预处理中" : "推理中");
  try {
    const response = await jobFetch("/api/vla-debug/run", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        run_id: state.debugRunId,
        stage,
        mobility_plugin_id: $("debugMobilityPlugin")?.value || "",
        execute_command: stage === "inference" && Boolean($("debugExecuteCommand")?.checked),
        operator_confirmation: stage === "inference" && $("debugExecuteCommand")?.checked ? "I_CONFIRM_SINGLE_STEP" : "",
        execution_duration_ms: Number($("debugExecutionDuration")?.value || 150)
      })
    });
    renderDebugResult(response.result);
    await loadDebugImage("processed", "debugProcessedImage", "debugProcessedEmpty");
    setDebugStage(stage, stage === "preprocess" ? "预处理完成" : "推理完成", "succeeded");
    toast(response.message);
  } finally { setDebugBusy(false); }
}
const componentStateLabels = {
  running: ["运行正常", "RUNNING"], degraded: ["运行异常", "DEGRADED"], stopped: ["已停止", "STOPPED"],
  starting: ["启动中", "STARTING"], stopping: ["停止中", "STOPPING"], failed: ["启动失败", "FAILED"],
  external: ["外部启动", "EXTERNAL"], unavailable: ["未安装", "UNAVAILABLE"]
};
function componentLabel(stateName) { return componentStateLabels[stateName] || ["未知", String(stateName || "UNKNOWN").toUpperCase()]; }
function componentDescription(component) {
  return ({
    running: "systemd 服务、ROS 节点和健康 Topic 均正常。",
    degraded: "服务正在运行，但部分 ROS 节点或健康 Topic 尚未就绪。",
    stopped: "组件当前未运行，可以从调试台启动。",
    starting: "systemd 正在启动组件，请稍候。",
    stopping: "systemd 正在停止组件，请稍候。",
    failed: `组件异常退出，最近退出码 ${component.last_exit_code}。`,
    external: "检测到同名 ROS 节点由外部 Launch 启动；为避免重复进程，网页控制已禁用。",
    unavailable: "对应 systemd 用户服务尚未安装。"
  })[component.state] || component.message || "等待组件状态。";
}
function renderComponents() {
  const grid = $("componentGrid");
  grid.replaceChildren();
  if (!state.components.length) {
    grid.innerHTML = '<div class="job-empty"><strong>未发现受管组件</strong><span>检查 Operation Orchestrator 和 systemd 用户服务</span></div>';
    return;
  }
  state.components.forEach((component) => {
    const blackCamera = component.component_id === "front_camera" &&
      state.cameraStatus?.content_analyzed && !state.cameraStatus?.content_valid && component.state === "running";
    const effectiveState = blackCamera ? "degraded" : component.state;
    const label = componentLabel(effectiveState);
    const card = document.createElement("article");
    card.className = "component-card";
    card.innerHTML = `<div class="component-card-head"><h3>${component.display_name}<small>${component.component_id}</small></h3><span class="component-state ${effectiveState}">${label[0]} · ${label[1]}</span></div>
      <div class="component-meta"><div><small>进程 PID</small><strong>${component.pid || "--"}</strong></div><div><small>运行时间</small><strong>${component.uptime_seconds > 0 ? `${number(component.uptime_seconds, 0)} s` : "--"}</strong></div><div><small>组件分组</small><strong>${component.group_name || "--"}</strong></div></div>
      <p class="component-message">${blackCamera ? `进程和 Topic 正常，但图像内容近乎全黑（平均亮度 ${number(state.cameraStatus?.mean_intensity, 2)}）。请检查镜头遮挡、USB 连接或重启相机。` : componentDescription(component)}</p><p class="component-dependencies">依赖：${component.dependencies?.length ? component.dependencies.join(" → ") : "无"}</p>
      <div class="component-actions"><button class="button primary" data-component-action="start" ${component.can_start ? "" : "disabled"}>启动</button><button class="button secondary" data-component-action="stop" title="仅停止当前组件" ${component.can_stop ? "" : "disabled"}>停止</button><button class="button secondary" data-component-action="restart" ${component.can_restart ? "" : "disabled"}>重启</button><button class="button ghost" data-component-log>日志</button></div>`;
    card.querySelectorAll("[data-component-action]").forEach((button) => button.addEventListener("click", () => controlComponent(component.component_id, button.dataset.componentAction)));
    card.querySelector("[data-component-log]").addEventListener("click", () => loadComponentLog(component.component_id, component.display_name));
    grid.appendChild(card);
  });
}
async function refreshComponents(showError = false) {
  try {
    const response = await fetch("/api/components", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const result = await response.json();
    state.components = result.components || [];
    state.componentProfiles = result.profiles || [];
    badge($("componentApiState"), true, `${state.components.length} 个组件`, "编排离线");
    renderComponents();
  } catch (error) {
    badge($("componentApiState"), false, "编排在线", "编排离线");
    if (showError) toast(error.message, true);
  }
}
function managedDependents(componentId) {
  const result = [];
  const visited = new Set();
  function visit(parentId) {
    state.components.filter((component) => component.managed && component.dependencies?.includes(parentId)).forEach((component) => {
      if (visited.has(component.component_id)) return;
      visited.add(component.component_id);
      visit(component.component_id);
      result.push(component);
    });
  }
  visit(componentId);
  return result;
}
async function controlComponent(componentId, action) {
  const component = state.components.find((item) => item.component_id === componentId);
  const displayName = component?.display_name || componentId;
  const dependents = action === "stop" ? managedDependents(componentId) : [];
  if (action === "stop") {
    const warning = dependents.length ? `\n\n下游组件将保持运行，但可能因输入中断进入无数据或降级状态：${dependents.map((item) => item.display_name).join("、")}` : "";
    if (!confirm(`确认仅停止“${displayName}”？${warning}\n\n如需停止整条链路，请使用上方的一键停止。`)) return;
  }
  if (action === "restart" && !confirm(`确认重启“${displayName}”？`)) return;
  $("componentGrid").classList.add("component-action-busy");
  try {
    await jobFetch("/api/components/control", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ component_id: componentId, action, force: true }) });
    const actionName = ({ start: "启动", stop: "停止", restart: "重启" })[action] || action;
    toast(`${displayName}已${actionName}`);
    await refreshComponents();
    setTimeout(refreshComponents, 1200);
    setTimeout(refreshComponents, 4200);
  } catch (error) { toast(error.message, true); }
  finally { $("componentGrid").classList.remove("component-action-busy"); }
}
async function controlProfile(profileId, action) {
  if (action === "stop" && !confirm("确认停止完整受管 Shadow 链路？Vehicle Ops 管理面会继续运行。")) return;
  document.querySelectorAll("[data-profile-action]").forEach((button) => button.disabled = true);
  try {
    const result = await jobFetch("/api/profiles/control", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ profile_id: profileId, action }) });
    toast(result.message); await refreshComponents();
  } catch (error) { toast(error.message, true); }
  finally { document.querySelectorAll("[data-profile-action]").forEach((button) => button.disabled = false); }
}
async function loadComponentLog(componentId, displayName = componentId) {
  state.selectedComponentId = componentId;
  text("componentLogTitle", `${displayName} · ${componentId}`);
  $("refreshComponentLog").disabled = false;
  text("componentLog", "正在读取日志…");
  try { text("componentLog", await jobFetch(`/api/components/${componentId}/log`)); }
  catch (error) { text("componentLog", error.message); toast(error.message, true); }
}
document.querySelectorAll("[data-profile-action]").forEach((button) => button.addEventListener("click", () => {
  const [profileId, action] = button.dataset.profileAction.split(":"); controlProfile(profileId, action);
}));
$("refreshComponentLog").addEventListener("click", () => {
  const component = state.components.find((item) => item.component_id === state.selectedComponentId);
  if (component) loadComponentLog(component.component_id, component.display_name);
});
$("startCameraQuick").addEventListener("click", () => controlComponent("front_camera", "start"));
document.querySelectorAll("[data-debug-source]").forEach((button) => button.addEventListener("click", () => setDebugInputSource(button.dataset.debugSource)));
$("debugUploadInput").addEventListener("change", async (event) => {
  try { await prepareDebugUpload(event.target.files?.[0]); }
  catch (error) { clearDebugUpload(); showDebugError(error); toast(error.message, true); }
});
$("clearDebugUpload").addEventListener("click", clearDebugUpload);
$("debugUploadDropzone").addEventListener("dragover", (event) => { event.preventDefault(); $("debugUploadDropzone").classList.add("dragging"); });
$("debugUploadDropzone").addEventListener("dragleave", () => $("debugUploadDropzone").classList.remove("dragging"));
$("debugUploadDropzone").addEventListener("drop", async (event) => {
  event.preventDefault();
  $("debugUploadDropzone").classList.remove("dragging");
  try { await prepareDebugUpload(event.dataTransfer?.files?.[0]); }
  catch (error) { clearDebugUpload(); showDebugError(error); toast(error.message, true); }
});
setDebugInputSource("camera");
$("enterShadowMode").addEventListener("click", () => {
  switchDebugMode().catch((error) => { showDebugError(error); toast(error.message, true); });
});
$("captureDebug").addEventListener("click", async () => {
  try { await captureDebugObservation(); } catch (error) { setDebugStage("capture", "执行失败", "failed"); showDebugError(error); toast(error.message, true); setDebugBusy(false); }
});
$("preprocessDebug").addEventListener("click", async () => {
  try { await runDebugStage("preprocess"); } catch (error) { setDebugStage("preprocess", "执行失败", "failed"); showDebugError(error); toast(error.message, true); setDebugBusy(false); }
});
$("inferenceDebug").addEventListener("click", async () => {
  const execute = Boolean($("debugExecuteCommand")?.checked);
  const prompt = execute
    ? "Confirm sending one single-step vehicle command? Ensure the vehicle area is safe."
    : "Confirm one VLA inference? Preview only; no vehicle command will be published.";
  if (!confirm(prompt)) return;
  try { await runDebugStage("inference"); } catch (error) { setDebugStage("inference", "????", "failed"); showDebugError(error); toast(error.message, true); setDebugBusy(false); }
});
$("copyDebugJson").addEventListener("click", async () => {
  try { await navigator.clipboard.writeText($("debugJson").textContent); toast("调试 JSON 已复制"); }
  catch { toast("浏览器禁止剪贴板访问，请手动复制", true); }
});
document.querySelectorAll("[data-inspector-mode]").forEach((button) => button.addEventListener("click", () => setInspectorMode(button.dataset.inspectorMode)));
$("refreshPipelineHistory").addEventListener("click", () => refreshPipelineHistory(true));
$("debugMobilityPlugin")?.addEventListener("change", updateMobilityPluginInfo);
loadMobilityPlugins();
setInspectorMode("live");updateCommand();
setView(["monitor", "capture", "debug", "storage", "tools"].includes(location.hash.slice(1)) ? location.hash.slice(1) : "monitor");
refresh();
refreshPipeline();
refreshPipelineHistory();
refreshComponents();
refreshJobs();
refreshDatasets();

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${bytes} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let amount = bytes;
  let index = -1;
  do { amount /= 1024; index += 1; } while (amount >= 1024 && index < units.length - 1);
  return `${amount.toFixed(amount >= 10 ? 1 : 2)} ${units[index]}`;
}
function storageDate(value) {
  if (!value?.sec) return "未知时间";
  return new Date(Number(value.sec) * 1000).toLocaleString("zh-CN", { hour12: false });
}
function renderStorageCategories() {
  const grid = $("storageCategoryGrid");
  grid.replaceChildren();
  (state.storage?.categories || []).forEach((category) => {
    const card = document.createElement("button");
    card.className = `storage-category-card${state.selectedStorageCategory === category.category_id ? " active" : ""}`;
    card.innerHTML = `<span><strong>${category.display_name}</strong><small>${category.path}</small></span><span class="storage-category-value"><b>${formatBytes(category.bytes)}</b><small>${category.item_count} 项 · ${category.cleanup_allowed ? "可选择清理" : "只读保护"}</small></span>`;
    card.addEventListener("click", () => loadStorageItems(category.category_id, category.display_name));
    grid.appendChild(card);
  });
}
async function refreshStorage(showError = false) {
  try {
    const response = await fetch("/api/storage", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    state.storage = await response.json();
    const percent = Number(state.storage.used_percent || 0);
    text("storagePercent", `${percent.toFixed(1)}%`);
    text("storageTotal", formatBytes(state.storage.total_bytes));
    text("storageUsed", formatBytes(state.storage.used_bytes));
    text("storageAvailable", formatBytes(state.storage.available_bytes));
    text("storageMessage", state.storage.message);
    $("storageProgress").style.width = `${Math.min(percent, 100)}%`;
    $("storageRing").style.setProperty("--storage-percent", `${Math.min(percent, 100) * 3.6}deg`);
    $("storageRing").dataset.level = state.storage.level;
    badge($("storageApiBadge"), state.storage.level === "normal", `${state.storage.categories.length} 个分类`, state.storage.level === "critical" ? "空间严重不足" : "空间告警");
    renderStorageCategories();
  } catch (error) {
    badge($("storageApiBadge"), false, "存储在线", "存储离线");
    if (showError) toast(error.message, true);
  }
}
async function loadStorageItems(categoryId, displayName) {
  try {
    const result = await jobFetch(`/api/storage/items/${encodeURIComponent(categoryId)}`);
    state.selectedStorageCategory = categoryId;
    state.storageItems = result.items || [];
    text("storageItemsTitle", `${displayName} · ${state.storageItems.length} 项`);
    renderStorageCategories();
    renderStorageItems();
  } catch (error) { toast(error.message, true); }
}
function selectedStorageItems() {
  return [...document.querySelectorAll("[data-storage-item]:checked")].map((input) => input.value);
}
function updateStorageSelection() {
  const selected = new Set(selectedStorageItems());
  const bytes = state.storageItems.filter((item) => selected.has(item.item_id)).reduce((sum, item) => sum + Number(item.bytes || 0), 0);
  text("storageSelectionCount", selected.size ? `已选择 ${selected.size} 项` : "未选择项目");
  text("storageSelectionBytes", formatBytes(bytes));
  $("cleanupStorage").disabled = selected.size === 0;
}
function renderStorageItems() {
  const list = $("storageItemList");
  list.replaceChildren();
  if (!state.storageItems.length) {
    list.innerHTML = '<div class="job-empty"><strong>当前分类为空</strong><span>没有可展示的数据</span></div>';
  } else {
    state.storageItems.forEach((item) => {
      const row = document.createElement("label");
      row.className = `storage-item${item.is_protected ? " protected" : ""}`;
      row.innerHTML = `<input type="checkbox" data-storage-item value="${item.item_id}" ${item.is_protected ? "disabled" : ""}><span><strong>${item.display_name}</strong><small>${item.path} · ${storageDate(item.modified_at)}</small></span><span class="storage-item-size"><b>${formatBytes(item.bytes)}</b><small>${item.message}</small></span>`;
      row.querySelector("input").addEventListener("change", updateStorageSelection);
      list.appendChild(row);
    });
  }
  $("selectAllStorage").disabled = !state.storageItems.some((item) => !item.protected);
  updateStorageSelection();
}
async function cleanupSelectedStorage() {
  const itemIds = selectedStorageItems();
  if (!itemIds.length) return;
  const payload = { category_id: state.selectedStorageCategory, item_ids: itemIds, dry_run: true };
  try {
    const preview = await jobFetch("/api/storage/cleanup", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) });
    if (!confirm(`确认清理 ${preview.item_count} 项？

预计释放 ${formatBytes(preview.bytes)}。此操作不可恢复。`)) return;
    payload.dry_run = false;
    const result = await jobFetch("/api/storage/cleanup", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) });
    toast(`清理完成，释放 ${formatBytes(result.bytes)}`);
    const category = state.storage.categories.find((item) => item.category_id === state.selectedStorageCategory);
    await refreshStorage();
    await loadStorageItems(state.selectedStorageCategory, category?.display_name || state.selectedStorageCategory);
  } catch (error) { toast(error.message, true); }
}
$("refreshStorage").addEventListener("click", () => refreshStorage(true));
$("selectAllStorage").addEventListener("click", () => {
  document.querySelectorAll("[data-storage-item]:not(:disabled)").forEach((input) => {input.checked = true;});
  updateStorageSelection();
});
$("cleanupStorage").addEventListener("click", cleanupSelectedStorage);
initMetricTooltip();
refreshStorage();
setInterval(refreshStorage, 10000);
setInterval(refresh, 1000);
setInterval(refreshPipeline, 1000);
setInterval(refreshActiveDebug, 1000);
setInterval(refreshPipelineHistory, 5000);
setInterval(refreshComponents, 2000);
setInterval(() => refreshJobs(), 2000);
setInterval(() => refreshDatasets(), 5000);
setInterval(() => refreshModels(), 5000);
refreshModels();
