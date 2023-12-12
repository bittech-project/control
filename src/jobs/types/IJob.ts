import { ResourceId } from 'src/entities/AbstractStorageResource';
import { JobResult } from './scan/JobResult';
import { IError } from 'src/entities/IError';

export const DEFAULT_JOB_TTL_MS: number = 60 * 1000;

export enum JobType {
    UNKNOWN = 'UNKNOWN',
    SCAN = 'SCAN',

    EXPORT_ADD_NVME = 'EXPORT_ADD_NVME',
    EXPORT_RM_NVME = 'EXPORT_RM_NVME',
    EXPORT_ADD_SCST = 'EXPORT_ADD_SCST',
    EXPORT_RM_SCST = 'EXPORT_RM_SCST',
    EXPORT_ADD_NFS = 'EXPORT_ADD_NFS',
    EXPORT_RM_NFS = 'EXPORT_RM_NFS',
    EXPORT_ADD_ISCSI = 'EXPORT_ADD_ISCSI',
    TEST = 'TEST',
    EXPORT_RM_ISCSI = 'EXPORT_RM_ISCSI',
    FS = 'FS',
    NETDEV_CONF = 'NETDEV_CONF',
}

export enum JobStatus {
    OK = 'OK',
    FAILED = 'FAILED',
    PROCESSING = 'PROCESSING',
    NEW = 'NEW',
}

export enum JobRole {
    load = 'load',
    target = 'target',
    control = 'control',
}

export interface IJob {
    id: string;
    environment_key: string;
    role: JobRole;
    // nodeId: number;
    type: JobType;
    status: JobStatus;
    created: number;
    ttl: number;
    params: object;
    tgt?: ResourceId[];
    result: JobResult;
}

export abstract class Job implements IJob {
    id: string;
    environment_key: string;
    role: JobRole;
    // nodeId: number;
    type: JobType;
    status: JobStatus = JobStatus.NEW;
    created: number = Date.now();
    ttl: number = DEFAULT_JOB_TTL_MS;
    params: object = {};
    tgt?: ResourceId[];
    result: JobResult = { environment_key: '', errors: [] };

    /**
     * copy properties from previously created IJob (it may be DAO or DTO received from Agent)
     * @param j
     */
    copyProps(j: IJob) {
        this.id = j.id;
        this.environment_key = j.environment_key;
        this.role = JobRole[j.role];
        this.created = j.created;
        this.status = JobStatus[j.status];
        this.result = j.result;
    }

    expired(): boolean {
        return this.created + this.ttl < Date.now();
    }
    done(): boolean {
        return [JobStatus.OK, JobStatus.FAILED].includes(this.status);
    }
    ok(): boolean {
        return [JobStatus.OK].includes(this.status);
    }
    constructor(environment_key: string, t: JobType) {
        this.environment_key = environment_key;
        this.type = t;
        this.id = Job.buildId(this.type, this.created);
    }

    addError(e: IError) {
        this.result.errors.push(e);
    }

    // /**
    //  * this method MUST be overriden in subclasses!
    //  * @param obj
    //  */
    // static fromJson(obj: IJob): Job {
    //     throw new Error("not implemented");
    // }

    static buildId(t: JobType, created: number) {
        return `${t}-${created}`;
    }

    toString(): string {
        return `<Job ${this.id}: ${this.status}>`;
    }
}
