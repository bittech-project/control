import { Job, JobType } from '../IJob';

export type EXPORT_ADD_NFS_PARAMS = {
    ip: string;
    export_path: string;
    folder_name: string;
};

export class EXPORT_ADD_NFS extends Job {
    type: JobType = JobType.EXPORT_ADD_NFS;
    params: EXPORT_ADD_NFS_PARAMS;

    constructor(environment_key: string, params: EXPORT_ADD_NFS_PARAMS) {
        super(environment_key, JobType.EXPORT_ADD_NFS);
        this.params = params;
    }
}
