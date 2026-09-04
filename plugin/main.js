(() => {
  const DEFAULTS = {
    enabled: true,
    startWithNcm: true,
    httpPort: 3412,
    sources: "",
  };

  const STATE = {
    Disabled: "Disabled",
    Starting: "Starting",
    Running: "Running",
  };

  const PROXY_HOST = "127.0.0.1";
  const PLUGIN_NAME = "UnblockLite";
  let currentState = STATE.Disabled;
  let ui = null;

  function configOf() {
    const read = (key, fallback) => {
      try {
        if (plugin && typeof plugin.getConfig === "function") {
          return plugin.getConfig(key, fallback);
        }
      } catch (_) {
        /* keep fallback */
      }
      return fallback;
    };
    const httpPort = Number(read("httpPort", DEFAULTS.httpPort));
    return {
      enabled: Boolean(read("enabled", DEFAULTS.enabled)),
      startWithNcm: Boolean(read("startWithNcm", DEFAULTS.startWithNcm)),
      httpPort: Number.isInteger(httpPort) && httpPort > 0 && httpPort < 65536
        ? httpPort
        : DEFAULTS.httpPort,
      sources: String(read("sources", DEFAULTS.sources) || ""),
    };
  }

  function writeConfig(next) {
    if (!plugin || typeof plugin.setConfig !== "function") {
      return;
    }
    plugin.setConfig("enabled", next.enabled);
    plugin.setConfig("startWithNcm", next.startWithNcm);
    plugin.setConfig("httpPort", next.httpPort);
    plugin.setConfig("sources", next.sources);
  }

  function joinPath(...parts) {
    return parts
      .filter((part) => part !== undefined && part !== null && String(part).length > 0)
      .join("\\")
      .replace(/[\\/]+/g, "\\");
  }

  function quoteCmd(value) {
    // Escape for embedding inside a double-quoted Windows command-line fragment.
    return `"${String(value).replace(/"/g, '\\"')}"`;
  }

  function quotePsSingle(value) {
    return `'${String(value).replace(/'/g, "''")}'`;
  }

  function isValidSourceName(token) {
    return /^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(token);
  }

  function sanitizeSources(raw) {
    return String(raw || "")
      .split(/[,\s;]+/)
      .map((item) => item.trim())
      .filter((item) => item.length > 0 && isValidSourceName(item));
  }

  function normalizeDir(path) {
    return String(path || "")
      .replace(/[\\/]+$/g, "")
      .replace(/\//g, "\\");
  }

  function pluginRuntimeRoot() {
    if (plugin && plugin.pluginPath) {
      return normalizeDir(plugin.pluginPath);
    }
    if (typeof loadedPlugins !== "undefined" && loadedPlugins[PLUGIN_NAME]) {
      return normalizeDir(loadedPlugins[PLUGIN_NAME].pluginPath || "");
    }
    return "";
  }

  async function dataRoot() {
    try {
      if (betterncm && betterncm.app && typeof betterncm.app.getDataPath === "function") {
        return normalizeDir(await betterncm.app.getDataPath());
      }
    } catch (_) {
      /* fall through */
    }
    return "";
  }

  async function searchRoots() {
    const roots = [];
    const runtime = pluginRuntimeRoot();
    if (runtime) {
      roots.push(runtime);
    }
    const data = await dataRoot();
    if (data) {
      roots.push(joinPath(data, PLUGIN_NAME));
      roots.push(data);
    }
    // Relative paths accepted by betterncm.fs / native helpers (Revived style).
    roots.push(`./${PLUGIN_NAME}`);
    roots.push(".");
    return roots;
  }

  async function fileExists(path) {
    if (!path) return false;
    try {
      if (
        typeof betterncm_native !== "undefined" &&
        betterncm_native.fs &&
        typeof betterncm_native.fs.exists === "function"
      ) {
        if (betterncm_native.fs.exists(path)) {
          return true;
        }
      }
    } catch (_) {
      /* fall through */
    }
    try {
      if (betterncm && betterncm.fs && typeof betterncm.fs.exists === "function") {
        return Boolean(await betterncm.fs.exists(path));
      }
    } catch (_) {
      /* fall through */
    }
    return false;
  }

  async function firstExisting(candidates) {
    for (const candidate of candidates) {
      if (await fileExists(candidate)) {
        return candidate;
      }
    }
    return "";
  }

  async function resolveHost() {
    const roots = await searchRoots();
    const names = ["unm-host.exe", joinPath("native", "unm-host.exe")];
    const candidates = [];
    for (const root of roots) {
      for (const name of names) {
        candidates.push(joinPath(root, name));
      }
    }
    return firstExisting(candidates);
  }

  async function resolveUnm() {
    const roots = await searchRoots();
    const names = [
      "UnblockNeteaseMusic.exe",
      "unblockneteasemusic-win-x64.exe",
      joinPath("core", "UnblockNeteaseMusic.exe"),
      joinPath("core", "unblockneteasemusic-win-x64.exe"),
    ];
    const candidates = [];
    for (const root of roots) {
      for (const name of names) {
        candidates.push(joinPath(root, name));
      }
    }
    return firstExisting(candidates);
  }

  async function ncmExecutable() {
    let raw = "";
    try {
      raw = String((await betterncm.app.getNCMPath()) || "");
    } catch (_) {
      raw = "";
    }
    if (!raw) {
      return "";
    }
    if (/cloudmusic\.exe$/i.test(raw)) {
      return raw;
    }
    return joinPath(raw, "cloudmusic.exe");
  }

  function httpsPort(httpPort) {
    return httpPort + 1;
  }

  function pacUrl(httpPort) {
    return `http://${PROXY_HOST}:${httpPort}/proxy.pac`;
  }

  async function pacReady(httpPort, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      try {
        const response = await fetch(pacUrl(httpPort), { cache: "no-store" });
        if (response.ok) {
          const body = await response.text();
          if (body && body.length > 0) {
            return true;
          }
        }
      } catch (_) {
        /* not ready */
      }
      await sleep(250);
    }
    return false;
  }

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function exec(command) {
    return betterncm.app.exec(command, false, false);
  }

  async function absoluteForExec(path) {
    const text = String(path || "");
    if (!text) {
      return "";
    }
    if (/^[A-Za-z]:[\\/]/.test(text) || text.startsWith("\\\\")) {
      return text;
    }
    const data = await dataRoot();
    if (!data) {
      return text;
    }
    const relative = text.replace(/^\.[\\/]/, "");
    return joinPath(data, relative);
  }

  async function startHost() {
    const settings = configOf();
    const ncm = await ncmExecutable();
    const unm = await absoluteForExec(await resolveUnm());
    const host = await absoluteForExec(await resolveHost());
    if (!host || !unm || !ncm) {
      throw new Error(
        "Missing unm-host.exe, UNM executable, or NCM path. Put UNM at BetterNCM data/UnblockLite/UnblockNeteaseMusic.exe",
      );
    }
    // PS5 Start-Process joins ArgumentList arrays with spaces and does NOT
    // quote tokens, so paths under "Program Files" must be one pre-quoted string.
    const pieces = [
      `--ncm ${quoteCmd(ncm)}`,
      `--unm ${quoteCmd(unm)}`,
      `--http ${settings.httpPort}`,
      `--https ${httpsPort(settings.httpPort)}`,
    ];
    const sources = sanitizeSources(settings.sources);
    if (sources.length > 0) {
      pieces.push(`--sources ${quoteCmd(sources.join(","))}`);
    }
    const argumentList = pieces.join(" ");
    const command =
      "powershell -NoProfile -WindowStyle Hidden -Command " +
      `Start-Process -FilePath ${quotePsSingle(host)} ` +
      `-ArgumentList ${quotePsSingle(argumentList)} ` +
      "-WindowStyle Hidden";
    await exec(command);
  }

  async function stopHost() {
    const host = await absoluteForExec(await resolveHost());
    if (!host) {
      return;
    }
    const command =
      "powershell -NoProfile -WindowStyle Hidden -Command " +
      `Start-Process -FilePath ${quotePsSingle(host)} ` +
      `-ArgumentList ${quotePsSingle("--stop")} ` +
      "-WindowStyle Hidden -Wait";
    await exec(command);
  }

  function parseProxy(raw) {
    if (!raw) {
      return null;
    }
    try {
      return typeof raw === "string" ? JSON.parse(raw) : raw;
    } catch (_) {
      return null;
    }
  }

  function proxyMatches(config, httpPort) {
    if (!config || String(config.Type).toLowerCase() !== "http" || !config.http) {
      return false;
    }
    return String(config.http.Host) === PROXY_HOST &&
      Number(config.http.Port) === httpPort;
  }

  function callChannel(name, args) {
    return new Promise((resolve, reject) => {
      try {
        channel.call(name, resolve, args);
      } catch (error) {
        reject(error);
      }
    });
  }

  async function ensureProxy(httpPort) {
    const current = parseProxy(
      await callChannel("app.getLocalConfig", ["Proxy", ""]),
    );
    if (proxyMatches(current, httpPort)) {
      return false;
    }
    const next = {
      Type: "http",
      http: { Host: PROXY_HOST, Port: String(httpPort) },
    };
    await callChannel("app.setLocalConfig", [
      "Proxy",
      "",
      JSON.stringify(next),
    ]);
    return true;
  }

  function setState(next) {
    currentState = next;
    if (ui && ui.state) {
      ui.state.textContent = next;
    }
  }

  async function becomeRunning() {
    const settings = configOf();
    await ensureProxy(settings.httpPort);
    setState(STATE.Running);
  }

  async function startIfNeeded() {
    const settings = configOf();
    if (!settings.enabled) {
      setState(STATE.Disabled);
      return;
    }
    setState(STATE.Starting);
    if (await pacReady(settings.httpPort, 750)) {
      await becomeRunning();
      return;
    }
    await startHost();
    if (!(await pacReady(settings.httpPort, 10000))) {
      setState(STATE.Disabled);
      throw new Error(
        "UNM did not become ready on 127.0.0.1:" +
          settings.httpPort +
          ". Sources must be UNM match-order ids (e.g. kugou,kuwo), not 127.0.0.1; leave empty for defaults. Ensure UnblockNeteaseMusic.exe is under BetterNCM data/UnblockLite/.",
      );
    }
    await becomeRunning();
  }

  async function disableAndStop() {
    const settings = configOf();
    settings.enabled = false;
    writeConfig(settings);
    await stopHost();
    setState(STATE.Disabled);
  }

  function bindUi(root) {
    const settings = configOf();
    root.innerHTML = "";
    root.style.padding = "12px";
    root.style.lineHeight = "1.6";

    const title = document.createElement("h3");
    title.textContent = "UnblockLite";
    root.appendChild(title);

    const state = document.createElement("div");
    state.textContent = currentState;
    root.appendChild(state);

    const enabled = document.createElement("label");
    const enabledBox = document.createElement("input");
    enabledBox.type = "checkbox";
    enabledBox.checked = settings.enabled;
    enabled.appendChild(enabledBox);
    enabled.appendChild(document.createTextNode(" Enabled"));
    root.appendChild(enabled);

    const startWith = document.createElement("label");
    const startBox = document.createElement("input");
    startBox.type = "checkbox";
    startBox.checked = settings.startWithNcm;
    startWith.appendChild(startBox);
    startWith.appendChild(document.createTextNode(" Start with NCM"));
    root.appendChild(startWith);

    const portLabel = document.createElement("label");
    portLabel.textContent = "HTTP port ";
    const port = document.createElement("input");
    port.type = "number";
    port.min = "1";
    port.max = "65534";
    port.value = String(settings.httpPort);
    portLabel.appendChild(port);
    root.appendChild(portLabel);

    const sourceLabel = document.createElement("label");
    sourceLabel.textContent = "Sources ";
    const sources = document.createElement("input");
    sources.type = "text";
    sources.placeholder = "empty = UNM defaults (not 127.0.0.1)";
    // Migrate common misconfig: proxy host typed into Sources.
    const initialSources = sanitizeSources(settings.sources).join(",");
    if (settings.sources && initialSources !== settings.sources.trim()) {
      writeConfig({ ...settings, sources: initialSources });
    }
    sources.value = initialSources;
    sourceLabel.appendChild(sources);
    root.appendChild(sourceLabel);

    const note = document.createElement("p");
    note.textContent =
      "Install UnblockLite.plugin into BetterNCM plugins. Place official UNM v0.28.0 as UnblockNeteaseMusic.exe under BetterNCM data/UnblockLite/. Sources are UNM -o match-order ids (kugou,kuwo,migu,...), not an IP. Closing NCM to tray keeps UNM; tray Exit reclaims it.";
    root.appendChild(note);

    const save = document.createElement("button");
    save.textContent = "Save & apply";
    save.onclick = async () => {
      const next = {
        enabled: enabledBox.checked,
        startWithNcm: startBox.checked,
        httpPort: Number(port.value) || DEFAULTS.httpPort,
        sources: sanitizeSources(sources.value).join(","),
      };
      sources.value = next.sources;
      writeConfig(next);
      try {
        if (next.enabled) {
          await startIfNeeded();
        } else {
          await disableAndStop();
        }
      } catch (error) {
        state.textContent = String(error && error.message ? error.message : error);
      }
    };
    root.appendChild(save);
    ui = { state };
  }

  plugin.onLoad(() => {
    const settings = configOf();
    if (settings.enabled && settings.startWithNcm) {
      startIfNeeded().catch((error) => {
        currentState = STATE.Disabled;
        console.error("[UnblockLite]", error);
      });
    } else {
      currentState = STATE.Disabled;
    }
  });

  plugin.onConfig(() => {
    const root = document.createElement("div");
    bindUi(root);
    return root;
  });
})();
