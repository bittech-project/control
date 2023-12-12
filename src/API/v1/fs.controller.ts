import { Controller, Post } from '@nestjs/common';
import { ApiBody, ApiTags } from '@nestjs/swagger';
import { FsService } from 'src/resources/fs.service';
import { FsCreateDto } from './dto/fs.dto';

@ApiTags('fs')
@Controller('api/v1/fs')
export class FsController {
    constructor(private readonly FsService: FsService) {}

    @Post('/fs')
    @ApiBody({ type: FsCreateDto })
    async makeFs() {
        const Fs = this.FsService.mount();
        return Fs;
    }
}
