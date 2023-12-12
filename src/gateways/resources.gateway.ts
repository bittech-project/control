import { ConnectedSocket, SubscribeMessage, WebSocketGateway, WebSocketServer } from '@nestjs/websockets';
import { Server, Socket } from 'socket.io';
import { Logger } from '@nestjs/common';
import { RESOURCE_CREATED, RESOURCE_DELETED, RESOURCE_UPDATED } from 'src/resources/resource.events';
import { OnEvent } from '@nestjs/event-emitter';
import { StorageResource } from 'src/entities/StorageResource';
import { Cron } from '@nestjs/schedule';

export type ResourceEvent = { type: string; resource: StorageResource };

@WebSocketGateway({ namespace: 'resources' })
export class ResourcesGateway {
    @WebSocketServer() server: Server;
    private readonly clients: Socket[] = [];
    private readonly logger: Logger = new Logger(ResourcesGateway.name);
    private readonly eventsQueue: Map<string, ResourceEvent> = new Map();

    @SubscribeMessage('connection')
    handleConnection(@ConnectedSocket() client: Socket) {
        this.addClient(client);
        this.logger.log(`Total clients ${this.clients.length}`);
    }

    @SubscribeMessage('disconnect')
    handleDisconnect(@ConnectedSocket() client: Socket) {
        this.removeClient(client);
        this.logger.log(`Total clients ${this.clients.length}`);
    }

    addClient(sock: Socket) {
        if (!this.clients.includes(sock)) this.clients.push(sock);
    }

    removeClient(sock: Socket) {
        const index = this.clients.indexOf(sock);
        if (index != -1) this.clients.splice(index);
    }

    @OnEvent(RESOURCE_CREATED)
    emitCreatedResource(resource: StorageResource) {
        this.eventsQueue.set(resource.id, <ResourceEvent>{ type: RESOURCE_CREATED, resource: resource });
    }

    @OnEvent(RESOURCE_UPDATED)
    emitUpdatedResource(resource: StorageResource) {
        this.eventsQueue.set(resource.id, <ResourceEvent>{ type: RESOURCE_UPDATED, resource: resource });
    }

    @OnEvent(RESOURCE_DELETED)
    emitDeletedResource(resource: StorageResource) {
        this.eventsQueue.set(resource.id, <ResourceEvent>{ type: RESOURCE_DELETED, resource: resource });
    }

    @Cron('*/1 * * * * *')
    async emitEvents() {
        if (this.eventsQueue.size) {
            this.clients.map((c) => {
                this.eventsQueue.forEach((e) => c.emit(e.type, e.resource));
            });
            this.eventsQueue.clear();
        }
    }
}
