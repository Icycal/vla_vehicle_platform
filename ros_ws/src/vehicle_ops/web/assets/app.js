const $ = (id) => document.getElementById(id);
const state = { token: sessionStorage.getItem("vehicleOpsToken") || "", cameraSequence: 0, pipelineCameraSequence: 0, cameraStatus: null, pipelineCameraActive: false, selectedJobId: "", jobs: [], debugRunId: "", debugResult: null, debugImageUrls: {}, pipelineTrace: null, pipelineHistory: [], selectedPipelineStageId: "", selectedPipelineHistoryId: "", inspectorMode: "live", components: [], componentProfiles: [], selectedComponentId: "", storage: null, storageItems: [], selectedStorageCategory: "" };
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
  if (name === "storage") refreshStorage(true);
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
  text(ids.label, camera?.fresh ? "实时快照" : "最后快照");
  $(ids.image).style.display = received ? "block" : "none";
  $(ids.empty).style.display = received ? "none" : "grid";
  const sequence = Number(camera?.frame_sequence || 0);
  if (!received || sequence <= 0 || sequence === state[sequenceKey]) return;
  state[sequenceKey] = sequence;
  $(ids.image).src = `/api/camera/front.jpg?t=${sequence}`;
  text(ids.time, frameTime(camera.frame_received_at_ms));
  text(ids.sequence, `帧 #${sequence}`);
  $(ids.overlay).classList.remove("updated");
  void $(ids.overlay).offsetWidth;
  $(ids.overlay).classList.add("updated");
}
function render(data) {
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
  const cameraReceived = Boolean(data.camera?.received);
  state.cameraStatus = data.camera || null;
  badge($("cameraBadge"), Boolean(data.camera?.fresh), "实时快照", cameraReceived ? "画面停滞" : "无画面");
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
    const label = componentLabel(component.state);
    const card = document.createElement("article");
    card.className = "component-card";
    card.innerHTML = `<div class="component-card-head"><h3>${component.display_name}<small>${component.component_id}</small></h3><span class="component-state ${component.state}">${label[0]} · ${label[1]}</span></div>
      <div class="component-meta"><div><small>进程 PID</small><strong>${component.pid || "--"}</strong></div><div><small>运行时间</small><strong>${component.uptime_seconds > 0 ? `${number(component.uptime_seconds, 0)} s` : "--"}</strong></div><div><small>组件分组</small><strong>${component.group_name || "--"}</strong></div></div>
      <p class="component-message">${componentDescription(component)}</p><p class="component-dependencies">依赖：${component.dependencies?.length ? component.dependencies.join(" → ") : "无"}</p>
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
setView(["monitor", "capture", "debug", "storage", "tools"].includes(location.hash.slice(1)) ? location.hash.slice(1) : "monitor");
refresh();
refreshPipeline();
refreshPipelineHistory();
refreshComponents();
refreshJobs();

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
refreshStorage();
setInterval(refreshStorage, 10000);
setInterval(refresh, 1000);
setInterval(refreshPipeline, 1000);
setInterval(refreshPipelineHistory, 5000);
setInterval(refreshComponents, 2000);
setInterval(() => refreshJobs(), 2000);
