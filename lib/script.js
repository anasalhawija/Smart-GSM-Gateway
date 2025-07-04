/*
=================================================================================
  Project:  Smart GSM Gateway - Frontend Logic
  File:     script.js
  Author:   Eng: Anas Alhawija
  Version:  2.1 (Original Stable Structure)
  Date:     2025-07-04
  License:  MIT License

  About:    This script handles all user interactions, WebSocket communication,
            and dynamic UI updates for the GSM Gateway. It uses the original
            globally-scoped structure for proven stability.
=================================================================================
*/

// --- Global State Variables ---
let ws = null; // Holds the WebSocket object.
let currentLang = "en"; // The currently active language code (e.g., "en" or "ar").
let langData = {}; // Stores the loaded language strings from the JSON file.
let activeTab = "status"; // The ID of the currently visible tab.
let simPinRequired = false; // Flag indicating if the backend requires a SIM PIN.
let currentMode = "AP"; // The current operating mode of the device ("AP" or "STA").
let ussdSessionActive = false; // Flag to track if a USSD session is waiting for a reply.
let currentSmsIndex = null; // Stores the index of the SMS currently viewed in the modal.
let notificationTimeout = null; // Timeout ID for hiding notifications automatically.

// --- Constants ---
const MAX_SMS_CHARS_SINGLE_GSM7 = 160;
const MAX_SMS_CHARS_MULTI_GSM7 = 153;
const MAX_SMS_CHARS_SINGLE_UNICODE = 70;
const MAX_SMS_CHARS_MULTI_UNICODE = 67;

// --- Language Functions ---
/**
 * Loads a language JSON file from the server and applies it to the UI.
 * @param {string} lang The language code to load (e.g., "en").
 */
async function loadLanguage(lang) {
  try {
    console.log(`Loading language: ${lang}`);
    // Append a timestamp to the URL to prevent browser caching issues.
    const response = await fetch(`/lang/${lang}.json?v=${Date.now()}`);
    if (!response.ok) {
      throw new Error(`HTTP Error ${response.status}`);
    }
    langData = await response.json();
    currentLang = lang;
    localStorage.setItem("gsm_gateway_lang", lang);
    applyLanguage();
    console.log(`Language ${lang} applied successfully.`);
  } catch (error) {
    console.error("Failed to load language file:", error);
    // If the requested language fails, fall back to English.
    if (lang !== "en") {
      console.warn("Falling back to English.");
      await loadLanguage("en");
    } else {
      // If English itself fails, it's a critical error.
      alert("Critical Error: Could not load base language files.");
    }
  }
}

/**
 * Applies the loaded language data to all relevant DOM elements.
 */
function applyLanguage() {
  if (!langData || Object.keys(langData).length === 0) {
    console.error("Cannot apply language: langData is empty.");
    return;
  }
  // Set the document's language and text direction.
  document.documentElement.lang = currentLang;
  document.documentElement.dir = currentLang === "ar" ? "rtl" : "ltr";

  // Apply translations to elements with data-lang attributes.
  document.querySelectorAll("[data-lang]").forEach((el) => {
    const key = el.getAttribute("data-lang");
    if (langData[key] !== undefined) {
      el.textContent = langData[key];
    }
  });
  // Apply translations to tab buttons.
  document.querySelectorAll("[data-lang-tab]").forEach((el) => {
    const key = el.getAttribute("data-lang-tab");
    if (langData.tabs && langData.tabs[key] !== undefined) {
      el.textContent = langData.tabs[key];
    }
  });
  // Apply translations to input placeholders.
  document.querySelectorAll("[data-lang-placeholder]").forEach((el) => {
    const key = el.getAttribute("data-lang-placeholder");
    if (langData[key] !== undefined) {
      el.placeholder = langData[key];
    }
  });

  // Update dynamic elements that depend on language strings.
  updateCharCount();
  document.title = langData.title || "GSM Gateway";
  updateActionButtonStates();
  const currentStatus = getCurrentStatusFromUI();
  updateStatusDisplay(currentStatus);
}

/**
 * Changes the current language and reloads the UI elements.
 * @param {string} lang The new language code.
 */
function changeLanguage(lang) {
  if (lang !== currentLang) {
    loadLanguage(lang);
  }
}

/**
 * Initializes the language based on saved preferences or browser language.
 */
function initializeLanguage() {
  const savedLang = localStorage.getItem("gsm_gateway_lang");
  const browserLang = navigator.language.split("-")[0];
  let initialLang = "ar"; // Default to Arabic.
  if (savedLang && ["ar", "en"].includes(savedLang)) {
    initialLang = savedLang;
  } else if (browserLang === "en") {
    initialLang = "en";
  }
  getElement("language-selector").value = initialLang;
  return loadLanguage(initialLang);
}

// --- WebSocket Functions ---
/**
 * Initializes the WebSocket connection to the device.
 * Includes auto-reconnect logic.
 */
function initWebSocket() {
  if (
    ws &&
    (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)
  ) {
    return; // Connection already exists or is being established.
  }
  const wsUrl = `ws://${window.location.hostname}:81/`;
  setConnectionStatus("connecting");
  console.log(`Attempting to connect to WebSocket at ${wsUrl}`);
  ws = new WebSocket(wsUrl);

  ws.onopen = (event) => {
    console.log("WebSocket connection established.");
    setConnectionStatus("connected");
    showNotification(
      langData.wsConnected || "Connected to device.",
      false,
      "success"
    );
    requestStatusUpdate();
    requestConfig();
  };

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      console.log("WS RX:", msg);
      handleWebSocketPayload(msg);
    } catch (e) {
      console.error("Failed to parse WebSocket message:", e, event.data);
      showNotification(
        `${langData.wsInvalidJson || "Received invalid data"}: ${event.data}`,
        true
      );
    }
  };

  ws.onclose = (event) => {
    console.log(`WebSocket connection closed. Code: ${event.code}`);
    setConnectionStatus("disconnected");
    ws = null;
    showNotification(
      langData.wsDisconnected || "Disconnected. Trying to reconnect...",
      true,
      "warning"
    );
    // Reset UI to a safe, disconnected state.
    updateStatusDisplay(null);
    simPinRequired = false;
    updateActionButtonStates();
    handlePinRequirement();
    setTimeout(initWebSocket, 5000); // Attempt to reconnect after 5 seconds.
  };

  ws.onerror = (event) => {
    console.error("WebSocket error:", event);
    setConnectionStatus("disconnected");
  };
}

/**
 * Updates the connection status indicator dot in the UI.
 * @param {string} status 'connecting', 'connected', or 'disconnected'.
 */
function setConnectionStatus(status) {
  const statusDot = getElement("connection-status");
  if (!statusDot) return;
  statusDot.className = `status-dot status-${status}`;
  let titleText = "WebSocket Status: ";
  switch (status) {
    case "connected":
      titleText += langData.wsConnected || "Connected";
      break;
    case "disconnected":
      titleText += langData.wsDisconnectedShort || "Disconnected";
      break;
    case "connecting":
      titleText += langData.wsConnecting || "Connecting...";
      break;
  }
  statusDot.title = titleText;
}

/**
 * Main router for incoming WebSocket messages.
 * @param {object} msg The parsed JSON message from the device.
 */
function handleWebSocketPayload(msg) {
  if (!msg || typeof msg.type === "undefined") {
    console.warn("Received WebSocket message without type:", msg);
    return;
  }
  const { type, data } = msg;

  switch (type) {
    case "status":
      updateStatusDisplay(data);
      simPinRequired = data?.sim_pin_status === "Required";
      updateActionButtonStates();
      handlePinRequirement();
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

    // --- NEW: Handle the new streaming events for the SMS list ---
    case "sms_list_started":
      // This is the signal that the backend is starting to send messages.
      // Clear the current list and show the loader.
      console.log("Backend started sending SMS list. Clearing UI.");
      const listElement = getElement("sms-list");
      if (listElement) {
        listElement.innerHTML = `<li><em>${
          langData.inboxLoading || "Loading..."
        }</em></li>`;
      }
      showLoader("inbox-loader");
      break;

    case "sms_item":
      // This message contains a single, complete, decoded SMS message.
      // We add it to the list.
      console.log("Received a single SMS item:", data);
      appendSmsItem(data);
      break;

    case "sms_list_finished":
      // This is the signal that all messages have been sent.
      // We can now hide the loader.
      console.log("Backend finished sending SMS list. Status:", data.status);
      hideLoader("inbox-loader");
      // If the list is still empty after the process finishes, show the "empty" message.
      const list = getElement("sms-list");
      if (list && (list.innerHTML.includes("<em>") || list.innerHTML === "")) {
        list.innerHTML = `<li><em>${
          langData.inboxEmpty || "Inbox is empty."
        }</em></li>`;
      }
      break;

    case "sms_content":
      hideLoader("inbox-loader");
      displaySmsContent(data);
      break;
    case "sms_deleted":
      handleSmsDeleted(data);
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
      }
      break;
    case "error":
      hideAllLoaders();
      showNotification(`${langData.errorPrefix || "Error"}: ${data}`, true);
      break;
    default:
      console.warn(`Unhandled WebSocket message type: ${type}`);
      break;
  }
  updateActionButtonStates();
}

/**
 * Sends a JSON payload to the device via WebSocket.
 * @param {object} payload The JSON object to send.
 * @returns {boolean} True if the message was sent, false otherwise.
 */
function sendWebSocketMessage(payload) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    const jsonPayload = JSON.stringify(payload);
    console.log("WS TX:", jsonPayload);
    ws.send(jsonPayload);
    return true;
  } else {
    console.warn("WebSocket not open. Message not sent:", payload);
    showNotification(
      langData.wsNotConnected || "Not connected to device.",
      true
    );
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
  sendWebSocketMessage({ action: "getSMSList" });
}
function readSmsContent(idx) {
  if (idx <= 0) return;
  showLoader("inbox-loader");
  sendWebSocketMessage({ action: "readSMS", index: idx });
}
function deleteCurrentSms() {
  if (currentSmsIndex === null) return;
  if (
    confirm(
      `${langData.smsDeleteConfirm || "Delete this SMS?"} (#${currentSmsIndex})`
    )
  ) {
    sendWebSocketMessage({ action: "deleteSMS", index: currentSmsIndex });
  }
}

// --- UI Update Functions ---
function updateStatusDisplay(s) {
  setText("wifi-status", s?.wifi_status || "---");
  setText("ip-address", s?.ip_address || "---");
  setText("sim-status", s?.sim_status || "---");
  setText("signal-quality", s?.signal_quality || "---");
  setText("network-operator", s?.network_operator || "---");
  setText("sim-phone-number", s?.sim_phone_number || "---");
  setText("sim-pin-status", s?.sim_pin_status || "---");
}
function updateConfigDisplay(c) {
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
    getElement("pin-input")?.focus();
  } else {
    pS.style.display = "none";
    if (currentMode === "STA") {
      staM.style.opacity = "1";
      staM.style.pointerEvents = "auto";
    }
    setM.style.display = currentMode === "AP" ? "block" : "none";
  }
}

/**
 * Handles displaying USSD responses. This function is now simpler because
 * all text decoding is handled by the C++ backend.
 * @param {object} data The USSD response object from the device.
 */
function handleUssdResponse(data) {
  const responseDiv = getElement("ussd-response");
  const replyForm = getElement("ussd-reply-form");
  const mainForm = getElement("ussd-form");
  if (!responseDiv || !replyForm || !mainForm) return;

  if (data && typeof data === "object") {
    // The message from the backend is already decoded UTF-8. Just display it.
    let displayMessage = data.message || "";
    console.log(`Displaying pre-decoded USSD message: [${displayMessage}]`);
    responseDiv.textContent = displayMessage;

    // Automatically set text direction based on content for better readability.
    const arabicRegex = /[\u0600-\u06FF]/;
    if (arabicRegex.test(displayMessage)) {
      responseDiv.dir = "rtl";
      responseDiv.style.textAlign = "right";
    } else {
      responseDiv.dir = "ltr";
      responseDiv.style.textAlign = "left";
    }

    // Form Toggling Logic
    const responseType = data.type;
    if (responseType === 1) {
      ussdSessionActive = true;
      replyForm.style.display = "block";
      mainForm.style.display = "none";
      setValue("ussd-reply", "");
      getElement("ussd-reply").focus();
    } else {
      ussdSessionActive = false;
      replyForm.style.display = "none";
      mainForm.style.display = "block";
    }
  } else {
    responseDiv.textContent = langData.invalidResponse || "Invalid response";
    ussdSessionActive = false;
    replyForm.style.display = "none";
    mainForm.style.display = "block";
  }
  updateActionButtonStates();
}

function handleSmsSentStatus(data) {
  showNotification(
    data.message ||
      (data.status === "OK"
        ? langData.smsSentSuccess
        : langData.smsSentError) ||
      "SMS Status",
    data.status !== "OK",
    data.status === "OK" ? "success" : "error"
  );
  if (data.status === "OK") {
    setValue("sms-number", "");
    setValue("sms-message", "");
    updateCharCount();
  }
}

/**
 * Appends a single SMS item to the inbox list in the UI.
 * @param {object} sms The SMS object received from the backend.
 */
function appendSmsItem(sms) {
  const listElement = getElement("sms-list");
  if (!listElement || !sms || typeof sms.index === "undefined") return;

  // If this is the first item, clear the "Loading..." or "Empty" message.
  if (listElement.innerHTML.includes("<em>")) {
    listElement.innerHTML = "";
  }

  // Check if an item with the same index already exists to prevent duplicates.
  if (document.querySelector(`li[data-index='${sms.index}']`)) return;

  const li = document.createElement("li");
  li.setAttribute("data-index", sms.index);
  li.onclick = () => readSmsContent(sms.index);
  if (sms.status && sms.status.includes("UNREAD")) {
    li.classList.add("sms-unread");
  }

  const senderSpan = document.createElement("span");
  senderSpan.className = "sms-sender";
  senderSpan.textContent = sms.sender || langData.unknownSender || "Unknown";

  const previewSpan = document.createElement("span");
  previewSpan.className = "sms-preview";
  // The body is ALREADY DECODED by the C++ code.
  let previewText = sms.body || "";
  previewSpan.textContent =
    previewText.substring(0, 40) + (previewText.length > 40 ? "..." : "");

  const dateSpan = document.createElement("span");
  dateSpan.className = "sms-date";
  dateSpan.textContent = sms.timestamp || "";

  li.appendChild(senderSpan);
  li.appendChild(previewSpan);
  li.appendChild(dateSpan);
  listElement.prepend(li);
}

function displaySmsContent(sms) {
  if (!sms || typeof sms.index === "undefined") {
    showNotification(
      langData.errorReadingSms || "Error reading SMS content.",
      true
    );
    return;
  }

  // The body is already decoded by the backend.
  let smsBody = sms.body;

  const listItem = document.querySelector(`li[data-index='${sms.index}']`);
  const sender = listItem
    ? listItem.querySelector(".sms-sender").textContent
    : "N/A";
  const timestamp = listItem
    ? listItem.querySelector(".sms-date").textContent
    : "N/A";

  currentSmsIndex = sms.index;
  setText("modal-sms-from", sender);
  setText("modal-sms-date", timestamp);
  setText("modal-sms-body", smsBody);
  openModal("sms-content-modal");
}

function handleSmsDeleted(data) {
  if (data && data.success) {
    showNotification(
      `${langData.smsDeletedSuccess || "SMS deleted."} (#${data.index || "?"})`,
      false,
      "success"
    );
    if (currentSmsIndex === data.index) {
      closeModal("sms-content-modal");
      currentSmsIndex = null;
    }
    refreshInbox();
  } else {
    showNotification(
      `${data?.message || langData.smsDeletedError || "Failed to delete SMS."}`,
      true
    );
  }
}

function displayCallerId(cid) {
  const dE = getElement("caller-id-display");
  if (!dE) return;
  const noC = langData.noIncomingCall || "No incoming call...";
  if (cid === null || typeof cid === "undefined") {
    dE.innerHTML = `<em>${noC}</em>`;
  } else {
    dE.innerHTML = `<strong>${
      langData.incomingCallLabel || "Incoming Call:"
    }</strong> ${cid}`;
  }
}

/**
 * Updates the character and message count for the SMS textarea.
 */
function updateCharCount() {
  const ta = getElement("sms-message");
  const cD = getElement("char-count");
  if (!ta || !cD || !langData || !langData.smsCharCount) return;
  const m = ta.value;
  const l = m.length;
  let isU = false;
  for (let i = 0; i < l; i++) {
    if (m.charCodeAt(i) > 127) {
      isU = true;
      break;
    }
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

/**
 * Shows a dismissable notification bar at the top of the page.
 * @param {string} msg The message to display.
 * @param {boolean} isErr True if it's an error message.
 * @param {string} type 'success', 'info', 'warning', or 'error'.
 */
function showNotification(msg, isErr = false, type = null) {
  const n = getElement("notification");
  if (!n) return;
  n.textContent = msg;
  n.className = "";
  n.classList.remove("error", "info", "warning", "success", "show");
  let nT = type || (isErr ? "error" : "success");
  n.classList.add(nT);
  requestAnimationFrame(() => {
    n.classList.add("show");
  });
  if (notificationTimeout) clearTimeout(notificationTimeout);
  const dur = nT === "error" || nT === "warning" ? 6000 : 4000;
  notificationTimeout = setTimeout(() => {
    n.classList.remove("show");
    notificationTimeout = null;
  }, dur);
}

/**
 * Prompts the user and sends a reboot command to the device.
 */
function rebootDevice() {
  if (confirm(langData.rebootConfirm || "Reboot device?")) {
    showNotification(langData.rebooting || "Rebooting...", false, "info");
    fetch("/reboot", { method: "POST" }).catch((e) =>
      console.error("Reboot fetch error:", e)
    );
  }
}

/**
 * Manages the enabled/disabled state of action buttons.
 */
function updateActionButtonStates() {
  const setDis = (selector, disabled, title = "") => {
    document.querySelectorAll(selector).forEach((el) => {
      el.disabled = disabled;
      el.title = disabled ? title : "";
    });
  };
  const disableReply = !ussdSessionActive;
  const reasonReply = !ussdSessionActive
    ? langData.ussdNoSession || "No active USSD session"
    : "";
  setDis(
    "#ussd-reply-form input, #ussd-reply-form button",
    disableReply,
    reasonReply
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
  getElement(id).style.display = "block";
}
function closeModal(id) {
  getElement(id).style.display = "none";
  if (id === "sms-content-modal") currentSmsIndex = null;
}

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
  return getElement(id)?.value || "";
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
  const pin = getValue("pin-input");
  if (!pin || !/^\d{4,8}$/.test(pin)) {
    showNotification(
      langData.pinInvalidFormat || "PIN must be 4-8 digits.",
      true
    );
    return;
  }
  showLoader("pin-loader");
  const fd = new FormData();
  fd.append("pin", pin);
  fetch("/enterpin", { method: "POST", body: new URLSearchParams(fd) })
    .then((r) => r.json())
    .then((d) => {
      hideLoader("pin-loader");
      showNotification(d.message, !d.success);
      if (d.success) {
        getElement("pin-entry-section").style.display = "none";
      }
    })
    .catch((er) => {
      hideLoader("pin-loader");
      showNotification(
        langData.pinSubmitError || "PIN submission failed.",
        true
      );
    });
}
function submitWifiForm(e) {
  e.preventDefault();
  const ssid = getValue("ssid");
  if (!ssid) {
    showNotification(langData.ssidRequired || "SSID is required.", true);
    return;
  }
  showNotification(
    langData.savingWifi || "Saving WiFi and rebooting...",
    false,
    "info"
  );
  const fd = new FormData(getElement("wifi-form"));
  fetch("/savewifi", { method: "POST", body: new URLSearchParams(fd) })
    .then((r) => r.json().catch(() => ({})))
    .then((d) => {
      if (d && !d.success) showNotification(d.message, true);
    })
    .catch((er) =>
      showNotification(langData.saveWifiError || "Failed to save WiFi.", true)
    );
}
function submitConfigForm(e) {
  e.preventDefault();
  const fd = new FormData(getElement("config-form"));
  showNotification(
    langData.savingConfig || "Saving settings...",
    false,
    "info"
  );
  fetch("/saveconfig", { method: "POST", body: new URLSearchParams(fd) })
    .then((r) => r.json())
    .then((d) => {
      showNotification(d.message, !d.success);
      if (d.success) requestConfig();
    })
    .catch((er) =>
      showNotification(
        langData.saveConfigError || "Failed to save settings.",
        true
      )
    );
}
function submitSmsForm(e) {
  e.preventDefault();
  const num = getValue("sms-number");
  const msg = getValue("sms-message");
  if (!num || !msg) {
    showNotification(
      langData.smsFieldsRequired || "Recipient and message are required.",
      true
    );
    return;
  }
  showLoader("sms-loader");
  sendWebSocketMessage({ action: "sendSMS", number: num, message: msg });
}
function submitUssdForm(e) {
  e.preventDefault();
  const code = getValue("ussd-code");
  if (!code) {
    showNotification(
      langData.ussdCodeRequired || "USSD code is required.",
      true
    );
    return;
  }
  getElement("ussd-response").innerHTML = `<em>${
    langData.sending || "Sending..."
  }</em>`;
  showLoader("ussd-loader");
  sendWebSocketMessage({ action: "sendUSSD", code: code });
}
function submitUssdReplyForm(e) {
  e.preventDefault();
  const reply = getValue("ussd-reply");
  if (!ussdSessionActive) return;
  if (!reply) {
    showNotification(langData.ussdReplyRequired || "Reply is required.", true);
    return;
  }
  showLoader("ussd-reply-loader");
  sendWebSocketMessage({ action: "sendUSSDReply", reply: reply });
}
function cancelUssdSession() {
  sendWebSocketMessage({ action: "cancelUSSD" });
  setText("ussd-response", langData.ussdCancelled || "USSD session cancelled.");
  getElement("ussd-reply-form").style.display = "none";
  getElement("ussd-form").style.display = "block";
  ussdSessionActive = false;
  updateActionButtonStates();
}

// --- UI Interaction Functions ---
function openTab(e, tN) {
  document
    .querySelectorAll(".tab-content")
    .forEach((t) => (t.style.display = "none"));
  document
    .querySelectorAll(".tab-button")
    .forEach((b) => b.classList.remove("active"));
  getElement(tN).style.display = "block";
  e.currentTarget.classList.add("active");
  activeTab = tN;
  if (tN === "sms") refreshInbox();
}
function scanWifi() {
  const list = getElement("wifi-list");
  list.innerHTML = `<li><em>${langData.scanning || "Scanning..."}</em></li>`;
  showLoader("wifi-loader");
  fetch("/scanwifi")
    .then((r) => r.json())
    .then((d) => {
      hideLoader("wifi-loader");
      list.innerHTML = "";
      if (d.success && d.networks?.length) {
        d.networks.forEach((n) => {
          const li = document.createElement("li");
          let sB = n.rssi > -67 ? "▂▄▆█" : n.rssi > -80 ? "▂▄▆" : "▂▄";
          li.innerHTML = `${sB} ${n.ssid} <span class="wifi-rssi">(${
            n.rssi
          } dBm)</span> ${n.secure ? "🔒" : ""}`;
          li.onclick = () => {
            setValue("ssid", n.ssid);
            getElement("password").focus();
          };
          list.appendChild(li);
        });
      } else {
        list.innerHTML = `<li><em>${
          d.message || langData.scanFailed || "Scan failed."
        }</em></li>`;
      }
    })
    .catch((e) => {
      hideLoader("wifi-loader");
      list.innerHTML = `<li><em>${
        langData.scanError || "Scan error."
      }</em></li>`;
    });
}

// --- Initialization ---
function getInitialModeAndSetupUI() {
  fetch("/getmode")
    .then((r) => r.json())
    .then((d) => {
      currentMode = d.mode;
      simPinRequired = d.sim_pin_required;
      updateActionButtonStates();
      handlePinRequirement();
      if (currentMode === "AP") {
        getElement("setup-mode").style.display = "block";
        getElement("station-mode").style.display = "none";
        scanWifi();
      } else {
        getElement("setup-mode").style.display = "none";
        getElement("station-mode").style.display = "block";
        initWebSocket();
      }
    })
    .catch((e) => {
      console.error("Get mode err:", e);
      getElement("setup-mode").style.display = "block";
      getElement("station-mode").style.display = "none";
      showNotification(
        langData.getModeError || "Failed to get device mode.",
        true
      );
    });
}
window.onload = async function () {
  console.log("Page loaded. Initializing application...");
  await initializeLanguage();

  getElement("wifi-form")?.addEventListener("submit", submitWifiForm);
  getElement("config-form")?.addEventListener("submit", submitConfigForm);
  getElement("sms-form")?.addEventListener("submit", submitSmsForm);
  getElement("ussd-form")?.addEventListener("submit", submitUssdForm);
  getElement("ussd-reply-form")?.addEventListener(
    "submit",
    submitUssdReplyForm
  );
  getElement("pin-form")?.addEventListener("submit", submitPinForm);

  window.addEventListener("click", function (e) {
    document.querySelectorAll(".modal").forEach((m) => {
      if (e.target == m) closeModal(m.id);
    });
  });

  getInitialModeAndSetupUI();

  setTimeout(() => {
    const iTB = document.querySelector(
      `.tab-button[onclick*="'${activeTab}'"]`
    );
    const iTC = getElement(activeTab);
    if (currentMode === "STA" && !simPinRequired && iTB && iTC) {
      iTB.classList.add("active");
      iTC.style.display = "block";
    }
  }, 100);

  console.log("Initialization complete.");
};
