import { Injectable, Logger } from '@nestjs/common';
import { IJob } from 'src/jobs/types/IJob';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { EventListener, InjectIoClientProvider, IoClient, OnConnect, OnConnectError } from 'nestjs-io-client';
import { JOB_NEW, JOB_SAVE } from './agent.events';
import { MessageBody } from '@nestjs/websockets';
import { ConfigService } from '@nestjs/config';

@Injectable()
export class AgentClientService {
    private readonly logger: Logger = new Logger(AgentClientService.name);

    constructor(
        readonly config: ConfigService,
        private readonly emitter: EventEmitter2,
        @InjectIoClientProvider() private readonly io: IoClient
    ) {}

    @EventListener(JOB_NEW)
    fetchNewJobs(@MessageBody() jobs: IJob[]) {
        this.emitter.emit('new.job', jobs);
    }

    @OnConnect()
    connect() {
        this.logger.log('WS connected!');
    }

    @OnConnectError()
    connectError(err: Error) {
        this.logger.error(`An error occurs: ${err}`);
    }

    jobSave(cmd: IJob) {
        this.io.emit(JOB_SAVE, cmd);
    }
}
