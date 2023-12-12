import { Module } from '@nestjs/common';
import { resourceRepositoryFactory } from './resource.repository';
import { ResourceService } from './resource.service';
import { ResourceReadyConstraint } from './validation/resource-ready.rule';
import { ResourceIsTypeConstraint } from './validation/resource-is-type.rule';
import { NameAlreadyExistsValidator } from './validation/name-already-exists.rule';
import { IscsiService } from './iscsi.service';
import { NvmeofService } from './nvme.service';
import { NfsService } from './nfs.service';
import { TestService } from './test.service';
import { DbModule } from 'src/db/db.module';

@Module({
    imports: [DbModule],
    providers: [
        resourceRepositoryFactory,
        ResourceReadyConstraint,
        ResourceIsTypeConstraint,
        ResourceService,
        NameAlreadyExistsValidator,
        IscsiService,
        NfsService,
        NvmeofService,
        TestService,
    ],
    exports: [resourceRepositoryFactory, IscsiService, NvmeofService, NfsService, TestService],
})
export class ResourceModule {}
