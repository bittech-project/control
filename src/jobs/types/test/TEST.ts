import { Job, JobType } from '../IJob';

export type TEST_PARAMS = {
    target: string;
};

export class TEST extends Job {
    type: JobType = JobType.TEST;
    params: TEST_PARAMS;

    constructor(environment_key: string, params: TEST_PARAMS) {
        super(environment_key, JobType.TEST);
        this.params = params;
    }
}
