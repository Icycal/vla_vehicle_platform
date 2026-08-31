const $ = (id) => document.getElementById(id);
function makeControllerId() {
  const randomPart = globalThis.crypto?.getRandomValues ?
    Array.from(crypto.getRandomValues(new Uint8Array(6)), value => value.toString(16).padStart(2, "0")).join("") :
    `${Date.now().toString(16)}${Math.random().toString(16).slice(2, 8)}`;
  return `mobile-${randomPart}`;
}
const state = {
  token: sessionStorage.getItem("vehicleOpsToken") || "",
  controllerId: localStorage.getItem("chituTeleopControllerId") || makeControllerId(),
  lease: null,
  serverTimeMs: Date.now(),
  sequence: 0,
  pointerId: null,
  linear: 0,
  angular: 0,
  deadman: false,
  commandPending: false,
  acquiring: false,
  components: [],
  statusOnline: false,
  toastTimer: null,
  cameraSequence: 0,
  cameraMode: localStorage.getItem("chituTeleopCameraMode") === "floating" ? "floating" : "fixed",
  cameraPosition: null,
  cameraDrag: null
};
localStorage.setItem("chituTeleopControllerId", state.controllerId);

function toast(message, error = false) {
  const target = $("teleopToast");
  target.textContent = message;
  target.classList.toggle("error", error);
  target.classList.add("show");
  clearTimeout(state.toastTimer);
  state.toastTimer = setTimeout(() => target.classList.remove("show"), 2800);
}
function setText(id, value) { $(id).textContent = value; }
function clamp(value, minimum, maximum) { return Math.min(maximum, Math.max(minimum, value)); }
function readCameraPosition() {
  try {
    const position = JSON.parse(localStorage.getItem("chituTeleopCameraPosition") || "null");
    return Number.isFinite(position?.x) && Number.isFinite(position?.y) ? position : null;
  } catch (_) {
    return null;
  }
}
function cameraFloatBounds() {
  const panel = $("cameraPanel");
  const emergency = $("emergencyStop").getBoundingClientRect();
  const margin = 8;
  return {
    minimumX: margin,
    maximumX: Math.max(margin, window.innerWidth - panel.offsetWidth - margin),
    minimumY: margin,
    maximumY: Math.max(margin, emergency.top - panel.offsetHeight - margin)
  };
}
function applyCameraPosition(position, persist = false) {
  const panel = $("cameraPanel");
  const bounds = cameraFloatBounds();
  const next = {
    x: clamp(position.x, bounds.minimumX, bounds.maximumX),
    y: clamp(position.y, bounds.minimumY, bounds.maximumY)
  };
  panel.style.left = `${next.x}px`;
  panel.style.top = `${next.y}px`;
  state.cameraPosition = next;
  if (persist) localStorage.setItem("chituTeleopCameraPosition", JSON.stringify(next));
}
function defaultCameraPosition() {
  const panel = $("cameraPanel");
  const header = document.querySelector(".teleop-header").getBoundingClientRect();
  return {x: window.innerWidth - panel.offsetWidth - 8, y: Math.max(8, header.bottom + 8)};
}
function setCameraMode(mode, announce = false) {
  const floating = mode === "floating";
  const panel = $("cameraPanel");
  state.cameraMode = floating ? "floating" : "fixed";
  state.cameraDrag = null;
  panel.classList.toggle("floating", floating);
  panel.classList.remove("dragging");
  document.body.classList.toggle("camera-floating", floating);
  $("cameraModeToggle").setAttribute("aria-pressed", String(floating));
  $("cameraModeToggle").setAttribute(
    "aria-label", floating ? "将相机恢复为固定区域" : "将相机切换为悬浮窗口");
  setText("cameraModeLabel", floating ? "固定" : "悬浮");
  localStorage.setItem("chituTeleopCameraMode", state.cameraMode);
  if (floating) {
    requestAnimationFrame(() => applyCameraPosition(
      state.cameraPosition || readCameraPosition() || defaultCameraPosition()));
  } else {
    panel.style.removeProperty("left");
    panel.style.removeProperty("top");
  }
  if (announce) toast(floating ? "相机已切换为可拖动悬浮窗口" : "相机已恢复为固定区域");
}
function toggleCameraMode() {
  setCameraMode(state.cameraMode === "floating" ? "fixed" : "floating", true);
}
function startCameraDrag(event) {
  if (state.cameraMode !== "floating" || event.button !== 0 ||
    event.target.closest("#cameraModeToggle"))
  {
    return;
  }
  const panel = $("cameraPanel");
  const bounds = panel.getBoundingClientRect();
  state.cameraDrag = {
    pointerId: event.pointerId,
    offsetX: event.clientX - bounds.left,
    offsetY: event.clientY - bounds.top
  };
  panel.setPointerCapture(event.pointerId);
  panel.classList.add("dragging");
  event.preventDefault();
}
function moveCameraDrag(event) {
  if (!state.cameraDrag || state.cameraDrag.pointerId !== event.pointerId) return;
  applyCameraPosition({
    x: event.clientX - state.cameraDrag.offsetX,
    y: event.clientY - state.cameraDrag.offsetY
  });
  event.preventDefault();
}
function finishCameraDrag(event) {
  if (!state.cameraDrag || state.cameraDrag.pointerId !== event.pointerId) return;
  const panel = $("cameraPanel");
  state.cameraDrag = null;
  panel.classList.remove("dragging");
  if (panel.hasPointerCapture(event.pointerId)) panel.releasePointerCapture(event.pointerId);
  if (state.cameraPosition) {
    localStorage.setItem("chituTeleopCameraPosition", JSON.stringify(state.cameraPosition));
  }
}
function requestHeaders(write, json = true) {
  const headers = {};
  if (write && state.token) headers["X-Ops-Token"] = state.token;
  if (json) headers["Content-Type"] = "application/json";
  return headers;
}
async function api(path, options = {}) {
  const method = options.method || "GET";
  const write = method !== "GET";
  const response = await fetch(path, {
    method,
    headers: {...requestHeaders(write, options.json !== false), ...(options.headers || {})},
    body: options.body,
    cache: "no-store",
    keepalive: Boolean(options.keepalive)
  });
  const contentType = response.headers.get("content-type") || "";
  const payload = contentType.includes("json") ? await response.json() : await response.text();
  if (!response.ok) {
    throw new Error(payload?.message || payload || `HTTP ${response.status}`);
  }
  return payload;
}
function hasOwnLease() {
  return Boolean(state.lease?.active && state.lease.controller_id === state.controllerId);
}
function tokenRequired() {
  if (state.token) return false;
  openSettings();
  toast("请先保存 Operator Token", true);
  return true;
}
function openSettings() {
  $("settingsBackdrop").hidden = false;
  $("settingsSheet").classList.add("open");
  $("settingsSheet").setAttribute("aria-hidden", "false");
  $("tokenInput").value = state.token;
}
function closeSettings() {
  $("settingsSheet").classList.remove("open");
  $("settingsSheet").setAttribute("aria-hidden", "true");
  setTimeout(() => $("settingsBackdrop").hidden = true, 250);
}
function speedLimits() {
  return {
    linear: Number($("linearLimit").value),
    angular: Number($("angularLimit").value)
  };
}
function updateLimitLabels() {
  setText("linearLimitValue", `${Number($("linearLimit").value).toFixed(2)} m/s`);
  setText("angularLimitValue", `${Number($("angularLimit").value).toFixed(2)} rad/s`);
}

async function acquireLease(silent = false) {
  if (state.acquiring || tokenRequired()) return;
  if (!silent && !$("safetyConfirmed").checked) {
    toast("请先确认车辆周围安全", true);
    return;
  }
  state.acquiring = true;
  $("acquireLease").disabled = true;
  try {
    const limits = speedLimits();
    const result = await api("/api/teleop/acquire", {
      method: "POST",
      body: JSON.stringify({
        controller_id: state.controllerId,
        duration_seconds: 30,
        max_linear_velocity: limits.linear,
        max_angular_velocity: limits.angular
      })
    });
    state.lease = result.lease;
    state.sequence = 0;
    renderLease();
    if (!silent) toast("已取得车辆控制权");
  } catch (error) {
    if (!silent) toast(error.message, true);
  } finally {
    state.acquiring = false;
    $("acquireLease").disabled = hasOwnLease();
  }
}
async function sendCommand(deadman = state.deadman, keepalive = false) {
  if (!hasOwnLease() || state.commandPending) return;
  state.commandPending = true;
  const body = JSON.stringify({
    lease_id: state.lease.lease_id,
    controller_id: state.controllerId,
    sequence: ++state.sequence,
    deadman,
    linear: deadman ? state.linear : 0,
    angular: deadman ? state.angular : 0,
    valid_for_ms: 250
  });
  try {
    await api("/api/teleop/command", {method: "POST", body, keepalive});
  } catch (error) {
    if (error.message.includes("lease")) {
      state.lease = null;
      stopJoystick(false);
      renderLease();
    }
  } finally {
    state.commandPending = false;
  }
}
async function releaseLease(showToast = true, keepalive = false) {
  if (!hasOwnLease()) return;
  stopJoystick(false);
  await sendCommand(false, keepalive);
  const lease = state.lease;
  try {
    await api("/api/teleop/release", {
      method: "POST",
      body: JSON.stringify({lease_id: lease.lease_id, controller_id: state.controllerId}),
      keepalive
    });
    if (showToast) toast("已释放车辆控制权");
  } catch (error) {
    if (showToast) toast(error.message, true);
  } finally {
    state.lease = null;
    renderLease();
  }
}

function renderLease() {
  const own = hasOwnLease();
  const activeOther = state.lease?.active && !own;
  $("joystick").classList.toggle("disabled", !own);
  $("acquireLease").disabled = own || activeOther || state.acquiring;
  $("releaseLease").disabled = !own;
  setText("leaseState", own ? "本机已接管" : activeOther ? "其他终端占用" : "未接管");
  setText("deadmanHint", own ? "按住摇杆车辆才会运动；松手 300ms 内自动归零" : "申请控制权后才能使用摇杆");
  if (!own) {
    $("leaseCountdown").classList.remove("active");
    setText("leaseCountdown", activeOther ? "占用" : "--");
  }
}
function renderStatus(payload) {
  state.statusOnline = true;
  state.serverTimeMs = Number(payload.server_time_ms || Date.now());
  state.lease = payload.lease_received ? payload.lease : null;
  const modeName = payload.system?.mode_name || "--";
  setText("modeReadout", modeName);
  setText("executedLinear", Number(payload.executed_command?.linear_x || 0).toFixed(2));
  setText("executedAngular", Number(payload.executed_command?.angular_z || 0).toFixed(2));
  const episode = payload.episode || {};
  const recording = episode.state_name === "RECORDING";
  setText("episodeState", recording ? "记录中" : "未记录");
  $("recordLight").classList.toggle("active", recording);
  setText("episodeElapsed", `${Number(episode.elapsed_seconds || 0).toFixed(1)} s`);
  setText("episodeImages", String(episode.image_count || 0));
  setText("episodeMessages", String(episode.message_count || 0));
  $("startEpisode").disabled = recording;
  $("finishEpisode").disabled = !recording;
  $("discardEpisode").disabled = !recording;
  if (recording && episode.task && !$("episodeTask").value) $("episodeTask").value = episode.task;
  $("networkChip").className = "status-chip online";
  $("networkChip").innerHTML = "<i></i>在线";
  renderLease();
  if (hasOwnLease()) {
    const remaining = Math.max(0, state.lease.expires_at_ms - state.serverTimeMs);
    setText("leaseCountdown", `${Math.ceil(remaining / 1000)}s`);
    $("leaseCountdown").classList.add("active");
    if (remaining < 8000 && !state.acquiring) acquireLease(true);
  }
}
async function refreshStatus() {
  try {
    renderStatus(await api("/api/teleop/status", {json: false}));
  } catch (_) {
    state.statusOnline = false;
    $("networkChip").className = "status-chip offline";
    $("networkChip").innerHTML = "<i></i>离线";
  }
}
async function refreshComponents() {
  try {
    const payload = await api("/api/components", {json: false});
    state.components = payload.components || [];
    const component = (id) => state.components.find((item) => item.component_id === id);
    const label = (item) => !item ? "未注册" : item.state === "running" ? "正常" : item.state === "degraded" ? "异常" : item.state === "stopped" ? "已停止" : item.state;
    const chassis = component("vehicle_chassis");
    setText("chassisState", label(chassis));
    setText("chassisDetailState", label(chassis));
    setText("runtimeState", label(component("runtime_core")));
    setText("observationState", label(component("observation_pipeline")));
    setText("cameraState", label(component("front_camera")));
    $("startChassis").disabled = !chassis?.can_start;
    $("startChassis").textContent = chassis?.state === "running" ? "底盘组件运行中" : "启动底盘组件";
  } catch (_) {}
}

function joystickValues(event) {
  const rect = $("joystick").getBoundingClientRect();
  const centerX = rect.left + rect.width / 2;
  const centerY = rect.top + rect.height / 2;
  const radius = rect.width * .34;
  let dx = event.clientX - centerX;
  let dy = event.clientY - centerY;
  const distance = Math.hypot(dx, dy);
  if (distance > radius) {
    dx = dx / distance * radius;
    dy = dy / distance * radius;
  }
  const shape = (value) => Math.sign(value) * Math.pow(Math.abs(value), 1.35);
  state.linear = Math.abs(dy / radius) < .06 ? 0 : shape(clamp(-dy / radius, -1, 1));
  state.angular = Math.abs(dx / radius) < .06 ? 0 : shape(clamp(-dx / radius, -1, 1));
  $("joystickKnob").style.transform = `translate3d(${dx}px, ${dy}px, 0)`;
  renderCommandPreview(dx / radius, dy / radius);
}
function renderCommandPreview(dx = 0, dy = 0) {
  setText("linearPreview", `${Math.round(state.linear * 100)}%`);
  setText("angularPreview", `${Math.round(state.angular * 100)}%`);
  $("vectorDot").style.transform = `translate(${clamp(dx, -1, 1) * 27}px, ${clamp(dy, -1, 1) * 12}px)`;
}
function startJoystick(event) {
  if (!hasOwnLease()) {
    toast("请先申请车辆控制权", true);
    return;
  }
  event.preventDefault();
  state.pointerId = event.pointerId;
  state.deadman = true;
  $("joystick").setPointerCapture(event.pointerId);
  $("joystick").classList.add("active");
  joystickValues(event);
  sendCommand(true);
}
function moveJoystick(event) {
  if (state.pointerId !== event.pointerId) return;
  event.preventDefault();
  joystickValues(event);
}
function stopJoystick(send = true) {
  state.pointerId = null;
  state.deadman = false;
  state.linear = 0;
  state.angular = 0;
  $("joystick").classList.remove("active");
  $("joystickKnob").style.transform = "translate3d(0,0,0)";
  renderCommandPreview();
  if (send) sendCommand(false);
}

async function startEpisode() {
  if (tokenRequired()) return;
  const task = $("episodeTask").value.trim();
  const operator = $("operatorId").value.trim() || "mobile_operator";
  if (!task) {toast("请先填写任务描述", true); return;}
  try {
    await api("/api/task", {method: "POST", body: task, json: false});
    const body = ["", task, operator].join("\x1f");
    const result = await api("/api/episode/start", {method: "POST", body, json: false});
    toast(result.message);
    await refreshStatus();
  } catch (error) {toast(error.message, true);}
}
async function stopEpisode(success) {
  try {
    const reason = success ? "Mobile operator completed episode" : "Mobile operator discarded episode";
    const result = await api("/api/episode/stop", {
      method: "POST",
      body: JSON.stringify({success, reason})
    });
    toast(result.message);
    await refreshStatus();
  } catch (error) {toast(error.message, true);}
}
async function startChassis() {
  if (tokenRequired()) return;
  if (!confirm("确认启动车辆底盘？启动后底盘将接收经过安全边界的 /cmd_vel。")) return;
  try {
    const result = await api("/api/components/control", {
      method: "POST",
      body: JSON.stringify({component_id: "vehicle_chassis", action: "start", force: true})
    });
    toast(result.message || "底盘启动请求已发送");
    setTimeout(refreshComponents, 1600);
  } catch (error) {toast(error.message, true);}
}
async function emergencyStop() {
  if (tokenRequired()) return;
  if (!confirm("确认触发赤兔安全停车？车辆控制模式将切换到 SAFE_STOP。")) return;
  stopJoystick(false);
  try {
    await sendCommand(false);
    const body = [state.controllerId, "Mobile operator emergency stop"].join("\x1f");
    const result = await api("/api/safe-stop", {method: "POST", body, json: false});
    toast(result.message);
    state.lease = null;
    renderLease();
  } catch (error) {toast(error.message, true);}
}

$("cameraModeToggle").addEventListener("click", toggleCameraMode);
$("cameraPanel").addEventListener("pointerdown", startCameraDrag);
$("cameraPanel").addEventListener("pointermove", moveCameraDrag);
$("cameraPanel").addEventListener("pointerup", finishCameraDrag);
$("cameraPanel").addEventListener("pointercancel", finishCameraDrag);
$("openSettings").addEventListener("click", openSettings);
$("closeSettings").addEventListener("click", closeSettings);
$("settingsBackdrop").addEventListener("click", closeSettings);
$("saveSettings").addEventListener("click", () => {
  state.token = $("tokenInput").value.trim();
  if (state.token) sessionStorage.setItem("vehicleOpsToken", state.token);
  else sessionStorage.removeItem("vehicleOpsToken");
  updateLimitLabels();
  closeSettings();
  toast(state.token ? "操作令牌已保存" : "操作令牌已清除");
});
$("linearLimit").addEventListener("input", updateLimitLabels);
$("angularLimit").addEventListener("input", updateLimitLabels);
$("acquireLease").addEventListener("click", () => acquireLease(false));
$("releaseLease").addEventListener("click", () => releaseLease(true));
$("joystick").addEventListener("pointerdown", startJoystick);
$("joystick").addEventListener("pointermove", moveJoystick);
$("joystick").addEventListener("pointerup", (event) => {if (state.pointerId === event.pointerId) stopJoystick();});
$("joystick").addEventListener("pointercancel", () => stopJoystick());
$("joystick").addEventListener("lostpointercapture", () => {if (state.deadman) stopJoystick();});
$("startEpisode").addEventListener("click", startEpisode);
$("finishEpisode").addEventListener("click", () => stopEpisode(true));
$("discardEpisode").addEventListener("click", () => {if (confirm("确认丢弃本次 Episode？")) stopEpisode(false);});
$("startChassis").addEventListener("click", startChassis);
$("emergencyStop").addEventListener("click", emergencyStop);

document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    stopJoystick(false);
    if (hasOwnLease()) releaseLease(false, true);
  }
});
window.addEventListener("resize", () => {
  if (state.cameraMode === "floating") {
    requestAnimationFrame(() => applyCameraPosition(
      state.cameraPosition || defaultCameraPosition(), true));
  }
});
window.addEventListener("pagehide", () => {
  stopJoystick(false);
  if (hasOwnLease()) releaseLease(false, true);
});

setInterval(() => {if (state.deadman && hasOwnLease()) sendCommand(true);}, 100);
setInterval(refreshStatus, 700);
setInterval(refreshComponents, 2500);
setInterval(() => {
  const image = $("teleopCamera");
  image.src = `/api/camera/front.jpg?frame=${Date.now()}`;
}, 350);
$("teleopCamera").addEventListener("load", () => {
  $("teleopCamera").classList.add("ready");
  $("cameraPlaceholder").hidden = true;
  setText("cameraTime", new Date().toLocaleTimeString("zh-CN", {hour12: false}));
});
$("teleopCamera").addEventListener("error", () => {
  $("teleopCamera").classList.remove("ready");
  $("cameraPlaceholder").hidden = false;
});

updateLimitLabels();
renderLease();
state.cameraPosition = readCameraPosition();
setCameraMode(state.cameraMode);
refreshStatus();
refreshComponents();
if (!state.token) setTimeout(openSettings, 350);
if ("serviceWorker" in navigator) navigator.serviceWorker.register("/teleop-sw.js").catch(() => {});