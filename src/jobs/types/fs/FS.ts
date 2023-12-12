import { Job, JobType } from '../IJob';

export class FS extends Job {
    type: JobType = JobType.FS;

    constructor(environment_key: string) {
        super(environment_key, JobType.FS);
    }
}
