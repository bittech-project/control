import { Injectable } from '@nestjs/common';
import { ExportNvmeCreateDto, ExportNvmeDeleteDto } from 'src/API/v1/dto/export-nvme.dto';
import { JOB_CREATED } from 'src/jobs/types/jobs.events';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { EXPORT_ADD_NVME } from 'src/jobs/types/nvme/EXPORT_ADD_NVME';
import { EXPORT_RM_NVME } from 'src/jobs/types/nvme/EXPORT_RM_NVME';
import { ConfigService } from '@nestjs/config';
import { Job, JobStatus, JobType } from 'src/jobs/types/IJob';
import { TestService } from 'src/resources/test.service';

@Injectable()
export class NvmeofService {
    constructor(
        private readonly testService: TestService,
        private readonly emitter: EventEmitter2,
        readonly config: ConfigService
    ) {}

    @OnEvent(JobType[JobType.EXPORT_ADD_NVME])
    handleExportAddIscsiEvent(job: Job) {
        if (job.status == JobStatus.OK) {
            this.testService.start();
        }
    }
    connect(dto: ExportNvmeCreateDto) {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new EXPORT_ADD_NVME(env_key, dto);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }

    disconnect(dto: ExportNvmeDeleteDto) {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new EXPORT_RM_NVME(env_key, dto);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }
}
