import { Inject, Injectable } from '@nestjs/common';
import { ParamsExportAddNfs, ParamsExportRmNfs } from 'src/entities/ExportNfs';
import { ExportNfsCreateDto, ExportNfsDeleteDto } from 'src/API/v1/dto/export-nfs-dto';
import { JOB_CREATED } from 'src/jobs/types/jobs.events';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { RESOURCE_REPOSITORY, ResourceRepository } from './resource.repository';
import { EXPORT_ADD_NFS } from 'src/jobs/types/nfs/EXPORT_ADD_NFS';
import { EXPORT_RM_NFS } from 'src/jobs/types/nfs/EXPORT_RM_NFS';
import { ConfigService } from '@nestjs/config';

@Injectable()
export class NfsService {
    constructor(
        @Inject(RESOURCE_REPOSITORY) private readonly repo: ResourceRepository,
        private readonly emitter: EventEmitter2,
        readonly config: ConfigService
    ) {}
    mount(dto: ExportNfsCreateDto) {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const NfsParams = {
            ip: dto.ip,
            export_path: dto.export_path,
            folder_name: dto.folder_name,
        } as ParamsExportAddNfs;
        const job = new EXPORT_ADD_NFS(env_key, NfsParams);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }

    unmount(dto: ExportNfsDeleteDto) {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const NfsParams = {
            ip: dto.ip,
            export_path: dto.export_path,
            folder_name: dto.folder_name,
        } as ParamsExportRmNfs;
        const job = new EXPORT_RM_NFS(env_key, NfsParams);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }
}
