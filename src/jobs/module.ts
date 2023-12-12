import { Module } from '@nestjs/common';
import { jobRepositoryFactory } from './job.repository';
import { JobService } from './job.service';

@Module({
    providers: [jobRepositoryFactory, JobService],
    exports: [jobRepositoryFactory, JobService],
    controllers: [],
})
export class JobModule {}
