import { Injectable } from '@nestjs/common';
import { JOB_CREATED } from 'src/jobs/types/jobs.events';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { FS } from 'src/jobs/types/fs/FS';
import { ConfigService } from '@nestjs/config';

@Injectable()
export class FsService {
    constructor(private readonly emitter: EventEmitter2, readonly config: ConfigService) {}

    mount() {
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new FS(env_key);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }
}
