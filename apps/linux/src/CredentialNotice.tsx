import { useEffect, useState } from "react";
import { Group, Row } from "../../browser/src/components/settings/Form";
import { ardiaHost } from "./host";
import { autofillStatus, credentialNotice, passwordStatus } from "./hostCredentials";

export function CredentialNotice({ kind }: { kind: "passwords" | "autofill" }) {
  const [notice, setNotice] = useState<string | null>(null);
  useEffect(() => {
    if (!ardiaHost) return;
    let live = true;
    const status = kind === "passwords" ? passwordStatus(ardiaHost) : autofillStatus(ardiaHost);
    status.then(
      (value) => live && setNotice(credentialNotice(value, kind)),
      (error: unknown) => live && setNotice(error instanceof Error ? error.message : "Saved data is unavailable on this computer."),
    );
    return () => {
      live = false;
    };
  }, [kind]);
  if (!notice) return null;
  return (
    <Group>
      <Row title={kind === "passwords" ? "Passwords are unavailable" : "Some autofill data is unavailable"} description={notice} />
    </Group>
  );
}
