import { Job, JobType } from '../IJob';

export type EXPORT_RM_ISCSI_PARAMS = {
    target: string;
};

export class EXPORT_RM_ISCSI extends Job {
    type: JobType = JobType.EXPORT_RM_ISCSI;
    params: EXPORT_RM_ISCSI_PARAMS;

    constructor(environment_key: string, params: EXPORT_RM_ISCSI_PARAMS) {
        super(environment_key, JobType.EXPORT_RM_ISCSI);
        this.params = params;
    }
}
