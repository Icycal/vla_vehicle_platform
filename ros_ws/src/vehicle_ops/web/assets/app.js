const $ = (id) => document.getElementById(id);
const state = { token: sessionStorage.getItem("vehicleOpsToken") || "", cameraTick: 0, selectedJobId: "", jobs: [], debugRunId: "", debugResult: null, debugImageUrls: {}, pipelineTrace: null, pipelineHistory: [], selectedPipelineStageId: "", selectedPipelineHistoryId: "", inspectorMode: "live" };
const text = (id, value) => { $(id).textContent = value ?? "—"; };
const number = (value, digits = 1) => Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : "--";
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
}
function badge(node, healthy, yes, no) {
  node.className = `pill ${healthy ? "success" : "warning"}`;
  node.innerHTML = `<i></i>${healthy ? yes : no}`;
}function render(data) {
  badge($("connectionBadge"), true, "API ONLINE", "连接中");
  text("modeName", data.system?.mode_name || "NO STATE");
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
  const used = data.host?.memory_total_mb - data.host?.memory_available_mb;
  const ratio = data.host?.memory_total_mb ? used / data.host.memory_total_mb * 100 : NaN;
  text("memoryState", Number.isFinite(ratio) ? `${ratio.toFixed(0)}%` : "--");
  text("hostMeta", `Load ${number(data.host?.load_one, 2)} · Up ${number((data.host?.uptime_seconds || 0) / 3600, 1)}h`);
  text("cameraResolution", `${data.observation?.image_width || "--"} × ${data.observation?.image_height || "--"}`);
  text("cameraAge", data.camera?.age_ms >= 0 ? `更新 ${number(data.camera.age_ms / 1000, 1)}s 前` : "更新时间 --");
  text("calibrationState", data.observation?.camera_calibrated ? "已标定" : "未标定");
  badge($("cameraBadge"), Boolean(data.camera?.fresh), "实时", "无画面");
  if (data.camera?.received) {
    $("cameraImage").style.display = "block";
    $("cameraEmpty").style.display = "none";
    if (++state.cameraTick % 2 === 0) $("cameraImage").src = `/api/camera/front.jpg?t=${Date.now()}`;
  } else {
    $("cameraImage").style.display = "none";
    $("cameraEmpty").style.display = "grid";
  }
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
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    render(await response.json());
  } catch (error) {
    badge($("connectionBadge"), false, "API ONLINE", "API OFFLINE");
    text("systemMessage", error.message);
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
async function refreshJobs(showError = false) {
  if (!state.token) {
    $("jobApiBadge").className = "pill neutral";
    $("jobApiBadge").innerHTML = "<i></i>需要令牌";
    return;
  }
  try {
    const result = await jobFetch("/api/jobs");
    state.jobs = result.jobs || [];
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
    $("cancelJob").disabled = !["queued", "running"].includes(job.state);
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
function pipelineStageDescription(stage) {
  const base = pipelineStageDescriptions[stage?.stage_id] || "正在读取当前阶段状态。";
  if (["FAILED", "REJECTED", "STALE"].includes(stage?.status) && stage?.message) return `${base} 当前异常：${stage.message}`;
  return base;
}function setInspectorMode(mode) {
  state.inspectorMode = mode;
  document.querySelectorAll("[data-inspector-mode]").forEach((button) => {
    const active = button.dataset.inspectorMode === mode;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", active ? "true" : "false");
  });
  document.querySelectorAll("[data-inspector-panel]").forEach((panel) => panel.classList.toggle("active", panel.dataset.inspectorPanel === mode));
  if (mode === "history") refreshPipelineHistory(true);
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
  if (camera && ["LIVE", "READY"].includes(camera.status)) {
    $("pipelineLiveImage").style.display = "block";
    $("pipelineLiveEmpty").style.display = "none";
    $("pipelineLiveImage").src = `/api/camera/front.jpg?t=${Date.now()}`;
  } else {
    $("pipelineLiveImage").style.display = "none";
    $("pipelineLiveEmpty").style.display = "grid";
  }
}
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
  $("captureDebug").disabled = busy;
  $("preprocessDebug").disabled = busy || !state.debugRunId;
  $("inferenceDebug").disabled = busy || !state.debugRunId;
}
function setDebugStage(stage, label, stateClass = "running") {
  const stages = ["Capture", "Contract", "Preprocess", "Inference", "Denormalize", "Adapter", "Safety"];
  const activeIndex = ({ capture: 0, preprocess: 2, inference: 3 })[stage] ?? 0;
  const completedIndex = stateClass === "succeeded" ? ({ capture: 1, preprocess: 2, inference: 6 })[stage] : activeIndex - 1;
  stages.forEach((name, index) => {
    const node = $("debugStep" + name);
    node.classList.toggle("active", stateClass === "running" && index === activeIndex);
    node.classList.toggle("completed", index <= completedIndex);
    node.classList.toggle("failed", stateClass === "failed" && index === activeIndex);
  });
  $("debugStageState").className = `job-state ${stateClass}`;
  $("debugStageState").textContent = label;
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
function renderDebugResult(result) {
  state.debugResult = result;
  $("debugJson").textContent = JSON.stringify(result, null, 2);
  $("copyDebugJson").disabled = false;
  text("debugProvider", `${result.provider_id || "--"} / ${result.model_id || "--"}`);
  text("debugLatency", `${number(result.latency_ms?.total, 1)} ms`);
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
async function captureDebugObservation() {
  clearDebugError();
  if (!state.token) { openToken(); throw new Error("请先填写操作令牌"); }
  setDebugBusy(true); setDebugStage("capture", "采集中");
  try {
    const result = await jobFetch("/api/vla-debug/capture", {
      method: "POST", headers: { "Content-Type": "text/plain;charset=UTF-8" },
      body: $("debugTask").value.trim()
    });
    state.debugRunId = result.run_id;
    text("debugRunId", result.run_id);
    $("debugJson").textContent = JSON.stringify(result.observation, null, 2);
    $("copyDebugJson").disabled = false;
    $("debugActionRows").innerHTML = '<tr><td colspan="4">输入已冻结，可以执行预处理或单次推理。</td></tr>';
    await loadDebugImage("original", "debugOriginalImage", "debugOriginalEmpty");
    setDebugStage("capture", "输入已冻结", "succeeded");
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
      body: JSON.stringify({ run_id: state.debugRunId, stage })
    });
    renderDebugResult(response.result);
    await loadDebugImage("processed", "debugProcessedImage", "debugProcessedEmpty");
    setDebugStage(stage, stage === "preprocess" ? "预处理完成" : "推理完成", "succeeded");
    toast(response.message);
  } finally { setDebugBusy(false); }
}
$("captureDebug").addEventListener("click", async () => {
  try { await captureDebugObservation(); } catch (error) { setDebugStage("capture", "执行失败", "failed"); showDebugError(error); toast(error.message, true); setDebugBusy(false); }
});
$("preprocessDebug").addEventListener("click", async () => {
  try { await runDebugStage("preprocess"); } catch (error) { setDebugStage("preprocess", "执行失败", "failed"); showDebugError(error); toast(error.message, true); setDebugBusy(false); }
});
$("inferenceDebug").addEventListener("click", async () => {
  if (!confirm("确认执行一次 VLA 推理？结果只用于 Shadow 调试，不会发布控制命令。")) return;
  try { await runDebugStage("inference"); } catch (error) { setDebugStage("inference", "执行失败", "failed"); showDebugError(error); toast(error.message, true); setDebugBusy(false); }
});
$("copyDebugJson").addEventListener("click", async () => {
  try { await navigator.clipboard.writeText($("debugJson").textContent); toast("调试 JSON 已复制"); }
  catch { toast("浏览器禁止剪贴板访问，请手动复制", true); }
});
document.querySelectorAll("[data-inspector-mode]").forEach((button) => button.addEventListener("click", () => setInspectorMode(button.dataset.inspectorMode)));
$("refreshPipelineHistory").addEventListener("click", () => refreshPipelineHistory(true));
setInspectorMode("live");updateCommand();
setView(["monitor", "capture", "debug", "tools"].includes(location.hash.slice(1)) ? location.hash.slice(1) : "monitor");
refresh();
refreshPipeline();
refreshPipelineHistory();
refreshJobs();
setInterval(refresh, 1000);
setInterval(refreshPipeline, 1000);
setInterval(refreshPipelineHistory, 5000);
setInterval(() => refreshJobs(), 2000);