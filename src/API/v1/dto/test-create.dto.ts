import { IsString } from 'class-validator';

export class TestCreateDto {
    @IsString()
    target: string;

    params: string;
}
