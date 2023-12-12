import { Job, JobType } from '../IJob';

export type EXPORT_RM_NVME_PARAMS = {
    target: string;
};

export class EXPORT_RM_NVME extends Job {
    type: JobType = JobType.EXPORT_RM_NVME;
    params: EXPORT_RM_NVME_PARAMS;

    constructor(environment_key: string, params: EXPORT_RM_NVME_PARAMS) {
        super(environment_key, JobType.EXPORT_RM_NVME);
        this.params = params;
    }
}
