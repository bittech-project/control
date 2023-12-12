import { Job, JobType } from '../IJob';

export type EXPORT_RM_NFS_PARAMS = {
    ip: string;
    export_path: string;
    folder_name: string;
};

export class EXPORT_RM_NFS extends Job {
    type: JobType = JobType.EXPORT_RM_NFS;
    params: EXPORT_RM_NFS_PARAMS;

    constructor(environment_key: string, params: EXPORT_RM_NFS_PARAMS) {
        super(environment_key, JobType.EXPORT_RM_NFS);
        this.params = params;
    }
}
