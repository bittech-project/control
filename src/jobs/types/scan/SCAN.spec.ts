import { JobStatus, JobType } from '../IJob';
import { SCAN } from './SCAN';

test('object with type SCAN should be created', () => {
    const job = new SCAN('testlib1');
    expect(job.type).toBe(JobType.SCAN);
    expect(job.status).toBe(JobStatus.NEW);
    expect(job.id).toMatch('SCAN-');
});
