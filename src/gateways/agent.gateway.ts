import { ConnectedSocket, MessageBody, SubscribeMessage, WebSocketGateway, WebSocketServer } from '@nestjs/websockets';
import { Server, Socket } from 'socket.io';
import { Logger } from '@nestjs/common';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { IJob } from '../jobs/types/IJob';
import { JOB_NEW, JOB_RETURNED_FROM_AGENT, JOB_SAVE } from '../jobs/types/jobs.events';

@WebSocketGateway({ namespace: 'agent' })
export class AgentGateway {
    @WebSocketServer() server: Server;
    private readonly agents: Socket[] = [];
    private readonly logger: Logger = new Logger(AgentGateway.name);

    //TODO : support multinode
    constructor(private readonly emitter: EventEmitter2) {}

    @SubscribeMessage('connection')
    async handleConnection(@ConnectedSocket() client: Socket) {
        this.addAgent(client);
        this.logger.log(`Total agents ${this.agents.length}`);
    }

    @SubscribeMessage('disconnect')
    async handleDisconnect(@ConnectedSocket() client: Socket) {
        this.removeAgent(client);
        this.logger.log(`Total agents ${this.agents.length}`);
    }

    @SubscribeMessage(JOB_SAVE)
    async jobSave(@MessageBody() dto: IJob) {
        this.logger.debug(`received job ${dto.id}: ${dto.status}, ${dto.environment_key}`);
        this.emitter.emit(JOB_RETURNED_FROM_AGENT, dto);
    }

    @OnEvent(JOB_NEW)
    pushNewJob(job: IJob) {
        this.agents.forEach((s) => {
            s.emit(JOB_NEW, [job]);
        });
    }

    addAgent(sock: Socket) {
        if (!this.agents.includes(sock)) this.agents.push(sock);
    }

    removeAgent(sock: Socket) {
        const index = this.agents.indexOf(sock);
        if (index != -1) this.agents.splice(index);
    }
}
