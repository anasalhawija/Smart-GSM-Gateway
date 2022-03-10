// --- Global Variables ---
let ws = null;
let currentLang = "ar";
let langData = {};
let activeTab = "status";
let simPinRequired = false;
let currentMode = "AP";
let ussdSessionActive = false;
let currentSmsIndex = null;
let notificationTimeout = null;

// --- Constants ---
const MAX_SMS_CHARS_SINGLE_GSM7 = 160;
const MAX_SMS_CHARS_MULTI_GSM7 = 153;
const MAX_SMS_CHARS_SINGLE_UNICODE = 70;
const MAX_SMS_CHARS_MULTI_UNICODE = 67;

// --- Language Functions ---
async function loadLanguage(lang) {
  try {
    console.log(`Load lang:${lang}`);
    const response = await fetch(`/lang/${lang}.json?v=${Date.now()}`);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    langData = await response.json();
    currentLang = lang;
    localStorage.setItem("gsm_gateway_lang", lang);
    applyLanguage();
    console.log(`Lang ${lang} applied.`);
  } catch (error) {
    console.error("Fail load lang:", error);
    if (lang !== "en") {
      console.warn("Fallback: en");
      await loadLanguage("en");
    } else {
      alert("Critical Error: Failed to load language data.");
    }
  }
}

function applyLanguage() {
  if (!langData || Object.keys(langData).length === 0) {
    console.error("ERR:apply lang data empty");
    return;
  }
  document.documentElement.lang = currentLang;
  document.documentElement.dir = currentLang === "ar" ? "rtl" : "ltr";
  document.querySelectorAll("[data-lang]").forEach((el) => {
    const k = el.getAttribute("data-lang");
    if (langData[k] !== undefined) el.textContent = langData[k];
    else console.warn(`Lang key ${k} miss`);
  });
  document.querySelectorAll("[data-lang-tab]").forEach((el) => {
    const k = el.getAttribute("data-lang-tab");
    if (langData.tabs && langData.tabs[k] !== undefined)
      el.textContent = langData.tabs[k];
    else console.warn(`Tab key tabs.${k} miss`);
  });
  document.querySelectorAll("[data-lang-placeholder]").forEach((el) => {
    const k = el.getAttribute("data-lang-placeholder");
    if (langData[k] !== undefined) el.placeholder = langData[k];
    else console.warn(`Placeholder key ${k} miss`);
  });
  updateCharCount();
  document.title = langData.title || "GSM Gateway";
  updateActionButtonStates();
  const cS = getCurrentStatusFromUI();
  updateStatusDisplay(cS);
}

function changeLanguage(lang) {
  if (lang !== currentLang) loadLanguage(lang);
}

function initializeLanguage() {
  const sL = localStorage.getItem("gsm_gateway_lang");
  const bL = navigator.language.split("-")[0];
  let iL = "ar";
  if (sL && (sL === "ar" || sL === "en")) {
    iL = sL;
  } else if (bL === "en") {
    iL = "en";
  }
  const lS = getElement("language-selector");
  if (lS) lS.value = iL;
  return loadLanguage(iL);
}

// --- WebSocket Functions ---
function initWebSocket() {
  if (
    ws &&
    (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)
  ) {
    console.log("WS exists");
    return;
  }
  const wsH = window.location.hostname;
  const wsU = `ws://${wsH}:81/`;
  setConnectionStatus("connecting");
  console.log(`Attempt WS:${wsU}`);
  try {
    ws = new WebSocket(wsU);
  } catch (e) {
    console.error("WS create fail:", e);
    setConnectionStatus("disconnected");
    showNotification(langData.wsError || "WS connect err", true);
    setTimeout(initWebSocket, 10000);
    return;
  }
  ws.onopen = (e) => {
    console.log("WS open");
    setConnectionStatus("connected");
    showNotification(langData.wsConnected || "Connected", false, "success");
    requestStatusUpdate();
    requestConfig();
  };
  ws.onmessage = (e) => {
    try {
      const m = JSON.parse(e.data);
      console.log("WS RX:", m);
      handleWebSocketPayload(m);
    } catch (er) {
      console.error("WS JSON parse fail:", er, e.data);
      if (!er.message.includes("decode"))
        showNotification(
          `${langData.wsInvalidJson || "Invalid data"}: ${e.data}`,
          true
        );
      else console.warn("WS UTF-8 decode issue detected.");
    }
  };
  ws.onclose = (e) => {
    console.log(
      `WS closed. Code:${e.code}, Rsn:"${e.reason}",Clean:${e.wasClean}`
    );
    setConnectionStatus("disconnected");
    ws = null;
    showNotification(
      langData.wsDisconnected || "WS down. Reconnect...",
      true,
      "warning"
    );
    setTimeout(initWebSocket, 5000);
    updateStatusDisplay(null);
    simPinRequired = false;
    updateActionButtonStates();
    handlePinRequirement();
  };
  ws.onerror = (e) => {
    console.error("WS error:", e);
    setConnectionStatus("disconnected");
  };
}

function setConnectionStatus(s) {
  const d = getElement("connection-status");
  if (!d) return;
  d.className = `status-dot status-${s}`;
  let t = "WS Status: ";
  switch (s) {
    case "connected":
      t += langData.wsConnected || "OK";
      break;
    case "disconnected":
      t += langData.wsDisconnectedShort || "Down";
      break;
    case "connecting":
      t += langData.wsConnecting || "Wait";
      break;
  }
  d.title = t;
}

function handleWebSocketPayload(msg) {
  if (!msg || typeof msg.type === "undefined") {
    console.warn("WS msg no type:", msg);
    return;
  }
  const type = msg.type;
  const data = msg.data;
  switch (type) {
    case "status":
      updateStatusDisplay(data);
      simPinRequired = data?.sim_pin_status === "Required";
      handlePinRequirement();
      updateActionButtonStates();
      break;
    case "config":
      updateConfigDisplay(data);
      break;
    case "ussd_response":
      hideLoader("ussd-loader");
      hideLoader("ussd-reply-loader");
      handleUssdResponse(data);
      break;
    case "sms_sent":
      hideLoader("sms-loader");
      handleSmsSentStatus(data);
      break;
    case "sms_received_indication":
      showNotification(
        `${langData.newSmsReceived || "New SMS"} (#${data?.index || "?"})`,
        false,
        "info"
      );
      break;
    case "sms_list":
      hideLoader("inbox-loader");
      displaySmsList(data);
      break;
    case "sms_content":
      hideLoader("inbox-loader");
      displaySmsContent(data);
      break;
    case "sms_deleted":
      handleSmsDeleted(data);
      break;
    case "call_forwarding_status":
      hideLoader("cf-loader");
      updateCallForwardingDisplay(data);
      break;
    case "call_forwarding_update_result":
      hideLoader("cf-loader");
      handleCallForwardingUpdateResult(data);
      break;
    case "caller_id":
      displayCallerId(data?.caller_id);
      break;
    case "call_incoming":
      displayCallerId(
        data === "RING" ? langData.incomingCall || "Incoming..." : data
      );
      break;
    case "call_status":
      if (data === "NO CARRIER") {
        displayCallerId(null);
        showNotification(langData.callEnded || "Call End", false, "info");
      }
      break;
    case "log":
      appendLog(data);
      break;
    case "info":
      showNotification(data, false, "info");
      hideAllLoaders();
      break;
    case "warning":
      showNotification(data, false, "warning");
      hideAllLoaders();
      break;
    case "error":
      hideAllLoaders();
      showNotification(`${langData.errorPrefix || "Err"}: ${data}`, true);
      break;
    default:
      console.warn("WS unhandled type:", type, data);
      break;
  }
}

function sendWebSocketMessage(p) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    try {
      const j = JSON.stringify(p);
      console.log("WS TX:", j);
      ws.send(j);
      return true;
    } catch (e) {
      console.error("WS send fail:", e, p);
      showNotification(langData.wsSendError || "WS send err", true);
      return false;
    }
  } else {
    console.warn("WS !open:", p);
    showNotification(langData.wsNotConnected || "Not connected", true);
    return false;
  }
}

// --- Action Request Functions ---
function requestStatusUpdate() {
  sendWebSocketMessage({ action: "getStatus" });
}
function requestConfig() {
  sendWebSocketMessage({ action: "getConfig" });
}
function refreshInbox() {
  showLoader("inbox-loader");
  getElement("sms-list").innerHTML = `<li><em>${
    langData.loadingInbox || "Load..."
  }</em></li>`;
  sendWebSocketMessage({ action: "getSMSList" });
}
function readSmsContent(idx) {
  if (idx <= 0) return;
  console.log(`Req SMS:${idx}`);
  sendWebSocketMessage({ action: "readSMS", index: idx });
}
function deleteCurrentSms() {
  if (currentSmsIndex === null) return;
  if (
    confirm(`${langData.smsDeleteConfirm || "Delete SMS"} #${currentSmsIndex}?`)
  ) {
    console.log(`Del SMS:${currentSmsIndex}`);
    sendWebSocketMessage({ action: "deleteSMS", index: currentSmsIndex });
  }
}
function queryCallForwarding() {
  showLoader("cf-loader");
  sendWebSocketMessage({ action: "getCallForwardingStatus" });
}
function updateCallForwarding(k) {
  const t = getElement(`${k}-toggle`);
  const n = getElement("forward-number");
  if (!t || !n) return;
  const a = t.checked;
  const num = n.value.trim();
  if (a && !num) {
    showNotification(langData.cfNumberRequired || "Num needed", true);
    t.checked = false;
    return;
  }
  showLoader("cf-loader");
  sendWebSocketMessage({
    action: "setCallForwarding",
    condition: k,
    activate: a,
    number: num,
  });
}

// --- UI Update Functions ---
function updateStatusDisplay(s) {
  console.log("Update status UI:", s);
  setText("wifi-status", s?.wifi_status || "---");
  setText("ip-address", s?.ip_address || "---");
  setText("sim-status", s?.sim_status || "---");
  setText("signal-quality", s?.signal_quality || "---");
  setText("network-operator", s?.network_operator || "---");
  setText("sim-phone-number", s?.sim_phone_number || "---");
  setText("sim-pin-status", s?.sim_pin_status || "---");
}
function updateConfigDisplay(c) {
  console.log("Update cfg UI:", c);
  setValue("server-host", c?.server_host || "");
  setValue("server-port", c?.server_port || "");
  setValue("server-user", c?.server_user || "");
  const apP = getElement("ap-password");
  if (apP)
    apP.placeholder = c?.ap_password_set
      ? langData.apPasswordSetPlaceholder || "AP Pass set"
      : langData.apPasswordNotSetPlaceholder || "AP Pass !set";
  const simP = getElement("sim-pin-save");
  if (simP)
    simP.placeholder = c?.sim_pin_set
      ? langData.simPinSetPlaceholder || "PIN saved"
      : langData.simPinNotSetPlaceholder || "PIN !saved";
}
function handlePinRequirement() {
  const pS = getElement("pin-entry-section");
  const staM = getElement("station-mode");
  const setM = getElement("setup-mode");
  if (!pS || !staM || !setM) return;
  const pinNeededNow = simPinRequired && currentMode === "STA";
  if (pinNeededNow) {
    pS.style.display = "block";
    staM.style.opacity = "0.3";
    staM.style.pointerEvents = "none";
    setM.style.display = "none";
    const pI = getElement("pin-input");
    if (pI) pI.focus();
  } else {
    pS.style.display = "none";
    if (currentMode === "STA") {
      staM.style.opacity = "1";
      staM.style.pointerEvents = "auto";
    }
    setM.style.display = currentMode === "AP" ? "block" : "none";
  }
}
function handleUssdResponse(d) {
  const rD = getElement("ussd-response");
  const rF = getElement("ussd-reply-form");
  const mF = getElement("ussd-form");
  if (!rD || !rF || !mF) return;
  if (d && typeof d === "object") {
    let dM = d.message || "";
    const rT = d.type;
    const dcs = d.dcs;
    console.log(`Raw USSD - T:${rT}, DCS:${dcs}, Msg:[${dM}]`);
    let isU =
      (dcs === 72 || dcs === 8 || dcs === 136) &&
      dM.length > 0 &&
      dM.length % 4 === 0 &&
      /^[0-9A-Fa-f]+$/i.test(dM);
    if (isU) {
      console.log("Decode UCS2...");
      try {
        let dec = "";
        const hS = dM.replace(/[^0-9A-Fa-f]/gi, "");
        for (let i = 0; i < hS.length; i += 4) {
          const hC = hS.substring(i, i + 4);
          if (hC.length === 4) {
            const cC = parseInt(hC, 16);
            if (!isNaN(cC)) dec += String.fromCharCode(cC);
            else dec += "?";
          } else break;
        }
        if (dec.length > 0 && !dec.includes("?")) {
          dM = dec;
          console.log("Decoded:", dM);
          rD.dir = "rtl";
          rD.style.textAlign = "right";
        } else {
          console.warn("Decode fail/empty. Keep hex.");
          dM = d.message + " (Decode Failed)";
          rD.dir = "ltr";
          rD.style.textAlign = "left";
        }
      } catch (e) {
        console.error("UCS2 decode err:", e);
        dM = d.message + " (Decode Exc)";
        rD.dir = "ltr";
        rD.style.textAlign = "left";
      }
    } else {
      console.log("Not UCS2.");
      rD.dir = "ltr";
      rD.style.textAlign = "left";
    }
    rD.textContent = dM || langData.emptyResponse || "Empty";
    if (rT === 1) {
      console.log("Action req: Show Reply");
      ussdSessionActive = true;
      rF.style.display = "block";
      mF.style.display = "none";
      setValue("ussd-reply", "");
      getElement("ussd-reply").focus();
      showNotification(
        langData.ussdActionRequired || "Network needs reply",
        false,
        "info"
      );
    } else {
      console.log("Session end: Show Main");
      ussdSessionActive = false;
      rF.style.display = "none";
      mF.style.display = "block";
      setValue("ussd-reply", "");
      if (rT !== 0 && rT !== -1) {
        showNotification(
          `${langData.ussdSessionEnded || "USSD End"} (T:${rT})`,
          false,
          rT > 1 ? "warning" : "info"
        );
      }
    }
  } else {
    rD.textContent = langData.invalidResponse || "Invalid";
    ussdSessionActive = false;
    rF.style.display = "none";
    mF.style.display = "block";
  }
}
function handleSmsSentStatus(d) {
  const sP = getElement("sms-send-status");
  if (!sP) return;
  if (d && d.status === "OK") {
    showNotification(
      d.message || langData.smsSentSuccess || "SMS Sent",
      false,
      "success"
    );
    setValue("sms-number", "");
    setValue("sms-message", "");
    updateCharCount();
    sP.textContent = "";
    sP.className = "status-message success";
  } else {
    const eM = d?.message || langData.smsSentError || "Fail SMS";
    showNotification(eM, true);
    sP.textContent = eM;
    sP.className = "status-message error";
  }
}
function displaySmsList(list) {
  const lE = getElement("sms-list");
  if (lE)
    lE.innerHTML = `<li><em>${
      langData.featureNotImplemented || "Inbox !Impl"
    }</em></li>`;
}
function displaySmsContent(sms) {
  showNotification(
    langData.featureNotImplemented || "Read SMS !Impl",
    false,
    "warning"
  );
}
function handleSmsDeleted(d) {
  showNotification(
    langData.featureNotImplemented || "Del SMS !Impl",
    false,
    "warning"
  );
}
function updateCallForwardingDisplay(sD) {
  console.log("Update CF UI:", sD);
  const nI = getElement("forward-number");
  const conds = ["cfu", "cfb", "cfnry", "cfnrc"];
  conds.forEach((k) => {
    const t = getElement(`${k}-toggle`);
    const s = getElement(`${k}-status`);
    if (t && s) {
      t.checked = false;
      s.textContent = `(${langData.querying || "Query..."})`;
      s.className = "";
    }
  });
}
function handleCallForwardingUpdateResult(r) {
  showNotification(
    langData.featureNotImplemented || "Set CF !Impl",
    false,
    "warning"
  );
}
function displayCallerId(cid) {
  const dE = getElement("caller-id-display");
  if (!dE) return;
  const noC = langData.noIncomingCall || "No incoming call...";
  const incL = langData.incomingCallLabel || "Incoming Call:";
  const incG = langData.incomingCall || "Incoming...";
  console.log("displayCallerId received:", cid);
  if (cid === null || typeof cid === "undefined") {
    dE.innerHTML = `<em>${noC}</em>`;
    console.log("Clear CID display");
  } else if (cid === "RING") {
    if (dE.textContent.includes(noC)) {
      dE.innerHTML = `<strong>${incL}</strong> ${incG}`;
      console.log("Display generic RING");
    } else console.log("Ignore RING, num shown");
  } else if (cid !== noC) {
    dE.innerHTML = `<strong>${incL}</strong> ${cid}`;
    console.log("Show CID:", cid);
  }
}
function appendLog(lE) {
  const lA = getElement("activity-log");
  if (!lA) return;
  let line = "";
  const ts = new Date().toLocaleTimeString();
  if (typeof lE === "string") {
    line = `[${ts}] <span class="info">${lE}</span>`;
  } else if (typeof lE === "object") {
    const dir = lE.dir === "TX" ? ">>" : "<<";
    const dC = lE.dir === "TX" ? "tx" : "rx";
    const cnt = lE.cmd || lE.line || JSON.stringify(lE);
    let cC = dC;
    if (lE.dir === "RX" && (cnt.includes("ERROR") || cnt.includes("FAIL")))
      cC = "error";
    line = `[${ts}] <span class="${dC}">${dir}</span> <span class="${cC}">${cnt}</span>`;
    if (lE.partial)
      line += `\n   <span class="info">(Part:${lE.partial})</span>`;
  } else return;
  lA.innerHTML += line + "\n";
  lA.scrollTop = lA.scrollHeight;
  const maxL = 150;
  const lines = lA.innerHTML.split("\n");
  if (lines.length > maxL)
    lA.innerHTML = lines.slice(lines.length - maxL).join("\n");
}
function clearLog() {
  const lA = getElement("activity-log");
  if (lA) {
    lA.innerHTML = "";
    appendLog("Log cleared.");
  }
}

// --- UI Interaction Functions ---
function openTab(e, tN) {
  document
    .querySelectorAll(".tab-content")
    .forEach((t) => (t.style.display = "none"));
  document
    .querySelectorAll(".tab-button")
    .forEach((b) => b.classList.remove("active"));
  const sel = getElement(tN);
  if (sel) sel.style.display = "block";
  if (e && e.currentTarget) e.currentTarget.classList.add("active");
  activeTab = tN;
  switch (tN) {
    case "sms":
      refreshInbox();
      break;
    case "calls":
      queryCallForwarding();
      break;
    case "log":
      const lA = getElement("activity-log");
      if (lA) lA.scrollTop = lA.scrollHeight;
      break;
  }
}
function scanWifi() {
  if (currentMode !== "AP") {
    showNotification(langData.scanOnlyInAP || "Scan AP only", true);
    return;
  }
  const list = getElement("wifi-list");
  if (!list) return;
  list.innerHTML = `<li><em>${langData.scanning || "Scan..."}</em></li>`;
  showLoader("wifi-loader");
  fetch("/scanwifi")
    .then((r) => {
      if (!r.ok) {
        return r
          .json()
          .then((eD) => {
            throw new Error(`HTTP ${r.status}:${eD.message || r.statusText}`);
          })
          .catch(() => {
            throw new Error(`HTTP ${r.status}:${r.statusText}`);
          });
      }
      return r.json();
    })
    .then((d) => {
      hideLoader("wifi-loader");
      list.innerHTML = "";
      if (d.success && d.networks && d.networks.length > 0) {
        d.networks.forEach((n) => {
          const li = document.createElement("li");
          let sB = "▂";
          if (n.rssi > -67) sB = "▂▄▆█";
          else if (n.rssi > -75) sB = "▂▄▆";
          else if (n.rssi > -85) sB = "▂▄";
          li.innerHTML = `${sB} ${
            n.ssid || "Hidden"
          } <span class="wifi-rssi">(${n.rssi} dBm)</span> ${
            n.secure ? "🔒" : ""
          }`;
          li.onclick = () => {
            setValue("ssid", n.ssid);
            if (n.secure) getElement("password").focus();
          };
          list.appendChild(li);
        });
      } else {
        list.innerHTML = `<li><em>${
          d.message || langData.scanFailed || "Scan fail"
        }</em></li>`;
      }
    })
    .catch((e) => {
      hideLoader("wifi-loader");
      list.innerHTML = `<li><em>${langData.scanError || "Scan err"}</em></li>`;
      console.error("Scan WiFi err:", e);
      showNotification(
        `${langData.scanError || "Scan err"}:${e.message}`,
        true
      );
    });
}
function updateCharCount() {
  const ta = getElement("sms-message");
  const cD = getElement("char-count");
  if (!ta || !cD || !langData || !langData.smsCharCount) return;
  const m = ta.value;
  const l = m.length;
  let isU = false;
  for (let i = 0; i < l; i++)
    if (m.charCodeAt(i) > 127) {
      isU = true;
      break;
    }
  let mS, mM, ms, ec;
  if (isU) {
    mS = MAX_SMS_CHARS_SINGLE_UNICODE;
    mM = MAX_SMS_CHARS_MULTI_UNICODE;
    ec = "Unicode";
  } else {
    mS = MAX_SMS_CHARS_SINGLE_GSM7;
    mM = MAX_SMS_CHARS_MULTI_GSM7;
    ec = "GSM-7";
  }
  if (l === 0) ms = 0;
  else if (l <= mS) ms = 1;
  else ms = Math.ceil(l / mM);
  cD.textContent = langData.smsCharCount
    .replace("{chars}", l)
    .replace("{msgs}", ms)
    .replace("{encoding}", ec);
}
function showNotification(msg, isErr = false, type = null) {
  const n = getElement("notification");
  if (!n) {
    console.error("Notify ERR: El not found!");
    return;
  }
  console.log(`Showing notification: T=${type}, Err=${isErr}, Msg=${msg}`);
  n.textContent = msg;
  n.className = "";
  n.classList.remove("error", "info", "warning", "success", "show");
  let nT = "success";
  if (type) nT = type;
  else if (isErr) nT = "error";
  n.classList.add(nT);
  requestAnimationFrame(() => {
    requestAnimationFrame(() => {
      n.classList.add("show");
    });
  });
  if (notificationTimeout) clearTimeout(notificationTimeout);
  const dur = nT === "error" || nT === "warning" ? 6000 : 4000;
  notificationTimeout = setTimeout(() => {
    n.classList.remove("show");
    notificationTimeout = null;
  }, dur);
}
function rebootDevice() {
  if (confirm(langData.rebootConfirm || "Reboot?")) {
    showNotification(langData.rebooting || "Rebooting...", false, "info");
    fetch("/reboot", { method: "POST" })
      .then((r) => r.json().catch(() => ({})))
      .then((d) => {
        if (d && !d.success) {
          showNotification(
            d.message || langData.rebootError || "Fail reboot",
            true
          );
        }
      })
      .catch((e) => {
        showNotification(langData.rebootError || "Fail reboot", true);
        console.error("Reboot err:", e);
      });
  }
}

// *** FINAL updateActionButtonStates - NO general disabling ***
function updateActionButtonStates() {
  console.log(
    "Action buttons always enabled (except USSD reply based on session)."
  );
  const setDis = (selector, disabled, title = "") => {
    document.querySelectorAll(selector).forEach((el) => {
      el.disabled = disabled;
      el.title = disabled ? title : "";
    });
  };
  // Keep ALL general buttons ENABLED
  setDis("#sms-form button, #sms-form textarea, #sms-form input", false, "");
  setDis("#sms-inbox-section button", false, "");
  setDis("#ussd-form button, #ussd-form input", false, "");
  setDis(
    "#call-forwarding-form button, #call-forwarding-form input",
    false,
    ""
  );

  // Disable/Enable USSD Reply Form based ONLY on ussdSessionActive
  const disableReply = !ussdSessionActive;
  const reasonReply = !ussdSessionActive
    ? langData.ussdNoSession || "No active USSD session"
    : "";
  setDis('#ussd-reply-form input[type="text"]', disableReply, reasonReply);
  setDis('#ussd-reply-form button[type="submit"]', disableReply, reasonReply);
  setDis('#ussd-reply-form button[type="button"]', disableReply, reasonReply); // For Cancel button
  console.log(
    `USSD Reply Form ${
      disableReply ? "Off" : "On"
    } (USSD Active:${ussdSessionActive})`
  );
}

// --- Loader Functions ---
function showLoader(id) {
  const l = getElement(id);
  if (l) l.style.display = "inline-block";
}
function hideLoader(id) {
  const l = getElement(id);
  if (l) l.style.display = "none";
}
function hideAllLoaders() {
  document
    .querySelectorAll(".loader")
    .forEach((l) => (l.style.display = "none"));
}

// --- Modal Dialog Functions ---
function openModal(id) {
  const m = getElement(id);
  if (m) m.style.display = "block";
}
function closeModal(id) {
  const m = getElement(id);
  if (m) m.style.display = "none";
  if (id === "sms-content-modal") currentSmsIndex = null;
}
window.addEventListener("click", function (e) {
  document.querySelectorAll(".modal").forEach((m) => {
    if (e.target == m) closeModal(m.id);
  });
});

// --- Helper Functions ---
function getElement(id) {
  return document.getElementById(id);
}
function setText(id, text) {
  const e = getElement(id);
  if (e) e.textContent = text;
}
function setValue(id, value) {
  const e = getElement(id);
  if (e) e.value = value;
}
function getValue(id) {
  const e = getElement(id);
  return e ? e.value : "";
}
function getCurrentStatusFromUI() {
  return {
    wifi_status: getElement("wifi-status")?.textContent || "---",
    ip_address: getElement("ip-address")?.textContent || "---",
    sim_status: getElement("sim-status")?.textContent || "---",
    signal_quality: getElement("signal-quality")?.textContent || "---",
    network_operator: getElement("network-operator")?.textContent || "---",
    sim_phone_number: getElement("sim-phone-number")?.textContent || "---",
    sim_pin_status: getElement("sim-pin-status")?.textContent || "---",
  };
}

// --- Form Submission Handlers ---
function submitPinForm(e) {
  e.preventDefault();
  const pI = getElement("pin-input");
  if (!pI) return;
  const pin = pI.value;
  const ld = getElement("pin-loader");
  const eP = getElement("pin-error");
  if (!pin || !/^\d{4,8}$/.test(pin)) {
    if (eP) eP.textContent = langData.pinInvalidFormat || "PIN 4-8 digits";
    return;
  }
  if (eP) eP.textContent = "";
  if (ld) showLoader("pin-loader");
  const fd = new FormData();
  fd.append("pin", pin);
  fetch("/enterpin", { method: "POST", body: new URLSearchParams(fd) })
    .then((r) => r.json())
    .then((d) => {
      if (ld) hideLoader("pin-loader");
      if (d.success) {
        showNotification(
          d.message || langData.pinAcceptSuccess || "PIN OK",
          false,
          "success"
        );
        const pS = getElement("pin-entry-section");
        if (pS) pS.style.display = "none";
      } else {
        if (eP)
          eP.textContent = d.message || langData.pinRejectError || "PIN Fail";
        showNotification(
          d.message || langData.pinRejectError || "PIN Fail",
          true
        );
      }
    })
    .catch((er) => {
      if (ld) hideLoader("pin-loader");
      const eM = langData.pinSubmitError || "PIN Submit Err";
      if (eP) eP.textContent = eM;
      showNotification(eM, true);
      console.error("PIN fetch err:", er);
    });
}
function submitWifiForm(e) {
  e.preventDefault();
  const ssid = getValue("ssid");
  const pass = getValue("password");
  if (!ssid) {
    showNotification(langData.ssidRequired || "SSID required", true);
    return;
  }
  showNotification(langData.savingWifi || "Saving WiFi...", false, "info");
  const fd = new FormData();
  fd.append("ssid", ssid);
  fd.append("password", pass);
  fetch("/savewifi", { method: "POST", body: new URLSearchParams(fd) })
    .then((r) => r.json().catch(() => ({})))
    .then((d) => {
      if (d && !d.success) {
        showNotification(
          d.message || langData.saveWifiError || "Fail save WiFi",
          true
        );
      }
    })
    .catch((er) => {
      showNotification(langData.saveWifiError || "Fail save WiFi", true);
      console.error("Save WiFi err:", er);
    });
}
function submitConfigForm(e) {
  e.preventDefault();
  const form = getElement("config-form");
  if (!form) return;
  const fd = new FormData(form);
  showNotification(langData.savingConfig || "Saving cfg...", false, "info");
  fetch("/saveconfig", { method: "POST", body: new URLSearchParams(fd) })
    .then((r) => r.json())
    .then((d) => {
      showNotification(
        d.message ||
          (d.success
            ? langData.saveConfigSuccess || "Cfg saved"
            : langData.saveConfigError || "Fail save cfg"),
        !d.success
      );
      if (d.success && d.needs_pin_recheck) {
        console.log("Cfg saved, backend recheck PIN.");
      }
      if (d.success) requestConfig();
    })
    .catch((er) => {
      showNotification(langData.saveConfigError || "Fail save cfg", true);
      console.error("Save cfg err:", er);
    });
}
function submitSmsForm(e) {
  e.preventDefault();
  const num = getValue("sms-number");
  const msg = getValue("sms-message");
  if (!num || !msg) {
    showNotification(langData.smsFieldsRequired || "Num/Msg needed", true);
    return;
  }
  showLoader("sms-loader");
  sendWebSocketMessage({ action: "sendSMS", number: num, message: msg });
}
function submitUssdForm(e) {
  e.preventDefault();
  const code = getValue("ussd-code");
  const rD = getElement("ussd-response");
  if (!code) {
    showNotification(langData.ussdCodeRequired || "Code needed", true);
    return;
  }
  if (rD) rD.innerHTML = `<em>${langData.sending || "Sending..."}</em>`;
  showLoader("ussd-loader");
  sendWebSocketMessage({ action: "sendUSSD", code: code });
}
function submitUssdReplyForm(e) {
  e.preventDefault();
  const reply = getValue("ussd-reply");
  if (!ussdSessionActive) {
    showNotification(langData.ussdNoSession || "No USSD sess", true);
    return;
  }
  if (!reply) {
    showNotification(langData.ussdReplyRequired || "Reply needed", true);
    return;
  }
  showLoader("ussd-reply-loader");
  sendWebSocketMessage({ action: "sendUSSDReply", reply: reply });
}
function cancelUssdSession() {
  sendWebSocketMessage({ action: "cancelUSSD" });
  setText("ussd-response", langData.ussdCancelled || "USSD cancelled");
  setValue("ussd-code", "");
  setValue("ussd-reply", "");
  getElement("ussd-reply-form").style.display = "none";
  getElement("ussd-form").style.display = "block";
  ussdSessionActive = false;
  updateActionButtonStates();
}

// --- Initialization ---
function getInitialModeAndSetupUI() {
  console.log("Fetch mode...");
  fetch("/getmode")
    .then((r) => {
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return r.json();
    })
    .then((d) => {
      console.log("Mode data:", d);
      currentMode = d.mode;
      simPinRequired = d.sim_pin_required;
      updateActionButtonStates();
      handlePinRequirement();
      if (currentMode === "AP") {
        console.log("Mode:AP");
        getElement("setup-mode").style.display = "block";
        getElement("station-mode").style.display = "none";
        getElement("pin-entry-section").style.display = "none";
        scanWifi();
      } else {
        console.log("Mode:STA");
        getElement("setup-mode").style.display = "none";
        getElement("station-mode").style.display = "block";
        initWebSocket();
      }
    })
    .catch((e) => {
      console.error("Get mode err:", e);
      getElement("setup-mode").style.display = "block";
      getElement("station-mode").style.display = "none";
      getElement("pin-entry-section").style.display = "none";
      showNotification(langData.getModeError || "Get mode err", true);
    });
}
window.onload = async function () {
  console.log("Page loaded. Init...");
  await initializeLanguage();
  const wf = getElement("wifi-form");
  if (wf) wf.addEventListener("submit", submitWifiForm);
  const cf = getElement("config-form");
  if (cf) cf.addEventListener("submit", submitConfigForm);
  const sf = getElement("sms-form");
  if (sf) sf.addEventListener("submit", submitSmsForm);
  const uf = getElement("ussd-form");
  if (uf) uf.addEventListener("submit", submitUssdForm);
  const urf = getElement("ussd-reply-form");
  if (urf) urf.addEventListener("submit", submitUssdReplyForm);
  const pf = getElement("pin-form");
  if (pf) pf.addEventListener("submit", submitPinForm);
  getInitialModeAndSetupUI();
  setTimeout(() => {
    const iTB = document.querySelector(
      `.tab-button[onclick*="'${activeTab}'"]`
    );
    const iTC = getElement(activeTab);
    if (currentMode === "STA" && !simPinRequired) {
      if (iTB) iTB.classList.add("active");
      if (iTC) iTC.style.display = "block";
    } else if (currentMode === "AP") {
      document
        .querySelectorAll("#station-mode .tab-content")
        .forEach((t) => (t.style.display = "none"));
    }
  }, 100);
  console.log("Init complete.");
};
