import { Body, Controller, Delete, Post } from '@nestjs/common';
import { ApiBody, ApiTags } from '@nestjs/swagger';
import { IscsiService } from 'src/resources/iscsi.service';
import { ExportIscsiCreateDto, ExportIscsiDeleteDto } from './dto/export-create-iscsi.dto';

@ApiTags('iscsi')
@Controller('api/v1/exports')
export class IscsiController {
    constructor(private readonly iscsi: IscsiService) {}

    @Post('/iscsi')
    @ApiBody({ type: ExportIscsiCreateDto })
    async makeIscsi(@Body() dto: ExportIscsiCreateDto) {
        const exportIscsi = this.iscsi.connect(dto);
        return exportIscsi;
    }

    @Delete('/iscsi')
    @ApiBody({ type: ExportIscsiDeleteDto })
    async delIscsi(@Body() dto: ExportIscsiDeleteDto) {
        const exportIscsi = this.iscsi.disconnect(dto);
        return exportIscsi;
    }
}
