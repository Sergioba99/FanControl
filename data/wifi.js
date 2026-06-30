var wifiStatusTimer = null;
var wifiStatusInProgress = false;

function requestJson(endpoint, callback) {
  var xhr = new XMLHttpRequest();
  var completed = false;

  function finish(error, response) {
    if (completed) {
      return;
    }
    completed = true;
    callback(error, response);
  }

  xhr.onreadystatechange = function() {
    if (this.readyState !== 4 || completed) {
      return;
    }

    var response;
    try {
      response = this.responseText ? JSON.parse(this.responseText) : {};
    } catch (error) {
      finish("Respuesta no valida del ESP32", null);
      return;
    }

    if (this.status < 200 || this.status >= 300) {
      finish(response.error || "Error HTTP " + this.status, response);
      return;
    }
    finish(null, response);
  };
  xhr.onerror = function() {
    finish("No se pudo comunicar con el ESP32", null);
  };
  xhr.ontimeout = function() {
    finish("El ESP32 no respondio a tiempo", null);
  };
  xhr.open("GET", endpoint, true);
  xhr.timeout = 10000;
  xhr.send();
}

function setWifiMessage(message, isError) {
  var element = document.getElementById("wifi-message");
  element.textContent = message;
  element.classList.toggle("form-error", Boolean(isError));
}

function setStaticFieldsVisible() {
  var staticMode = document.querySelector('input[name="mode"]:checked').value === "static";
  var fields = document.getElementById("static-ip-fields");
  fields.hidden = !staticMode;
  Array.prototype.forEach.call(fields.querySelectorAll("input"), function(input) {
    input.required = staticMode;
  });
}

function loadWifiStatus() {
  if (wifiStatusInProgress) {
    return;
  }

  wifiStatusInProgress = true;
  requestJson("/wifi/status?t=" + Date.now(), function(error, status) {
    wifiStatusInProgress = false;
    if (error) {
      setWifiMessage(error + ". Puedes usar Actualizar estado.", true);
      return;
    }

    document.getElementById("wifi-connection").textContent = status.connected ? "Conectado a " + status.ssid : "Sin conexion";
    document.getElementById("wifi-current-ip").textContent = status.ip || "--";
    document.getElementById("finish-wifi-button").hidden = !(status.connected && status.portalActive);

    if (status.connected) {
      setWifiMessage("Conexion completada. Anota la IP antes de finalizar.", false);
    }
  });
}

document.addEventListener("DOMContentLoaded", function() {
  Array.prototype.forEach.call(document.querySelectorAll('input[name="mode"]'), function(input) {
    input.addEventListener("change", setStaticFieldsVisible);
  });
  document.getElementById("wifi-network-select").addEventListener("change", function() {
    if (this.value) {
      document.getElementById("wifi-ssid").value = this.value;
    }
  });
  Array.prototype.forEach.call(document.querySelectorAll(".wifi-delete-form"), function(form) {
    form.addEventListener("submit", function(event) {
      if (!window.confirm("Se eliminara esta red guardada.")) {
        event.preventDefault();
      }
    });
  });
  setStaticFieldsVisible();
  loadWifiStatus();
  wifiStatusTimer = window.setInterval(loadWifiStatus, 3000);
});
