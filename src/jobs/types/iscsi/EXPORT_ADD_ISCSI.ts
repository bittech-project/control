import { Job, JobType } from '../IJob';

export type EXPORT_ADD_ISCSI_PARAMS = {
    ip: string;
    target: string;
};

export class EXPORT_ADD_ISCSI extends Job {
    type: JobType = JobType.EXPORT_ADD_ISCSI;
    params: EXPORT_ADD_ISCSI_PARAMS;

    constructor(environment_key: string, params: EXPORT_ADD_ISCSI_PARAMS) {
        super(environment_key, JobType.EXPORT_ADD_ISCSI);
        this.params = params;
    }
}
