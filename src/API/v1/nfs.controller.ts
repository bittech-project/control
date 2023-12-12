import { Body, Controller, Inject, Post } from '@nestjs/common';
import { ApiBody, ApiTags } from '@nestjs/swagger';
import { ResourceRepository, RESOURCE_REPOSITORY } from 'src/resources/resource.repository';
import { ExportNfsCreateDto, ExportNfsDeleteDto } from './dto/export-nfs-dto';
import { NfsService } from 'src/resources/nfs.service';

@ApiTags('nfs')
@Controller('api/v2/nfs')
export class NfsController {
    constructor(
        @Inject(RESOURCE_REPOSITORY) private readonly repo: ResourceRepository,
        private readonly nfs: NfsService
    ) {}

    @Post('/nfs')
    @ApiBody({ type: ExportNfsCreateDto })
    async makeNfs(@Body() dto: ExportNfsCreateDto) {
        const exportNfs = this.nfs.mount(dto);
        return exportNfs;
    }

    @Post('/rmnfs')
    @ApiBody({ type: ExportNfsDeleteDto })
    async delNfs(@Body() dto: ExportNfsDeleteDto) {
        const exportNfs = this.nfs.unmount(dto);
        return exportNfs;
    }
}
