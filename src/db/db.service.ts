import { Injectable, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import * as fs from 'fs';
import { EventEmitter2 } from '@nestjs/event-emitter';

class DB {
    resources: Map<string, string> = new Map<string, string>();
    users: Map<string, string> = new Map<string, string>();
    jobs: Map<string, string> = new Map<string, string>();
    tokens: Map<string, string> = new Map<string, string>();
}

@Injectable()
export class DbService {
    private readonly logger: Logger = new Logger(DbService.name);
    private readonly dbPath: string;
    private db: DB = new DB();
    private readonly MAX_WRITE_TRY = 3;
    private readonly backDbPath: string;

    constructor(private readonly config: ConfigService, private readonly emitter: EventEmitter2) {
        this.dbPath = config.get<string>('TESTLIB_DB_PATH');
        this.backDbPath = `${this.dbPath}.back`;
        try {
            const textConfig = fs.readFileSync(this.dbPath, 'utf-8');
            const parsed = JSON.parse(textConfig);
            this.db.resources = new Map<string, string>(parsed.resources);
            this.db.users = new Map<string, string>(parsed.users);
            this.db.jobs = new Map<string, string>(parsed.jobs);
        } catch (e) {
            fs.access(this.dbPath, (err) => {
                if (err) {
                    this.logger.warn('Text db was not exist');
                }
                this.logger.error('Text db was empty');
                fs.cp(this.dbPath, `${this.dbPath}.bad`, (err) => {
                    this.logger.error(`Cannot copy corrupted db file. Error: ${err}`);
                });
            });
        }
    }

    async fetchArray(prefix: string): Promise<unknown[]> {
        const [, key] = prefix.split('/');
        if (this.db[key]) return Promise.resolve([...this.db[key].values()]);
        this.logger.error(`Access to undefined namespace of db ${prefix}`);
        return Promise.reject(new Error(`Access to undefined namespace of db ${prefix}`));
    }

    async fetch(path: string): Promise<unknown | null> {
        const [, prefix, id] = path.split('/');
        if (this.db[prefix].get(id)) return Promise.resolve(this.db[prefix].get(id));
        this.logger.error(`Access to undefined key of db ${prefix}`);
        return Promise.reject(new Error(`Access to undefined key of db ${prefix}`));
    }

    async save(path: string, obj: string): Promise<void> {
        const [, prefix, id] = path.split('/');
        if (!this.db[prefix]) {
            this.db[prefix] = new Map([[id, obj]]);
        } else this.db[prefix].set(id, obj);
        this.saveToFile();
    }

    async rm(path: string) {
        const [, prefix, id] = path.split('/');
        this.db[prefix].delete(id);
        this.saveToFile();
        return;
    }

    saveToFile() {
        const raw = {
            resources: [...this.db.resources.entries()],
            users: [...this.db.users.entries()],
            jobs: [...this.db.jobs.entries()],
        };
        // this.emitter.emit(DB_SAVE, JSON.stringify(raw));
        for (let tryNum = 1; tryNum <= this.MAX_WRITE_TRY; tryNum++) {
            try {
                const fd = fs.openSync(this.backDbPath, 'w');
                fs.ftruncateSync(fd, 0);
                fs.writeSync(fd, JSON.stringify(raw));
                fs.fsyncSync(fd);
                fs.closeSync(fd);
            } catch {
                continue;
            }
            try {
                const fd = fs.openSync(this.dbPath, 'w');
                fs.ftruncateSync(fd, 0);
                fs.writeSync(fd, JSON.stringify(raw));
                fs.fsyncSync(fd);
                fs.closeSync(fd);
                return;
            } catch {
                if (tryNum == this.MAX_WRITE_TRY) throw new Error('Cannot write db');
            }
        }
    }
}
