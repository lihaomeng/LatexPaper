import { AuthoringRpcClient, ExportRpcClient, createNativeConnection, createSystemConnection,
  PreferencesSessionRpcClient, WorkspaceRpcClient } from '../native-api';

/** Composition only: platform transports never leak into feature components. */
export function createNativeServices(allowFake: boolean) {
  const systemConnection = createSystemConnection(allowFake);
  return {
    connection: createNativeConnection(allowFake),
    systemConnection,
    workspaceConnection: new WorkspaceRpcClient(systemConnection),
    workspaceOpenConnection: new WorkspaceRpcClient(createSystemConnection(allowFake, 305000)),
    authoringConnection: new AuthoringRpcClient(createSystemConnection(allowFake, 365000)),
    exportConnection: new ExportRpcClient(createSystemConnection(allowFake, 305000)),
    preferencesSessionConnection: new PreferencesSessionRpcClient(createSystemConnection(allowFake)),
  };
}
