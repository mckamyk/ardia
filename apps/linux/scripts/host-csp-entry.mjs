import "../src/main.tsx";
import { createElement } from "react";
import { createRoot } from "react-dom/client";
import { Symbol } from "../src/shims/web/Symbol.tsx";
import { SYMBOL_NAMES } from "../src/shims/web/symbols/data.ts";

const hostileColor = '\"><img src=x onerror="window.__symbolInjection=1"><svg onload="window.__symbolInjection=2" data-x="&\'';
const gallery = document.getElementById("symbol-fixture");
createRoot(gallery).render(createElement("div", null,
  SYMBOL_NAMES.map((name) => createElement(Symbol, { key: name, name, size: 14, color: "#334455", style: { width: 20, height: 20 } })),
  createElement("div", { id: "hostile-symbol" }, createElement(Symbol, { name: SYMBOL_NAMES[0], color: hostileColor })),
));

const deadline = Date.now() + 10000;
const verify = () => {
  const svgCount = gallery.querySelectorAll("svg").length;
  const uiCount = document.querySelectorAll("#root [data-symbol] svg").length;
  if ((svgCount !== SYMBOL_NAMES.length + 1 || !uiCount) && Date.now() < deadline) return setTimeout(verify, 20);
  const errors = [...window.__ardiaCspFixture.errors];
  const check = (condition, message) => { if (!condition) errors.push(message); };
  check(!!globalThis.trustedTypes, "Trusted Types API unavailable");
  check(svgCount === SYMBOL_NAMES.length + 1, `Rendered ${svgCount} SVGs for ${SYMBOL_NAMES.length + 1} expected symbols`);
  check(uiCount > 0 && document.querySelector(".ardia-desktop.host"), "Host UI failed to initialize");
  check(!gallery.querySelector("script,img,foreignObject,iframe,object,embed"), "Injected non-geometry element");
  check(![...gallery.querySelectorAll("*")].some((element) => [...element.attributes].some((attribute) => /^on/i.test(attribute.name))), "Injected event attribute");
  check(document.querySelector("#hostile-symbol svg")?.getAttribute("color") === hostileColor, "Color escaped incorrectly");
  check(!window.__symbolInjection, "Injected script executed");
  check(window.__ardiaCspFixture.htmlAssignments.length >= svgCount, "Missing symbol HTML assignments");
  check(window.__ardiaCspFixture.htmlAssignments.every(Boolean), "Host assigned raw HTML to a Trusted Types sink");
  let rawHtmlBlocked = false;
  let unknownPolicyBlocked = false;
  let duplicatePolicyBlocked = false;
  let stringEvaluationBlocked = false;
  try { document.createElement("div").innerHTML = "<b>blocked</b>"; } catch { rawHtmlBlocked = true; }
  try { trustedTypes.createPolicy("unapproved", { createHTML: (value) => value }); } catch { unknownPolicyBlocked = true; }
  try { trustedTypes.createPolicy("ardia-symbols", { createHTML: (value) => value }); } catch { duplicatePolicyBlocked = true; }
  try { new Function("return 1")(); } catch { stringEvaluationBlocked = true; }
  check(rawHtmlBlocked, "Raw HTML sink is not protected");
  check(unknownPolicyBlocked, "CSP allowed another Trusted Types policy");
  check(duplicatePolicyBlocked, "CSP allowed duplicate symbol policies");
  check(stringEvaluationBlocked, "CSP allowed string evaluation");
  window.__ardiaCspResult = {
    passed: !errors.length, errors, symbols: SYMBOL_NAMES.length, svgCount, uiCount,
    trustedAssignments: window.__ardiaCspFixture.htmlAssignments.filter(Boolean).length,
    rawHtmlBlocked, unknownPolicyBlocked, duplicatePolicyBlocked, stringEvaluationBlocked,
  };
};
setTimeout(verify, 100);
