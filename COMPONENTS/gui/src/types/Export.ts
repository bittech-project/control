import type { BaseResource, StatePropHealth } from '@/types/BaseResource';

export enum ParamsExportProto {
    iscsi = 'iscsi',
    nvmeof = 'nvmeof',
    nfs = 'nfs',
}

export enum Transport {
    tcp = 'tcp',
    rdma = 'rdma',
}

export interface ExportProtoParams {
    trnsport: Transport;
}

export interface ParamsExport {
    environment: string;
    relationId: string;
    proto: ParamsExportProto;
    exportPath: string;
    protoParams: ExportProtoParams;
}

export interface StateExport {
    health: StatePropHealth;
    clients: string[];
}

export interface Export extends BaseResource {
    params: ParamsExport;
    state: StateExport;
}
