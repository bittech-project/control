import { Body, Controller, Delete, Post } from '@nestjs/common';
import { ApiBody, ApiTags } from '@nestjs/swagger';
import { NvmeofService } from 'src/resources/nvme.service';
import { ExportNvmeCreateDto, ExportNvmeDeleteDto } from './dto/export-nvme.dto';

@ApiTags('nvme')
@Controller('api/v1/exports')
export class NvmeController {
    constructor(private readonly nvmeof: NvmeofService) {}

    @Post('/nvme')
    @ApiBody({ type: ExportNvmeCreateDto })
    async makeNvme(@Body() dto) {
        const exportIscsi = this.nvmeof.connect(dto);
        return exportIscsi;
    }

    @Delete('/nvme')
    @ApiBody({ type: ExportNvmeDeleteDto })
    async delNvme(@Body() dto: ExportNvmeDeleteDto) {
        const exportIscsi = this.nvmeof.disconnect(dto);
        return exportIscsi;
    }
}
