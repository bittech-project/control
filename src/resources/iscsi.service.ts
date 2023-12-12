import { Injectable } from '@nestjs/common';
import { EXPORT_ADD_ISCSI } from 'src/jobs/types/iscsi/EXPORT_ADD_ISCSI';
import { ExportIscsiCreateDto, ExportIscsiDeleteDto } from 'src/API/v1/dto/export-create-iscsi.dto';
import { JOB_CREATED } from 'src/jobs/types/jobs.events';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { EXPORT_RM_ISCSI } from 'src/jobs/types/iscsi/EXPORT_RM_ISCSI';
import { ConfigService } from '@nestjs/config';
import { TestService } from 'src/resources/test.service';
import { Job, JobStatus, JobType } from 'src/jobs/types/IJob';

@Injectable()
export class IscsiService {
    constructor(
        private readonly testService: TestService,
        private readonly emitter: EventEmitter2,
        readonly config: ConfigService
    ) {}

    @OnEvent(JobType[JobType.EXPORT_ADD_ISCSI])
    handleExportAddIscsiEvent(job: Job) {
        if (job.status == JobStatus.OK) {
            this.testService.start();
        }
    }

    connect(dto: ExportIscsiCreateDto) {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new EXPORT_ADD_ISCSI(env_key, dto);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }

    disconnect(dto: ExportIscsiDeleteDto) {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new EXPORT_RM_ISCSI(env_key, dto);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }
}
