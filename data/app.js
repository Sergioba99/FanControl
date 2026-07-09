function openUpdatePage() {
  window.location.href = window.location.origin + "/update";
}

function toggleCheckboxButton(button) {
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState === 4 && this.status === 200) {
      refreshTimerStateAfterCommand();
    }
  };
  xhr.open("GET", "/botones?button=" + encodeURIComponent(button.id), true);
  xhr.send();
}

function setTimerStatusText(text) {
  var element = document.getElementById("timer_status");
  if (element) {
    element.innerHTML = text;
  }
}

function setAutoStatusText(text) {
  var element = document.getElementById("auto_status");
  if (element) {
    element.innerHTML = text;
  }
}

var dashboardTimerDeadline = 0;
var dashboardTimerMode = "";

function formatRemainingTime(seconds) {
  var totalMinutes = Math.ceil(Math.max(0, seconds) / 60);
  var hours = Math.floor(totalMinutes / 60);
  var minutes = totalMinutes % 60;
  return (hours < 10 ? "0" : "") + hours + ":" + (minutes < 10 ? "0" : "") + minutes;
}

function applyTimerStatus(status) {
  if (!status.active) {
    dashboardTimerDeadline = 0;
    dashboardTimerMode = "";
    setTimerStatusText("Sin temporizador activo");
    return;
  }

  dashboardTimerMode = status.mode || "";
  if (typeof status.remainingSeconds === "number") {
    dashboardTimerDeadline = Date.now() + status.remainingSeconds * 1000;
  }
  renderDashboardTimer();
}

function renderDashboardTimer() {
  if (!dashboardTimerDeadline || !document.getElementById("timer_status")) {
    return;
  }
  var remainingSeconds = Math.ceil((dashboardTimerDeadline - Date.now()) / 1000);
  if (remainingSeconds <= 0) {
    dashboardTimerDeadline = 0;
    setTimerStatusText("Finalizando temporizador...");
    window.setTimeout(updateDashboardStatus, 500);
    return;
  }
  setTimerStatusText("Activo: " + dashboardTimerMode.toUpperCase() + " - restante " + formatRemainingTime(remainingSeconds));
}

function applyAutoStatus(status) {
  var checkbox = document.getElementById("auto-enabled");
  if (checkbox && checkbox.type === "checkbox") {
    checkbox.checked = status.enabled;
    updateAutoToggleLabel();
  }
  if (status.enabled) {
    var autoMode = status.autoMode ? status.autoMode.toUpperCase() : "PENDIENTE";
    setAutoStatusText("Auto activo: " + autoMode);
  } else {
    setAutoStatusText("Auto desactivado");
  }
}

function updateDashboardStatus() {
  if (!document.getElementById("local_time") && !document.getElementById("timer_status")) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState !== 4 || this.status !== 200) {
      return;
    }
    try {
      var status = JSON.parse(this.responseText);
      document.getElementById("local_time").textContent = status.time;
      document.getElementById("local_date").textContent = status.date;
      document.getElementById("temperature").textContent = status.temperature === null ? "--" : status.temperature;
      document.getElementById("humidity").textContent = status.humidity === null ? "--" : status.humidity;
      applyTimerStatus(status.timer);
      applyAutoStatus(status.auto);
    } catch (error) {
      setTimerStatusText("Estado del sistema no disponible");
    }
  };
  xhr.open("GET", "/api/status?t=" + Date.now(), true);
  xhr.send();
}

function refreshTimerStateAfterCommand() {
  window.setTimeout(updateTimerStatus, 250);
  window.setTimeout(function() {
    updateTimerStatus();
    updateDashboardStatus();
  }, 1500);
}

function updateAutoToggleLabel() {
  var checkbox = document.getElementById("auto-enabled");
  var label = document.getElementById("auto-toggle-label");
  if (!checkbox) {
    return;
  }

  var enabled = checkbox.type === "checkbox" ? checkbox.checked : checkbox.value === "1";
  if (label) {
    label.innerHTML = enabled ? "AUTO ON" : "AUTO OFF";
  }
}

function parseDecimal(value) {
  return parseFloat(String(value).replace(",", "."));
}

function sendTimerForm(form) {
  var xhr = new XMLHttpRequest();
  var hours = parseInt(form.elements["hours"].value, 10);
  var minutes = parseInt(form.elements["minutes"].value, 10);
  var mode = form.elements["mode"].value;

  if (isNaN(hours) || isNaN(minutes) || hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
    setTimerStatusText("Introduce una duracion valida");
    return;
  }

  if (hours === 0 && minutes === 0) {
    setTimerStatusText("La duracion minima es 1 minuto");
    return;
  }

  setTimerStatusText("Programando temporizador...");

  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        refreshTimerStateAfterCommand();
      } else {
        setTimerStatusText("No se pudo programar el temporizador (HTTP " + this.status + ")");
      }
    }
  };
  xhr.open("GET", "/temporizador?button=" + encodeURIComponent(mode) +
    "&hours=" + encodeURIComponent(hours) +
    "&minutes=" + encodeURIComponent(minutes), true);
  xhr.send();
}

function cancelTimer() {
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      refreshTimerStateAfterCommand();
    }
  };
  xhr.open("GET", "/temporizador?cancel=1", true);
  xhr.send();
}

function updateTimerStatus() {
  if (!document.getElementById("timer_status")) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        var status;
        try {
          status = JSON.parse(this.responseText);
        } catch (error) {
          setTimerStatusText("Respuesta de temporizador no valida");
          return;
        }

        if (status.active) {
          applyTimerStatus(status);
        } else {
          applyTimerStatus(status);
        }
      } else {
        setTimerStatusText("Estado del temporizador no disponible (HTTP " + this.status + ")");
      }
    }
  };
  xhr.open("GET", "/timer/status", true);
  xhr.send();
}

function updateAutoStatus() {
  if (!document.getElementById("auto_status") && !document.getElementById("auto-enabled") && !document.getElementById("auto-low")) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        var status;
        try {
          status = JSON.parse(this.responseText);
        } catch (error) {
          setAutoStatusText("Respuesta de auto no valida");
          return;
        }

        var autoForm = document.getElementById("auto-form");
        var autoEnabled = document.getElementById("auto-enabled");
        if (autoEnabled && (!autoForm || !autoForm.contains(document.activeElement))) {
          if (autoEnabled.type === "checkbox") {
            autoEnabled.checked = status.enabled;
          } else {
            autoEnabled.value = status.enabled ? "1" : "0";
          }
          updateAutoToggleLabel();
          if (document.getElementById("auto-low")) {
            document.getElementById("auto-low").value = Number(status.low).toFixed(1);
            document.getElementById("auto-med").value = Number(status.med).toFixed(1);
            document.getElementById("auto-high").value = Number(status.high).toFixed(1);
            document.getElementById("auto-humidity").value = Number(status.humidity).toFixed(1);
            document.getElementById("auto-temp-rise").value = Number(status.tempRise).toFixed(1);
            document.getElementById("auto-temp-fall").value = Number(status.tempFall).toFixed(1);
            document.getElementById("auto-humidity-hysteresis").value = Number(status.humidityHysteresis).toFixed(1);
            document.getElementById("auto-min-change").value = Number(status.minChange);
          }
        }

        applyAutoStatus(status);
      } else {
        setAutoStatusText("Auto no disponible (HTTP " + this.status + ")");
      }
    }
  };
  xhr.open("GET", "/auto/status", true);
  xhr.send();
}

function updateRfStatus() {
  if (!document.getElementById("rf-form")) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var status;
      try {
        status = JSON.parse(this.responseText);
      } catch (error) {
        return;
      }

      var rfForm = document.getElementById("rf-form");
      if (rfForm.contains(document.activeElement)) {
        return;
      }

      document.getElementById("rf-default").value = Number(status.default);
      document.getElementById("rf-low").value = Number(status.low);
      document.getElementById("rf-med").value = Number(status.med);
      document.getElementById("rf-high").value = Number(status.high);
      document.getElementById("rf-off").value = Number(status.off);
    }
  };
  xhr.open("GET", "/rf/status", true);
  xhr.send();
}

function sendAutoForm(form) {
  var enabledElement = form.elements["enabled"];
  var enabled = enabledElement.type === "checkbox" ? (enabledElement.checked ? "1" : "0") : enabledElement.value;
  var low = parseDecimal(form.elements["low"].value);
  var med = parseDecimal(form.elements["med"].value);
  var high = parseDecimal(form.elements["high"].value);
  var humidity = parseDecimal(form.elements["humidity"].value);
  var tempRise = parseDecimal(form.elements["tempRise"].value);
  var tempFall = parseDecimal(form.elements["tempFall"].value);
  var humidityHysteresis = parseDecimal(form.elements["humidityHysteresis"].value);
  var minChange = parseInt(form.elements["minChange"].value, 10);

  if (isNaN(low) || isNaN(med) || isNaN(high) || isNaN(humidity) ||
      isNaN(tempRise) || isNaN(tempFall) || isNaN(humidityHysteresis) || isNaN(minChange) ||
      low < 10 || high > 45 || humidity < 30 || humidity > 95 ||
      tempRise < 0 || tempRise > 5 || tempFall < 0 || tempFall > 5 ||
      humidityHysteresis < 0 || humidityHysteresis > 30 || minChange < 0 || minChange > 3600 ||
      !(low < med && med < high)) {
    setAutoStatusText("Umbrales no validos");
    return;
  }

  setAutoStatusText("Guardando auto...");

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        window.setTimeout(updateAutoStatus, 200);
        refreshTimerStateAfterCommand();
      } else {
        setAutoStatusText("No se pudo guardar auto (HTTP " + this.status + ")");
      }
    }
  };
  xhr.open("GET", "/auto?enabled=" + encodeURIComponent(enabled) +
    "&low=" + encodeURIComponent(low.toFixed(1)) +
    "&med=" + encodeURIComponent(med.toFixed(1)) +
    "&high=" + encodeURIComponent(high.toFixed(1)) +
    "&humidity=" + encodeURIComponent(humidity.toFixed(1)) +
    "&tempRise=" + encodeURIComponent(tempRise.toFixed(1)) +
    "&tempFall=" + encodeURIComponent(tempFall.toFixed(1)) +
    "&humidityHysteresis=" + encodeURIComponent(humidityHysteresis.toFixed(1)) +
    "&minChange=" + encodeURIComponent(minChange), true);
  xhr.send();
}

function parseRepeat(value) {
  var repeat = parseInt(value, 10);
  if (isNaN(repeat) || repeat < 1 || repeat > 80) {
    return null;
  }
  return repeat;
}

function sendRfForm(form) {
  var defaultRepeat = parseRepeat(form.elements["default"].value);
  var lowRepeat = parseRepeat(form.elements["low"].value);
  var medRepeat = parseRepeat(form.elements["med"].value);
  var highRepeat = parseRepeat(form.elements["high"].value);
  var offRepeat = parseRepeat(form.elements["off"].value);

  if (defaultRepeat === null || lowRepeat === null || medRepeat === null ||
      highRepeat === null || offRepeat === null) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      window.setTimeout(updateRfStatus, 200);
    }
  };
  xhr.open("GET", "/rf?default=" + encodeURIComponent(defaultRepeat) +
    "&low=" + encodeURIComponent(lowRepeat) +
    "&med=" + encodeURIComponent(medRepeat) +
    "&high=" + encodeURIComponent(highRepeat) +
    "&off=" + encodeURIComponent(offRepeat), true);
  xhr.send();
}

function updateNetworkStatus() {
  if (!document.getElementById("network-ip")) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState !== 4) {
      return;
    }
    if (this.status !== 200) {
      document.getElementById("network-message").textContent = "Estado de red no disponible";
      return;
    }

    try {
      var status = JSON.parse(this.responseText);
      document.getElementById("network-ssid").textContent = status.ssid || "Sin configurar";
      document.getElementById("network-ip").textContent = status.ip || "Sin conexion";
      document.getElementById("network-mode").textContent = status.mode === "static" ? "IP estatica" : "DHCP";
      document.getElementById("network-rssi").textContent = status.connected ? status.rssi + " dBm" : "--";
      document.getElementById("network-profile-count").textContent = status.profileCount + "/5";
      document.getElementById("network-message").textContent = status.connected ? "Conectado" : "Sin conexion";
    } catch (error) {
      document.getElementById("network-message").textContent = "Respuesta de red no valida";
    }
  };
  xhr.open("GET", "/wifi/status", true);
  xhr.send();
}

function resetWifiSettings() {
  if (!window.confirm("Se borraran todas las redes guardadas y se abrira FanControl-Setup.")) {
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState === 4) {
      var message = document.getElementById("network-message");
      if (this.status === 200) {
        message.textContent = "Redes borradas. Conectate a FanControl-Setup.";
      } else {
        message.textContent = "No se pudo borrar la configuracion WiFi.";
      }
    }
  };
  xhr.open("POST", "/wifi/reset", true);
  xhr.send();
}

function setAutoEnabled(enabled) {
  var xhr = new XMLHttpRequest();
  setAutoStatusText("Guardando auto...");
  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        window.setTimeout(updateAutoStatus, 200);
        refreshTimerStateAfterCommand();
      } else {
        setAutoStatusText("No se pudo cambiar auto (HTTP " + this.status + ")");
      }
    }
  };
  xhr.open("GET", "/auto?enabled=" + encodeURIComponent(enabled ? "1" : "0"), true);
  xhr.send();
}

function initApp() {
  var updateButton = document.getElementById("update-button");
  if (updateButton) {
    updateButton.addEventListener("click", openUpdatePage);
  }

  var timerForm = document.getElementById("timer-form");
  if (timerForm) {
    timerForm.addEventListener("submit", function(event) {
      event.preventDefault();
      sendTimerForm(this);
    });
  }

  var cancelTimerButton = document.getElementById("cancel-timer-button");
  if (cancelTimerButton) {
    cancelTimerButton.addEventListener("click", cancelTimer);
  }

  var autoForm = document.getElementById("auto-form");
  if (autoForm) {
    autoForm.addEventListener("submit", function(event) {
      event.preventDefault();
      sendAutoForm(this);
    });
  }

  var rfForm = document.getElementById("rf-form");
  if (rfForm) {
    rfForm.addEventListener("submit", function(event) {
      event.preventDefault();
      sendRfForm(this);
    });
  }

  var resetWifiButton = document.getElementById("reset-wifi-button");
  if (resetWifiButton) {
    resetWifiButton.addEventListener("click", resetWifiSettings);
  }

  var autoEnabled = document.getElementById("auto-enabled");
  if (autoEnabled) {
    autoEnabled.addEventListener("change", function() {
      updateAutoToggleLabel();
      if (autoEnabled.type === "checkbox") {
        setAutoEnabled(autoEnabled.checked);
      }
    });
  }

  if (document.getElementById("local_time")) {
    updateDashboardStatus();
    setInterval(updateDashboardStatus, 10000);
    setInterval(renderDashboardTimer, 1000);
  }
  if (autoForm) {
    updateAutoStatus();
    setInterval(updateAutoStatus, 5000);
  }
  if (rfForm) {
    updateRfStatus();
  }
  if (document.getElementById("network-ip")) {
    updateNetworkStatus();
    setInterval(updateNetworkStatus, 5000);
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initApp);
} else {
  initApp();
}
