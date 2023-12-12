import { Logger } from '@nestjs/common';
import { EventEmitter2, OnEvent } from '@nestjs/event-emitter';
import { ResourceId, ResourceStatus, ResourceType } from 'src/entities/AbstractStorageResource';
import { StorageResource } from 'src/entities/StorageResource';
import { Job } from 'src/jobs/types/IJob';
import { JOB_CREATED, JOB_DONE } from 'src/jobs/types/jobs.events';
import { RESOURCE_DELETED, RESOURCE_UPDATED } from './resource.events';
import { Export, ParamsExport } from 'src/entities/Export';
import crypto from 'crypto';
import { DbService } from 'src/db/db.service';

export const RESOURCE_REPOSITORY = 'RESOURCE_REPOSITORY';

export class ResourceRepository {
    private readonly logger: Logger = new Logger(ResourceRepository.name);
    private resources_: Map<ResourceId, StorageResource> = new Map();
    // change handler for objects in _resources;
    // the set function called when object property assigned to new value
    private resourceProxyHandler: ProxyHandler<StorageResource> = {
        set: (target: StorageResource, prop: string, value) => {
            target[prop] = value;
            target.refresh();
            this.emitter.emit(RESOURCE_UPDATED, target);
            return true;
        },
    };

    constructor(private readonly db: DbService, private readonly emitter: EventEmitter2) {}

    get resources(): StorageResource[] {
        return Array.from(this.resources_.values());
    }

    async add(r: StorageResource): Promise<void> {
        this.resources_.set(r.id, new Proxy<StorageResource>(r, this.resourceProxyHandler));
        return await this.save(r);
    }

    find(id: string): StorageResource | undefined {
        return this.resources_.get(id);
    }

    async save(r: StorageResource): Promise<void> {
        const dao = toDao(r);
        await this.db.save(`/resources/${dao.id}`, JSON.stringify(dao));
    }

    /**
     * add the resource to the in-memory storage and save into DB
     * @param r
     */

    async remove(r: StorageResource) {
        this.resources_.delete(r.id);

        this.emitter.emit(RESOURCE_DELETED, r);
    }

    makeId(): ResourceId {
        let id: ResourceId;
        // eslint-disable-next-line no-constant-condition
        while (true) {
            id = `tid_${crypto.randomInt(10, 99)}${makeRandomStr(4)}`;
            if (!this.checkIdExists(id)) return id;
        }
    }

    /**
     *
     * @param id
     * @returns true in case Resource with such id already exists
     */
    checkIdExists(id: string): boolean {
        return this.resources.filter((r) => r.id == id).length > 0;
    }

    /**
     *
     * @param name
     * @param t
     * @returns true in case resource with given name and type already exists
     */
    checkDuplicatedName(name: string, t: ResourceType): boolean {
        return this.resources.filter((r) => r.name == name && r.type == t).length > 0;
    }

    @OnEvent(JOB_DONE)
    handle_JOB_DONE(j: Job) {
        if (!j.tgt) return;
        j.tgt.forEach((rid: ResourceId) => {
            const resource = this.find(rid);
            if (!resource) {
                this.logger.warn(`ResourceRepository: trying to unlock non-existing resource ${rid}`);
                return;
            }
            resource.unlock();
        });
    }

    @OnEvent(JOB_CREATED)
    handle_JOB_CREATED(j: Job) {
        if (!j.tgt) return;
        j.tgt.forEach((rid) => {
            const resource = this.find(rid);
            if (!resource) {
                this.logger.warn(`ResourceRepository: non-existing resource ${rid}`);
                return;
            }
            resource.locked = true;
        });
    }
}

export const resourceRepositoryFactory = {
    provide: RESOURCE_REPOSITORY,
    useFactory: async (db: DbService, emitter: EventEmitter2) => {
        const repo = new ResourceRepository(db, emitter);
        // await repo.init()
        return repo;
    },
    inject: [DbService, EventEmitter2],
};

export interface IResourceDao {
    id: ResourceId;
    environment_key: string;
    name: string;
    type: string;
    status: string;
    // nodeId: string[];
    created: number;
    params: object;
}

(dao: IResourceDao): StorageResource => {
    const resourceType: ResourceType = <ResourceType>dao.type;
    switch (resourceType) {
        case ResourceType.Export: {
            return new Export(
                dao.id,
                dao.environment_key,
                dao.name,
                dao.params as ParamsExport,
                // new Set<string>(dao.nodeId),
                ResourceStatus[dao.status],
                dao.created
            );
        }
    }
};

function toDao(r: StorageResource): IResourceDao {
    return {
        id: r.id,
        environment_key: r.environment_key,
        name: r.name,
        type: r.type,
        status: ResourceStatus[r.status],
        created: r.created,
        params: r.params,
    };
}

function makeRandomStr(len: number) {
    const allowedSymbols = 'abcdefghijklmnopqrstuvwxyz';
    let result = '';
    for (let i = 0; i < len; i++) {
        result += allowedSymbols[Math.floor(Math.random() * allowedSymbols.length)];
    }
    return result;
}
