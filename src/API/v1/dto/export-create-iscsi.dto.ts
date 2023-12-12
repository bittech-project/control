import { IsString } from 'class-validator';

export class ExportIscsiCreateDto {
    @IsString()
    ip: string;

    @IsString()
    target: string;

    params: string;
}

export class ExportIscsiDeleteDto {
    @IsString()
    target: string;

    params: string;
}
