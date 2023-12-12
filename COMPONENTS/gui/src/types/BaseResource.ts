export type ResourceId = string;

export enum AccessType {
    Block = 'Block',
    File = 'File',
}

export enum StatePropHealth {
    UNKNOWN = 'UNKNOWN',
    OK = 'OK',
    DEGRADED = 'DEGRADED',
    FAILED = 'FAILED',
    LOST = 'LOST',
}

export interface BaseResource {
    id: ResourceId;
    name: string;
    type: string;
    params: object;
    status?: string;
    state: object;
    created: number;
    updated: number;
    locked: boolean;
    errors?: { error: string; message: string }[];
}
