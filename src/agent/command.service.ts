import { Injectable, Logger } from '@nestjs/common';
import { AgentClientService } from 'src/agent/AgentClient';
import { IJob } from 'src/jobs/types/IJob';
import { OnEvent } from '@nestjs/event-emitter';
import { ResourceId } from '../entities/AbstractStorageResource';

// import { JOB_NEW } from './agent.events';

@Injectable()
export class AgentCommandService {
    private readonly logger = new Logger(AgentCommandService.name);
    private newCommands: IJob[] = [];
    private busyResources: ResourceId[] = [];

    constructor(private readonly backend: AgentClientService) {}

    getNewCommands(): IJob {
        if (this.busyResources.length == 0) return this.newCommands.shift();

        for (let i = 0; i < this.newCommands.length; ++i) {
            // check that the target resources are not busy
            const targetIsBusy = this.newCommands[i].tgt.map((rId) => {
                return this.busyResources.includes(rId);
            });
            // if all target resources aren't busy - pushing it in busy list, removing job from queue, and return that
            if (!targetIsBusy.includes(true)) {
                this.busyResources.push(...this.newCommands[i].tgt);
                const [job] = this.newCommands.splice(i, 1);
                return job;
            }
        }
        return undefined;
    }

    async save(j: IJob): Promise<void> {
        try {
            this.backend.jobSave(j);
            this.removeFromQueue(j);
        } catch (err) {
            this.logger.error(`save: ${err} on saving ${JSON.stringify(j)}`);
            return;
        }
    }

    @OnEvent('new.job')
    private preventDuplicates(commands: IJob[]) {
        commands.forEach((cmd) => {
            const find = this.newCommands.find((c) => c.id == cmd.id);
            if (!find) this.newCommands.push(cmd);
        });
    }

    private removeFromQueue(j: IJob): void {
        this.newCommands = this.newCommands.filter((cmd) => cmd.id != j.id);
    }
}
