const $ = (id) => document.getElementById(id);
const state = { token: sessionStorage.getItem("vehicleOpsToken") || "", cameraTick: 0, selectedJobId: "", jobs: [] };
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
updateCommand();
setView(["monitor", "capture", "tools"].includes(location.hash.slice(1)) ? location.hash.slice(1) : "monitor");
refresh();
refreshJobs();
setInterval(refresh, 1000);
setInterval(() => refreshJobs(), 2000);