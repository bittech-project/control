import { Injectable, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { exec } from 'child_process';
import { AgentCommandService } from 'src/agent/command.service';
import { IJob, JobStatus } from 'src/jobs/types/IJob';
import { JobResult } from 'src/jobs/types/scan/JobResult';
import { SCAN } from 'src/jobs/types/scan/SCAN';
import { promisify } from 'util';
import { Cron, CronExpression } from '@nestjs/schedule';

const execPromise = promisify(exec);

type ShellCommandResult = {
    stdout: string;
    stderr: string;
};

@Injectable()
export class ExecutorService {
    private readonly logger = new Logger(ExecutorService.name);
    private readonly cwd = this.config.get<string>('TESTLIB_COMMANDS_PATH') || '/opt/testlib/jobs';

    constructor(readonly config: ConfigService, private readonly commandService: AgentCommandService) {}

    // prettier-ignore
    makeShellString(j: IJob) {
        return `ansible-playbook  -i inventory.yml ${this.cwd}/${j.type}.yml -e '${JSON.stringify(j.params)}'`
    }

    /**
     * run ansible role, collect stdout as json, attach parsed json to the result and set job status
     * @param j
     * @returns
     */
    async execute(j: IJob): Promise<IJob> {
        const shellString = this.makeShellString(j);
        let commandResult: ShellCommandResult;
        try {
            commandResult = await execPromise(shellString, { cwd: this.cwd });
            j.status = JobStatus.OK;
        } catch (execError) {
            this.logger.warn(`${j.id} shell failed with <stdout: ${execError.stdout} | stderr: ${execError.stderr}`);
            commandResult = execError;
            j.status = JobStatus.FAILED;
        }
        try {
            // Collect Ansible output
            const parsed: JobResult = JSON.parse(commandResult.stdout).global_custom_stats as JobResult;
            j.result.environment_key = parsed.environment_key;
            j.result.resources = parsed.resources;
            j.result.errors = parsed.errors || [];
        } catch (err) {
            this.logger.error(`${j.id} could not parse shell output: ${commandResult.stdout}`);
            j.result.errors.push({
                error: 'ParseError',
                message: `AGENT could not parse job output: ${commandResult.stdout}`,
            });
        }
        if (commandResult.stderr) j.result.errors.push({ error: 'UnknownError', message: commandResult.stderr });
        return Promise.resolve(j);
    }

    async handle(j: IJob) {
        if (!j) return;
        if (j.environment_key !== this.config.get<string>('TESTLIB_ENVIRONMENT_KEY')) return;
        j.status = JobStatus.PROCESSING;
        return await this.commandService
            .save(j)
            .then(() => this.execute(j))
            .then((cmd) => this.commandService.save(cmd))
            .catch((err) => {
                this.logger.error(`unexpected ${err}`);
                j.status = JobStatus.FAILED;
                return this.commandService.save(j);
            });
    }

    /**
     * this trigger runs jobs one by one. it's required for jobs that change state.
     */
    @Cron(CronExpression.EVERY_SECOND)
    runSerial() {
        this.handle(this.commandService.getNewCommands()).catch((err) => {
            this.logger.error(`unexpected error!: ${err.stack}`);
        });
    }

    @Cron('*/17 * * * * *')
    async runScan() {
        const command = new SCAN(this.config.get<string>('TESTLIB_ENVIRONMENT_KEY'));
        await this.handle(command);
    }
}
