import {
    AbstractStorageResource,
    ResourceId,
    ResourceState,
    ResourceType,
    StatePropHealth,
} from './AbstractStorageResource';

export interface AbstractExportProtoParams {
    transport: ExportTransport;
}

export interface AbstractBlockExportProtoParams extends AbstractExportProtoParams {
    qn: string;
}

export const NFS_DEFAULT_NETWORK = '*';

export enum ExportProto {
    nvmeof = 'nvmeof',
    iscsi = 'iscsi',
    nfs = 'nfs',
}

export enum ExportTransport {
    tcp = 'tcp',
    rdma = 'rdma',
}

export interface ExportProtoParamsNvmeof extends AbstractBlockExportProtoParams {
    port: string;
}

export type ExportProtoParamsIscsi = AbstractBlockExportProtoParams;

export interface ExportProtoParamsNfs extends AbstractExportProtoParams {
    allowedNetworks: string;
    mountPath: string;
}

export type ExportProtoParams = ExportProtoParamsNvmeof | ExportProtoParamsIscsi | ExportProtoParamsNfs;

export interface StateExport extends ResourceState {
    clients: string[];
    exportPath: string;
}

export interface ParamsExport {
    proto: ExportProto;
    protoParams: ExportProtoParams;
    relationId: ResourceId;
}

export interface ExportOzrState {
    fsId: string;
    num: number;
    path: string;
    params: string[];
    nodeId: string;
    health: StatePropHealth;
    clients: [];
}

export class Export extends AbstractStorageResource {
    readonly type = ResourceType.Export;
    params: ParamsExport = { proto: null, protoParams: null, relationId: null };
    state: StateExport | ExportOzrState = {
        health: StatePropHealth.UNKNOWN,
        clients: [],
        exportPath: null,
    };
}
