import { Module } from '@nestjs/common';
import { ResourceModule } from 'src/resources/resource.module';
import { JobModule } from 'src/jobs/module';
import { NvmeController } from './nvme.controller';
import { IscsiController } from './iscsi.controller';
import { FsController } from './fs.controller';
import { ResourceController } from './resource.controller';
import { NfsController } from './nfs.controller';

@Module({
    imports: [ResourceModule, JobModule],
    controllers: [IscsiController, NvmeController, NfsController, FsController, ResourceController],
})
export class ApiModule {}
