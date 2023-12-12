import isEqual from 'lodash.isequal';
import { IError } from './IError';

export type ResourceId = string;
export type ResourceEnvironmentKey = string;

export enum ResourceType {
    Export = 'Export',
    Network = 'Network',
}

export enum ResourceStatus {
    NEW = 'NEW',
    CREATING = 'CREATING',
    OK = 'OK',
    UNKNOWN = 'UNKNOWN',
    REMOVING = 'REMOVING',
}

export enum StatePropHealth {
    UNKNOWN = 'UNKNOWN',
    OK = 'OK',
    DEGRADED = 'DEGRADED',
    FAILED = 'FAILED',
    LOST = 'LOST',
}

export interface ResourceState {
    health: StatePropHealth;
}

export abstract class AbstractStorageResource {
    readonly id: ResourceId;
    readonly environment_key: ResourceEnvironmentKey;
    name = '';
    readonly type: ResourceType;
    status: ResourceStatus;
    params: object;
    created = 0;
    // nodes: Set<string> = new Set<string>();
    // following params are not stored in DB
    updated: number;
    state: ResourceState;
    errors: IError[] = [];
    locked = false;

    constructor(
        id: ResourceId,
        environment_key: ResourceEnvironmentKey,
        name: string,
        params: object,
        // nodes?: Set<string>,
        status?: ResourceStatus,
        created?: number
    ) {
        this.id = id;
        this.environment_key = environment_key;
        this.name = name;
        this.params = params;
        // this.nodes = nodes || new Set<string>(['1']);
        this.status = status || ResourceStatus.NEW;
        this.created = created || Date.now();
        this.updated = Date.now();
    }

    refresh() {
        this.updated = Date.now();
    }

    unlock() {
        this.locked = false;
    }

    setState(s: ResourceState) {
        if (isEqual(s, this.state)) return;
        this.state = s;

        this.refresh();
    }

    /**
     * In case resource existed previously, but does not appear on current scan, it MUST be marked as LOST.
     */
    markAsLost() {
        // ignore resource in case it's being modified;
        if (this.locked) return;
        this.state.health = StatePropHealth.LOST;
        this.refresh();
    }
}
