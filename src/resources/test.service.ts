import { Inject, Injectable } from '@nestjs/common';
import { TEST } from 'src/jobs/types/test/TEST';
import { JOB_CREATED } from 'src/jobs/types/jobs.events';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { RESOURCE_REPOSITORY, ResourceRepository } from './resource.repository';
import { ConfigService } from '@nestjs/config';
import { JobStatus, JobType } from 'src/jobs/types/IJob';
import { DbService } from 'src/db/db.service';

@Injectable()
export class TestService {
    constructor(
        @Inject(RESOURCE_REPOSITORY) private readonly repo: ResourceRepository,
        private readonly emitter: EventEmitter2,
        readonly config: ConfigService,
        private readonly DbService: DbService
    ) {}

    @OnEvent(JobType[JobType.TEST])
    handleExportAddIscsiEvent(job: TEST) {
        if (job.status == JobStatus.OK) {
            this.DbService.save('jobs', JSON.stringify(job.result));
        }
    }

    start() {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new TEST(env_key, { target: 'test' });
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }
}
