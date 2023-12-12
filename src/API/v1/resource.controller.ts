import { Controller, Get, HttpException, HttpStatus, Inject, Logger, Param } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import { ResourceId, ResourceStatus } from 'src/entities/AbstractStorageResource';
import { IError } from 'src/entities/IError';
import { StorageResource } from 'src/entities/StorageResource';
import { RESOURCE_REPOSITORY, ResourceRepository } from 'src/resources/resource.repository';

type StorageResourceDto = {
    id: ResourceId;
    environment: string;
    type: string;
    name: string;
    nodes: string[];
    params: object;
    status: string;
    state: object;
    created: number;
    updated: number;
    locked: boolean;
    errors: IError[];
};

function toDto(r: StorageResource): StorageResourceDto {
    return <StorageResourceDto>{
        id: r.id,
        environment: r.environment_key,
        type: r.type,
        name: r.name,
        status: ResourceStatus[r.status],
        params: r.params,
        state: r.state,
        created: r.created,
        updated: r.updated,
        locked: r.locked,
        errors: r.errors,
    };
}

@ApiTags('Resources')
@Controller('api/v1/resources')
export class ResourceController {
    private readonly logger: Logger = new Logger(ResourceController.name);

    constructor(
        @Inject(RESOURCE_REPOSITORY)
        private readonly resourcesRepo: ResourceRepository
    ) {}

    @Get()
    getResources(): StorageResourceDto[] {
        return this.resourcesRepo.resources.map((r) => toDto(r));
    }

    @Get('/:id')
    getResource(@Param('id') id: string): StorageResourceDto {
        const resource = this.resourcesRepo.find(id);
        if (!resource) throw new HttpException('Not found', HttpStatus.NOT_FOUND);
        return toDto(resource);
    }
}
