import { Inject, Injectable, Logger } from '@nestjs/common';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { Cron } from '@nestjs/schedule';
import { JOB_REPOSITORY, JobRepository, toDomain } from './job.repository';
import { IJob, Job, JobStatus, JobType } from './types/IJob';
import { JOB_CREATED, JOB_DONE, JOB_EXPIRED, JOB_NEW, JOB_RETURNED_FROM_AGENT } from './types/jobs.events';
import { ConfigService } from '@nestjs/config';

@Injectable()
export class JobService {
    private readonly logger: Logger = new Logger(JobService.name);

    constructor(
        readonly config: ConfigService,
        @Inject(JOB_REPOSITORY) private readonly repo: JobRepository,
        private readonly emitter: EventEmitter2
    ) {}

    /**
     * handle ijob returned from agent. The agent changes `ijob.status` and sets `ijob.result`
     * @param ijob
     * @returns
     */

    @OnEvent(JOB_RETURNED_FROM_AGENT)
    async handleReturned(ijob: IJob): Promise<void> {
        const job = toDomain(ijob);
        const first_path_env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY').split('-')[0];
        if (job.environment_key == undefined) {
            return;
        } else if (!job.environment_key.startsWith(first_path_env_key)) {
            return;
        }
        await this.repo.save(job);
        if (!job.done()) return;
        if (job.status == JobStatus.FAILED)
            this.logger.error(`${job.toString()}failed!: [ ${job.result.errors.map((e) => e.message).join('|')} ]`);
        this.emitter.emit(JOB_DONE, job);
        this.emitter.emit(JobType[job.type], job);
        return;
    }

    @Cron('*/5 * * * * *')
    async triggerRun() {
        const commands = await this.repo.getArray();
        // await this.unlockNode(commands);
        const expired = commands.filter((c) => !c.done() && c.expired());
        for (const c of expired) {
            this.expire(c);
            await this.repo.save(c);
        }
    }

    private expire(job: Job) {
        // TODO: ask the agent about the current status
        this.logger.warn(`Command ${job.id}, EXPIRED!`);
        job.status = JobStatus.FAILED;
        job.addError({ error: 'JobExpired', message: `Job ${job.id} expired at ${Date.now()}` });
        this.emitter.emit(JOB_EXPIRED, job);
    }

    @OnEvent(JOB_CREATED)
    private async handle_JOB_CREATED(job: Job) {
        await this.repo.save(job);
        this.emitter.emit(JOB_NEW, job);
    }
}
