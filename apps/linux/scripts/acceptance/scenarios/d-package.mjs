import { execFile } from "node:child_process";
import { access, chmod, copyFile, mkdir, readFile, symlink, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const run = promisify(execFile);
const ACTION = "dev.ardia.linux.reauthenticate";
const POLICY = "usr/share/polkit-1/actions/dev.ardia.linux.policy";
const PKGBUILD = fileURLToPath(new URL("../../../../../scripts/linux/arch/PKGBUILD", import.meta.url));
const POLKITD = "/usr/lib/polkit-1/polkitd";

const exists = (path) => access(path).then(() => true, () => false);

async function which(name) {
  return run("sh", ["-c", `command -v ${name}`]).then(({ stdout }) => stdout.trim() || null, () => null);
}

async function packageRoot(executable) {
  for (const candidate of [resolve(dirname(executable), ".."), resolve(dirname(executable), "../..")]) {
    if (await exists(join(candidate, "install.sh"))) return candidate;
  }
  return null;
}

async function subordinateIds() {
  const user = (await run("id", ["-un"])).stdout.trim();
  const line = async (file) => (await readFile(file, "utf8").catch(() => "")).split("\n").find((l) => l.startsWith(`${user}:`));
  const uid = await line("/etc/subuid");
  const gid = await line("/etc/subgid");
  if (!uid || !gid) return null;
  const [, uStart, uCount] = uid.split(":");
  const [, gStart, gCount] = gid.split(":");
  return { users: `1:${uStart}:${Math.min(Number(uCount), 65535)}`, groups: `1:${gStart}:${Math.min(Number(gCount), 65535)}` };
}

export function polkitBusConfig(socket, services) {
  return `<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>system</type>
  <listen>unix:path=${socket}</listen>
  <auth>EXTERNAL</auth>
  <servicedir>${services}</servicedir>
  <policy context="default">
    <allow user="*"/>
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
`;
}

export function privatePolkitScript(dir) {
  return `mount --bind '${dir}/actions' /usr/share/polkit-1/actions || exit 3
dbus-daemon --nofork --nopidfile --config-file='${dir}/bus.conf' > '${dir}/bus.log' 2>&1 < /dev/null & bus=$!
sleep 0.5
export DBUS_SYSTEM_BUS_ADDRESS='unix:path=${dir}/system-bus'
${POLKITD} --no-debug > '${dir}/polkitd.log' 2>&1 < /dev/null & polkitd=$!
status=1
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  sleep 0.3
  if pkaction --action-id ${ACTION} --verbose > '${dir}/pkaction.out' 2>&1; then status=0; break; fi
done
pkaction > '${dir}/pkaction-all.out' 2>&1 || true
kill $polkitd $bus 2>/dev/null
wait 2>/dev/null
exit $status
`;
}

export default {
  id: "S105P",
  issue: 105,
  desktop: true,
  title: "The Arch package built with makepkg into a scratch folder (no install) ships the polkit action where polkit reads it, and a private polkitd started without root reads it from the package",
  deadline: 420000,
  async run(t) {
    const tools = { makepkg: await which("makepkg"), fakeroot: await which("fakeroot"), bsdtar: await which("bsdtar"), unshare: await which("unshare"), pkaction: await which("pkaction"), dbusDaemon: await which("dbus-daemon"), polkitd: (await exists(POLKITD)) ? POLKITD : null };
    const root = await packageRoot(t.options.executable);
    const missing = Object.entries(tools).filter(([, path]) => !path).map(([name]) => name);
    if (missing.length || !root) {
      t.manual("The installed package registers the action", ["Build scripts/linux/arch/PKGBUILD with makepkg and install it; pkaction lists the action with implicit active auth_self."], missing.length ? `${missing.join(", ")} not found` : `no install.sh beside ${t.options.executable}`);
      return;
    }
    const build = await t.scratch("makepkg");
    let pkgdir;
    await t.check("makepkg builds ardia-bin from scripts/linux/arch/PKGBUILD into a scratch folder", async () => {
      await copyFile(PKGBUILD, join(build, "PKGBUILD"));
      await mkdir(join(build, "src"), { recursive: true });
      await symlink(root, join(build, "src", "ardia"));
      const { stdout, stderr } = await run(tools.makepkg, ["--noextract", "--nodeps", "--force", "--noconfirm", "--skipinteg"], {
        cwd: build,
        timeout: 360000,
        maxBuffer: 16 * 1024 * 1024,
        env: { ...process.env, BUILDDIR: build, SRCDEST: build, PKGDEST: join(build, "out"), PKGEXT: ".pkg.tar", MAKEFLAGS: "" },
      });
      await t.evidence("makepkg", { log: `${stdout}\n${stderr}`.split("\n").slice(-40) });
      pkgdir = join(build, "pkg", "ardia-bin");
      if (!(await exists(join(build, "out", "ardia-bin-0.0.0-1-x86_64.pkg.tar")))) throw new Error(`no package in ${join(build, "out")}`);
      return { finished: /Finished making/.test(stdout + stderr) };
    }, { timeout: 400000, critical: true });
    await t.check("the package ships the policy at /usr/share/polkit-1/actions, 0644 root:root, byte for byte the source's, with a /usr launcher and a polkit dependency", async () => {
      const archive = join(build, "out", "ardia-bin-0.0.0-1-x86_64.pkg.tar");
      const listing = (await run(tools.bsdtar, ["-tvf", archive], { maxBuffer: 64 * 1024 * 1024 })).stdout.split("\n");
      const policyRow = listing.find((l) => l.endsWith(` ${POLICY}`));
      if (!policyRow || !/^-rw-r--r--\s+0 root\s+root\s/.test(policyRow)) throw new Error(`policy row ${JSON.stringify(policyRow)}`);
      const stray = listing.filter((l) => / usr\/share\/polkit-1\/./.test(l) && !/ usr\/share\/polkit-1\/actions\/(dev\.ardia\.linux\.policy)?$/.test(l));
      if (stray.length) throw new Error(`other polkit files: ${stray.join("; ")}`);
      const packaged = await readFile(join(pkgdir, POLICY), "utf8");
      const source = await readFile(join(root, "share/polkit-1/actions/dev.ardia.linux.policy"), "utf8");
      if (packaged !== source || !packaged.includes(`<action id="${ACTION}">`)) throw new Error("the packaged policy differs from the source");
      const launcher = await readFile(join(pkgdir, "usr/bin/ardia"), "utf8");
      if (!launcher.includes("/usr/lib/ardia/chrome")) throw new Error(`launcher ${launcher}`);
      const info = (await run(tools.bsdtar, ["-xOf", archive, ".PKGINFO"])).stdout;
      if (!/^depend = polkit$/m.test(info)) throw new Error("no polkit dependency in .PKGINFO");
      await t.evidence("package-listing", { policy: policyRow, files: listing.length });
      return { policy: policyRow.replace(/\s+/g, " "), files: listing.length };
    });
    await t.check("a private polkitd in a user namespace, with the package's actions folder mounted over /usr/share/polkit-1/actions, lists the action with implicit active auth_self", async () => {
      const ids = await subordinateIds();
      if (!ids) throw new Error("no /etc/subuid and /etc/subgid range for this user, which polkitd needs to drop to its own user without root");
      const dir = await t.tempDir("polkit");
      await chmod(dir, 0o755);
      await mkdir(join(dir, "actions"), { recursive: true });
      await mkdir(join(dir, "services"), { recursive: true });
      await copyFile(join(pkgdir, POLICY), join(dir, "actions", "dev.ardia.linux.policy"));
      await chmod(join(dir, "actions"), 0o755);
      await chmod(join(dir, "actions", "dev.ardia.linux.policy"), 0o644);
      await writeFile(join(dir, "bus.conf"), polkitBusConfig(join(dir, "system-bus"), join(dir, "services")));
      await writeFile(join(dir, "inner.sh"), privatePolkitScript(dir));
      await run(tools.unshare, ["--map-root-user", `--map-users=${ids.users}`, `--map-groups=${ids.groups}`, "--setgroups", "allow", "--mount", "sh", join(dir, "inner.sh")], { timeout: 60000 }).catch(async (error) => {
        throw new Error(`${error.message.split("\n")[0]}; polkitd: ${(await readFile(join(dir, "polkitd.log"), "utf8").catch(() => "")).slice(-400)}; pkaction: ${(await readFile(join(dir, "pkaction.out"), "utf8").catch(() => "")).slice(-300)}`);
      });
      const out = await readFile(join(dir, "pkaction.out"), "utf8");
      await t.evidence("pkaction", { out });
      if (!out.startsWith(`${ACTION}:`) || !/implicit active:\s*auth_self\b/.test(out) || !/implicit any:\s*no\b/.test(out) || !/implicit inactive:\s*no\b/.test(out)) throw new Error(out);
      return { implicit: out.split("\n").filter((l) => l.includes("implicit")).map((l) => l.trim()) };
    });
  },
};
