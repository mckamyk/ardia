import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { launchArdia } from "./ardia.mjs";
import { summarizeScenario } from "./report.mjs";
import { errorText, withDeadline } from "./util.mjs";

export class ScenarioAbort extends Error {}

export function createContext({ scenario, options, fixtures, resultsDir, scratchRoot, launch = launchArdia, log = () => {} }) {
  const checks = [];
  const apps = new Set();
  const scratches = [];
  const evidenceDir = join(resultsDir, scenario.id);
  let evidenceReady;
  const ensureEvidence = () => (evidenceReady ??= mkdir(evidenceDir, { recursive: true }));
  const t = {
    scenario,
    options,
    fixtures,
    checks,
    evidenceDir,
    log: (message) => log(`[${scenario.id}] ${message}`),
    async scratch(name = "profile") {
      const dir = join(scratchRoot, `${scenario.id}-${name}-${scratches.length}`);
      await rm(dir, { recursive: true, force: true });
      await mkdir(dir, { recursive: true });
      scratches.push(dir);
      return dir;
    },
    async tempDir(name = "tmp") {
      const dir = await mkdtemp(join(tmpdir(), `ardia-${scenario.id}-${name}-`));
      scratches.push(dir);
      return dir;
    },
    async launch({ scratch, label = "ardia", background = true, extensions = false, extraArgs = [], env = {}, ozone, startTimeout, coreDumps } = {}) {
      if (!scratch) throw new Error("launch needs a scratch directory");
      const app = await launch({
        executable: options.executable,
        uiDir: options.uiDir,
        scratch,
        background,
        ozone,
        extensions,
        env,
        label,
        startTimeout,
        coreDumps,
        extraArgs: [...(fixtures?.chromiumArgs ?? []), ...extraArgs],
      });
      apps.add(app);
      return app;
    },
    async stop(app, options) {
      if (!app) return;
      const exited = await app.close(options);
      apps.delete(app);
      return exited;
    },
    async evidence(name, data) {
      await ensureEvidence();
      const file = join(evidenceDir, `${name}.json`);
      await writeFile(file, JSON.stringify(data, null, 2));
      return file;
    },
    async screenshot(target, name) {
      await ensureEvidence();
      return target?.screenshot ? target.screenshot(join(evidenceDir, `${name}.png`)) : null;
    },
    async check(name, body, { timeout = 60000, critical = false } = {}) {
      const started = Date.now();
      try {
        const detail = await withDeadline(Promise.resolve().then(body), timeout, name);
        checks.push({ name, status: "pass", detail: detail ?? null, durationMs: Date.now() - started });
        t.log(`pass ${name}`);
        return { ok: true, detail };
      } catch (error) {
        checks.push({ name, status: "fail", detail: errorText(error), durationMs: Date.now() - started });
        t.log(`FAIL ${name}: ${errorText(error)}`);
        if (critical) throw new ScenarioAbort(`${name}: ${errorText(error)}`);
        return { ok: false, error };
      }
    },
    manual(name, steps, reason = "needs physical input or a desktop session") {
      checks.push({ name, status: "manual", reason, steps });
      t.log(`manual ${name}`);
    },
    async cleanup({ keep = false, failed = false } = {}) {
      for (const app of [...apps]) {
        await app.close().catch(() => {});
        apps.delete(app);
      }
      if (keep || failed) return scratches;
      for (const dir of scratches) await rm(dir, { recursive: true, force: true }).catch(() => {});
      return [];
    },
  };
  return t;
}

export async function runScenario(scenario, { options, fixtures, resultsDir, scratchRoot, launch, log, keep = false, deadline = scenario.deadline ?? 300000 }) {
  const started = Date.now();
  const t = createContext({ scenario, options, fixtures, resultsDir, scratchRoot, launch, log });
  let error = null;
  try {
    await withDeadline(Promise.resolve().then(() => scenario.run(t)), deadline, `${scenario.id} scenario`);
  } catch (caught) {
    error = caught instanceof ScenarioAbort ? caught.message : errorText(caught);
    t.log(`ERROR ${error}`);
  }
  for (const item of scenario.manual ?? []) {
    if (!t.checks.some((c) => c.name === item.name)) t.manual(item.name, item.steps, item.reason);
  }
  const failed = !!error || t.checks.some((c) => c.status === "fail");
  const kept = await t.cleanup({ keep, failed });
  const record = { checks: t.checks, error, durationMs: Date.now() - started, evidence: t.evidenceDir, keptScratch: kept.length ? kept : null };
  return summarizeScenario(scenario, record);
}
