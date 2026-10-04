import { spawn, spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const web = fileURLToPath(new URL("../", import.meta.url));
const root = path.dirname(web.replace(/[\\/]$/, ""));
const venv = path.join(
  root,
  ".venv",
  process.platform === "win32" ? "Scripts/python.exe" : "bin/python",
);
const python =
  process.env.BD2_PYTHON ||
  (existsSync(venv)
    ? venv
    : process.platform === "win32"
      ? "python"
      : "python3");
const check = spawnSync(python, ["-c", "import fastapi, uvicorn"], {
  cwd: root,
  windowsHide: true,
});
if (check.status !== 0) {
  console.error(
    "Faltan las dependencias de Python. Desde la raíz ejecuta: python -m pip install -r api/requirements.txt",
  );
  process.exit(1);
}
const children = [];
let closing = false;
function close(code = 0) {
  if (closing) return;
  closing = true;
  for (const child of children) child.kill();
  process.exitCode = code;
}
for (const [command, args, cwd] of [
  [
    python,
    [
      "-m",
      "uvicorn",
      "api.web_api:app",
      "--host",
      "127.0.0.1",
      "--port",
      "8000",
    ],
    root,
  ],
  [process.execPath, [path.join(web, "node_modules/vite/bin/vite.js")], web],
]) {
  const child = spawn(command, args, {
    cwd,
    stdio: "inherit",
    windowsHide: true,
  });
  children.push(child);
  child.on("error", (error) => {
    console.error(error.message);
    close(1);
  });
  child.on("exit", (code) => {
    if (!closing) close(code ?? 1);
  });
}
process.on("SIGINT", () => close());
process.on("SIGTERM", () => close());
