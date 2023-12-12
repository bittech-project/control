import { Job, JobType } from '../IJob';

export type NETDEV_CONF_PARAMS = {
    ip: string;
    target: string;
};

export class NETDEV_CONF extends Job {
    type: JobType = JobType.EXPORT_ADD_NVME;
    params: NETDEV_CONF_PARAMS;

    constructor(environment_key: string, params: NETDEV_CONF_PARAMS) {
        super(environment_key, JobType.NETDEV_CONF);
        this.params = params;
    }
}
