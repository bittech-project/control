import { Injectable } from '@nestjs/common';
import { JOB_CREATED } from 'src/jobs/types/jobs.events';
import { EventEmitter2 } from '@nestjs/event-emitter';
import { ConfigService } from '@nestjs/config';
import { Network, NicDriver } from 'src/entities/Network';
import { NETDEV_CONF, NETDEV_CONF_PARAMS } from 'src/jobs/types/network/NETDEV_CONF';

@Injectable()
export class NetworkService {
    constructor(private readonly emitter: EventEmitter2, readonly config: ConfigService) {}

    configure(params: NETDEV_CONF_PARAMS) {
        // Network device configuration
        const env_key = this.config.get<string>('TESTLIB_ENVIRONMENT_KEY');
        const job = new NETDEV_CONF(env_key, params);
        this.emitter.emit(JOB_CREATED, job);
        return job;
    }

    checkDPDKCompat(device: Network) {
        if (device.params.driver !== NicDriver.UNKNOWN) return true;
    }
}
