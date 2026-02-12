import { execSync } from "node:child_process";
import cron from "node-cron";

const USAGE_URL = "https://api.anthropic.com/api/oauth/usage";
const UPDATE_URL = "http://claude-ticker-px.local/update";

function getAccessToken() {
  const raw = execSync(
    'security find-generic-password -s "Claude Code-credentials" -w',
    { encoding: "utf-8" }
  ).trim();

  const credentials = JSON.parse(raw);
  return credentials.claudeAiOauth.accessToken;
}

async function fetchUsage(token) {
  const res = await fetch(USAGE_URL, {
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
      "anthropic-beta": "oauth-2025-04-20",
    },
  });

  if (!res.ok) {
    throw new Error(`GET ${USAGE_URL} failed: ${res.status} ${await res.text()}`);
  }

  return res.json();
}

async function postUpdate(payload) {
  const res = await fetch(UPDATE_URL, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });

  if (!res.ok) {
    throw new Error(`POST ${UPDATE_URL} failed: ${res.status} ${await res.text()}`);
  }

  return res;
}

async function tick() {
  const token = getAccessToken();
  const usage = await fetchUsage(token);
  //console.log("Usage:", JSON.stringify(usage, null, 2));
  await postUpdate(usage);
  console.log("Update sent to", UPDATE_URL, "at", new Date().toLocaleTimeString());
}

// Run immediately on start, then every 5 minutes between 7:00–16:59
tick().catch(console.error);

cron.schedule("*/5 7-16 * * *", () => {
  tick().catch(console.error);
});

console.log("Scheduler running — every 5 min, 7:00–17:00");
