import { Module } from '@nestjs/common';
import { HttpModule } from '@nestjs/axios';
import { ConfigModule, ConfigService } from '@nestjs/config';
import Joi from 'joi';
import { CONFIG_VALIDATION_SCHEMA } from 'src/utils/config-validator';
import { ServeStaticModule } from '@nestjs/serve-static';
import { join } from 'path';
import { AgentClientService } from './AgentClient';
import { AgentCommandService } from './command.service';
import { ExecutorService } from './executor.service';
import { IoClientModule } from 'nestjs-io-client';
import { EventEmitterModule } from '@nestjs/event-emitter';
import { ScheduleModule } from '@nestjs/schedule';

@Module({
    imports: [
        HttpModule,
        IoClientModule.forRootAsync({
            providers: [ConfigService],
            inject: [ConfigService],
            useFactory: (config: ConfigService) => {
                return {
                    uri: `wss://${config.get<string>('TESTLIB_CONTROL_HOST')}:${config.get<string>(
                        'TESTLIB_CONTROL_PORT'
                    )}/agent`,
                    options: {
                        reconnectionDelayMax: 10000,
                        transports: ['websocket'],
                        rejectUnauthorized: false,
                    },
                };
            },
        }),
        ConfigModule.forRoot({
            validationSchema: Joi.object(CONFIG_VALIDATION_SCHEMA),
            validationOptions: {
                allowUnknown: true,
                abortEarly: true,
            },
            envFilePath: [
                '/opt/testlib/control/resources/cfg/config.env',
                './resources/cfg/config.env',
                '../resources/cfg/config.env',
            ],
            isGlobal: true,
        }),
        ScheduleModule.forRoot(),
        ServeStaticModule.forRoot({
            rootPath: join(__dirname, '..', 'public'),
        }),
        EventEmitterModule.forRoot({
            // set this to `true` to use wildcards
            wildcard: false,
            // the delimiter used to segment namespaces
            delimiter: '.',
            // set this to `true` if you want to emit the newListener event
            newListener: false,
            // set this to `true` if you want to emit the removeListener event
            removeListener: false,
            // the maximum amount of listeners that can be assigned to an event
            maxListeners: 10,
            // show event name in memory leak message when more than maximum amount of listeners is assigned
            verboseMemoryLeak: false,
            // disable throwing uncaughtException if an error event is emitted and it has no listeners
            ignoreErrors: false,
        }),
    ],
    providers: [AgentClientService, AgentCommandService, ExecutorService],
})
export class AgentModule {}
