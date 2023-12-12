import { IsString } from 'class-validator';

export class ExportNfsCreateDto {
    @IsString()
    ip: string;

    @IsString()
    export_path: string;

    @IsString()
    folder_name: string;

    params: string;
}

export class ExportNfsDeleteDto {
    @IsString()
    ip: string;

    @IsString()
    export_path: string;

    @IsString()
    folder_name: string;

    params: string;
}
