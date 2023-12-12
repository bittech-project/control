import { Job, JobType } from '../IJob';
import { JobResult } from './JobResult';

export class SCAN extends Job {
    environment_key: string;
    type: JobType = JobType.SCAN;
    result: JobResult;
    constructor(environment_key: string) {
        super(environment_key, JobType.SCAN);
    }
}
