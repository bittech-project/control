import { plainToClass } from 'class-transformer';
import { IJob, Job, JobType } from './types/IJob';
import { SCAN } from './types/scan/SCAN';
import { EXPORT_ADD_ISCSI } from './types/iscsi/EXPORT_ADD_ISCSI';
import { EXPORT_RM_ISCSI } from './types/iscsi/EXPORT_RM_ISCSI';
import { EXPORT_ADD_NVME } from './types/nvme/EXPORT_ADD_NVME';
import { EXPORT_RM_NVME } from './types/nvme/EXPORT_RM_NVME';
import { EXPORT_ADD_NFS } from './types/nfs/EXPORT_ADD_NFS';
import { EXPORT_RM_NFS } from './types/nfs/EXPORT_RM_NFS';
import { FS } from './types/fs/FS';
import { TEST } from './types/test/TEST';

export const JOB_REPOSITORY = 'JOB_REPOSITORY';

export class JobRepository {
    private jobs_: Map<string, Job> = new Map();
    private jobsDone_: Map<string, Job> = new Map();

    async save(job: Job): Promise<void> {
        if (job.done()) {
            this.jobsDone_.set(job.id, job);
            this.jobs_.delete(job.id);
            return;
        }
        this.jobs_.set(job.id, job);
    }

    async getArray(): Promise<Job[]> {
        return [...this.jobs_.values()];
    }

    async find(jobId: string): Promise<Job | null> {
        return this.jobs_.get(jobId);
    }
}

export function toDomain(dao: IJob): Job {
    const jobType: JobType = JobType[dao.type];
    switch (jobType) {
        case JobType.EXPORT_ADD_NVME: {
            return plainToClass(EXPORT_ADD_NVME, dao);
        }
        case JobType.EXPORT_RM_NVME: {
            return plainToClass(EXPORT_RM_NVME, dao);
        }
        case JobType.EXPORT_ADD_NFS: {
            return plainToClass(EXPORT_ADD_NFS, dao);
        }
        case JobType.EXPORT_RM_NFS: {
            return plainToClass(EXPORT_RM_NFS, dao);
        }
        case JobType.EXPORT_ADD_ISCSI: {
            return plainToClass(EXPORT_ADD_ISCSI, dao);
        }
        case JobType.TEST: {
            return plainToClass(TEST, dao);
        }
        case JobType.EXPORT_RM_ISCSI: {
            return plainToClass(EXPORT_RM_ISCSI, dao);
        }
        case JobType.FS: {
            return plainToClass(FS, dao);
        }
        case JobType.SCAN: {
            return plainToClass(SCAN, dao);
        }
        default: {
            throw new Error(`job ${dao.id} unknown JobType ${jobType}!`);
        }
    }
}

export const jobRepositoryFactory = {
    provide: JOB_REPOSITORY,
    useFactory: async () => {
        return new JobRepository();
    },
};
