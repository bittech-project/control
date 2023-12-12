import { IsString } from 'class-validator';

export class ExportNvmeCreateDto {
    ip: string;

    target: string;

    params: object;
}

export class ExportNvmeDeleteDto {
    @IsString()
    target: string;

    params: string;
}
