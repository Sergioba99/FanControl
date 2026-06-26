function getText(endpoint, elementId) {
  var element = document.getElementById(elementId);
  if (!element) {
    return;
  }

  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      element.innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", endpoint, true);
  xhttp.send();
}

function openUpdatePage() {
  window.location.href = window.location.origin + "/update";
}

function toggleCheckboxButton(button) {
  var xhr = new XMLHttpRequest();
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
        updateTimerStatus();
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
    if (this.readyState == 4) {
      updateTimerStatus();
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
          setTimerStatusText("Activo: " + status.mode.toUpperCase() + " - restante " + status.remaining);
        } else {
          setTimerStatusText("Sin temporizador activo");
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
          }
        }

        if (status.enabled) {
          var autoMode = status.autoMode ? status.autoMode.toUpperCase() : "PENDIENTE";
          setAutoStatusText("Auto activo: " + autoMode + " - ventilador " + status.currentMode.toUpperCase());
        } else {
          setAutoStatusText("Auto desactivado");
        }
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

  if (isNaN(low) || isNaN(med) || isNaN(high) || isNaN(humidity) ||
      low < 10 || high > 45 || humidity < 30 || humidity > 95 ||
      !(low < med && med < high)) {
    setAutoStatusText("Umbrales no validos");
    return;
  }

  setAutoStatusText("Guardando auto...");

  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        updateAutoStatus();
        updateTimerStatus();
      } else {
        setAutoStatusText("No se pudo guardar auto (HTTP " + this.status + ")");
      }
    }
  };
  xhr.open("GET", "/auto?enabled=" + encodeURIComponent(enabled) +
    "&low=" + encodeURIComponent(low.toFixed(1)) +
    "&med=" + encodeURIComponent(med.toFixed(1)) +
    "&high=" + encodeURIComponent(high.toFixed(1)) +
    "&humidity=" + encodeURIComponent(humidity.toFixed(1)), true);
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
      updateRfStatus();
    }
  };
  xhr.open("GET", "/rf?default=" + encodeURIComponent(defaultRepeat) +
    "&low=" + encodeURIComponent(lowRepeat) +
    "&med=" + encodeURIComponent(medRepeat) +
    "&high=" + encodeURIComponent(highRepeat) +
    "&off=" + encodeURIComponent(offRepeat), true);
  xhr.send();
}

function setAutoEnabled(enabled) {
  var xhr = new XMLHttpRequest();
  setAutoStatusText("Guardando auto...");
  xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      if (this.status == 200) {
        updateAutoStatus();
        updateTimerStatus();
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

  var autoEnabled = document.getElementById("auto-enabled");
  if (autoEnabled) {
    autoEnabled.addEventListener("change", function() {
      updateAutoToggleLabel();
      if (autoEnabled.type === "checkbox") {
        setAutoEnabled(autoEnabled.checked);
      }
    });
  }

  getText("/localtime", "local_time");
  getText("/localdate", "local_date");
  getText("/temperature", "temperature");
  getText("/humidity", "humidity");

  setInterval(function() {
    getText("/localtime", "local_time");
  }, 2000);
  setInterval(function() {
    getText("/localdate", "local_date");
  }, 2000);
  setInterval(function() {
    getText("/temperature", "temperature");
  }, 2000);
  setInterval(function() {
    getText("/humidity", "humidity");
  }, 2000);

  updateTimerStatus();
  updateAutoStatus();
  updateRfStatus();
  setInterval(updateTimerStatus, 1000);
  setInterval(updateAutoStatus, 5000);
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initApp);
} else {
  initApp();
}
