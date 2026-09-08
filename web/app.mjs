import {prepareApplication} from "./isolation.mjs";
import {FileStager, persistentStorage} from "./storage.mjs";

const archive = document.querySelector("#archive");
const play = document.querySelector("#play");
const reload = document.querySelector("#reload");
const status = document.querySelector("#status");
const progress = document.querySelector("#progress");
const note = document.querySelector("#storage-note");
const canvas = document.querySelector("#canvas");
const MAX_IMPORT_BYTES = 4 * 1024 * 1024 * 1024;
let moduleReady;

function setStatus(message, failed = false) {
  status.textContent = message;
  status.dataset.failed = failed ? "true" : "false";
}

async function loadRuntime() {
  window.Module = {
    canvas,
    noInitialRun: true,
    print: text => console.info(`[lf2] ${text}`),
    printErr: text => console.error(`[lf2] ${text}`),
    onSetupStatus: setStatus,
    onGameReady: () => {
      document.body.classList.add("playing");
      canvas.focus();
    },
    onAbort: reason => setStatus(`The browser runtime stopped: ${reason}`, true),
    onExit: code => setStatus(`The game closed (status ${code}).`, code !== 0),
  };
  moduleReady = new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = "lf2.js";
    script.onload = () => resolve(window.Module);
    script.onerror = () => reject(new Error("The LF2 WebAssembly runtime could not load."));
    document.head.append(script);
  });
  return moduleReady;
}

async function start(args) {
  const runtime = await moduleReady;
  archive.disabled = true;
  play.disabled = true;
  setStatus(args.length ? "Checking and unpacking your LF2 game files…" : "Starting saved LF2 installation…");
  runtime.callMain(args);
}

archive.addEventListener("change", async () => {
  const file = archive.files?.[0];
  if (!file) return;
  progress.hidden = false;
  progress.value = 0;
  setStatus("Copying the selected ZIP into private browser storage…");
  try {
    const staged = await new FileStager().stage(file, {
      directory: "incoming",
      name: "input.zip",
      maxBytes: MAX_IMPORT_BYTES,
      progress: ({bytes, total}) => { progress.value = total ? bytes / total : 0; },
    });
    note.textContent = `${staged.bytes.toLocaleString()} bytes staged locally.`;
    await start(["--import"]);
  } catch (error) {
    setStatus(error instanceof Error ? error.message : String(error), true);
    archive.disabled = false;
  } finally {
    progress.hidden = true;
  }
});

play.addEventListener("click", () => start([]).catch(error => setStatus(String(error), true)));
reload.addEventListener("click", () => location.reload());

async function prepare() {
  const {root, persistent} = await persistentStorage();
  note.textContent = persistent
    ? "Game files and saves are kept in persistent storage on this device."
    : "The browser has not granted persistent storage. It may clear game files and saves under storage pressure.";
  await navigator.locks.request("lucent-import:incoming", {ifAvailable: true}, async lock => {
    if (!lock) throw new Error("Another tab is importing game files.");
    try {
      const incoming = await root.getDirectoryHandle("incoming");
      await incoming.removeEntry("input.zip");
    } catch (error) {
      if (error.name !== "NotFoundError") throw error;
    }
  });
  archive.disabled = false;
  try {
    const user = await root.getDirectoryHandle("user");
    const port = await user.getDirectoryHandle("lf2-port");
    await port.getDirectoryHandle("game-import");
    play.disabled = false;
  } catch (error) {
    if (error.name !== "NotFoundError") throw error;
    play.disabled = true;
  }
  setStatus("Choose a game ZIP or start the saved installation.");
  await loadRuntime();
}

try {
  if (await prepareApplication()) {
    if (!navigator.locks) throw new Error("This browser cannot protect private game storage.");
    await navigator.locks.request("lf2-application", {ifAvailable: true}, async lock => {
      if (!lock) throw new Error("Little Fighter 2 is already open in another tab. Close it first.");
      await prepare();
      // Keep ownership of the install and save tree while native workers run.
      await new Promise(() => {});
    });
  }
} catch (error) {
  setStatus(error instanceof Error ? error.message : String(error), true);
}
