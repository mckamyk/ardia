import { useEffect, useSyncExternalStore } from "react";
import { View } from "react-native";
import { Button, TextField } from "../../browser/src/components/ui";
import { ErrorText, Handoff, Spacer, Title } from "../../browser/src/components/import/Parts";
import { importLayout } from "../../browser/src/components/import/tokens";
import type { ImportFlow } from "../../browser/src/components/import/useImportFlow";
import { asksPrimaryPassword, createSecretSlot, unlockCopy, unlockSource, withoutEncrypted } from "./importUnlock";

const primaryPassword = createSecretSlot();

function useTypedSecret() {
  return useSyncExternalStore(primaryPassword.subscribe, primaryPassword.get);
}

async function unlock(flow: ImportFlow) {
  const { api, source, sourceProfiles } = flow;
  if (!api || !source || flow.unlocking) return;
  if (asksPrimaryPassword(source, sourceProfiles) && !primaryPassword.get()) return;
  flow.setError(null);
  flow.setUnlocking(true);
  const failure = await unlockSource(api, source, sourceProfiles, flow.kinds, primaryPassword);
  flow.setUnlocking(false);
  if (failure) return flow.setError(failure);
  void flow.run(flow.kinds);
}

export function UnlockBody({ flow }: { flow: ImportFlow }) {
  const { source, sourceProfiles, kinds, error } = flow;
  const typed = useTypedSecret();
  useEffect(() => () => primaryPassword.clear(), []);
  if (!source) return null;
  const copy = unlockCopy(source, sourceProfiles, kinds);
  return (
    <>
      <Title title={copy.title} subtitle={copy.subtitle} />
      <Handoff iconPath={source.iconPath} symbol="lock.fill" maxWidth={importLayout.textMax} lead={copy.lead} detail={copy.detail} />
      {copy.field && (
        <View style={{ alignSelf: "center", width: 260, marginTop: 16 }}>
          <TextField
            value={typed}
            onChangeText={primaryPassword.set}
            placeholder="Primary password"
            secure
            autoFocus
            onSubmit={() => void unlock(flow)}
          />
        </View>
      )}
      {error && <ErrorText text={error} />}
    </>
  );
}

export function UnlockFooter({ flow }: { flow: ImportFlow }) {
  const { source, bySpaces, kinds, unlocking, sourceProfiles } = flow;
  const typed = useTypedSecret();
  if (!source) return null;
  const copy = unlockCopy(source, sourceProfiles, kinds);
  return (
    <>
      <Button title="Back" onPress={() => flow.setStep(source.profiles.length > 1 || bySpaces ? "profiles" : "choose")} />
      <Spacer />
      <Button
        title="Skip Encrypted Data"
        onPress={() => {
          primaryPassword.clear();
          const without = withoutEncrypted(source, kinds);
          flow.setKinds(without);
          void flow.run(without, true);
        }}
      />
      <Button
        title={unlocking ? copy.waiting : "Continue"}
        kind="primary"
        disabled={unlocking || (copy.field && !typed)}
        onPress={() => void unlock(flow)}
      />
    </>
  );
}
