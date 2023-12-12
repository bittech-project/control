import { Job, JobType } from '../IJob';

export type EXPORT_ADD_NVME_PARAMS = {
    ip: string;
    target: string;
};

export class EXPORT_ADD_NVME extends Job {
    type: JobType = JobType.EXPORT_ADD_NVME;
    params: EXPORT_ADD_NVME_PARAMS;

    constructor(environment_key: string, params: EXPORT_ADD_NVME_PARAMS) {
        super(environment_key, JobType.EXPORT_ADD_NVME);
        this.params = params;
    }
}
