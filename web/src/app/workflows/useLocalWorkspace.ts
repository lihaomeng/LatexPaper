import { useEffect, useRef, useState } from "react";
import type { EditorSession } from "../../features/editor";
import type { WorkspaceStateResponseResult } from "../../../.generated/rpc/protocol";
import type { CacheStatus } from "../useDraftWorkspace";

/** One owner for the local session, revisions and cross-workflow operation guards. */
export function useLocalWorkspace() {
  const [localSession, setLocalSession] = useState<EditorSession | null>(null);
  const localSessionRef = useRef<EditorSession | null>(null);
  const [project, setProject] = useState<WorkspaceStateResponseResult | null>(null);
  const revisions = useRef(new Map<string, string>());
  const [localStatus, setLocalStatus] = useState<CacheStatus>("saved");
  const [localError, setLocalError] = useState("");
  const [workspaceBusy, setWorkspaceBusy] = useState(false);
  const documentOpenSequence = useRef(0);
  const saveInFlight = useRef(false);
  const saveDeferred = useRef(false);
  const conflictRef = useRef<string | null>(null);
  const [conflictFile, setConflictFile] = useState<string | null>(null);
  const refreshInFlight = useRef(false);
  const refreshSequence = useRef(0);
  const refreshDeferred = useRef(false);
  const mutationBusy = useRef(false);
  const switchingWorkspace = useRef(false);
  const [selectedDirectory, setSelectedDirectory] = useState<string | null>(null);

  useEffect(() => () => {
    ++documentOpenSequence.current;
    ++refreshSequence.current;
    saveDeferred.current = false;
    const current = localSessionRef.current;
    localSessionRef.current = null;
    current?.dispose();
  }, []);
  return { localSession, setLocalSession, localSessionRef, project, setProject, revisions, localStatus,
    setLocalStatus, localError, setLocalError, workspaceBusy, setWorkspaceBusy, documentOpenSequence, saveInFlight,
    saveDeferred, conflictRef, conflictFile, setConflictFile, refreshInFlight, refreshSequence, refreshDeferred,
    mutationBusy, switchingWorkspace, selectedDirectory, setSelectedDirectory };
}
export type LocalWorkspace = ReturnType<typeof useLocalWorkspace>;
