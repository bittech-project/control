import { Module } from '@nestjs/common';
import { JobModule } from './jobs/module';
import { ConfigModule } from '@nestjs/config';
import { ServeStaticModule } from '@nestjs/serve-static';
import { join } from 'path';
import { ScheduleModule } from '@nestjs/schedule';
import Joi from 'joi';
import { CONFIG_VALIDATION_SCHEMA } from 'src/utils/config-validator';
import { ResourceModule } from './resources/resource.module';
import { EventEmitterModule } from '@nestjs/event-emitter';
import { GatewayModule } from './gateways/gateway.module';
import { ApiModule } from './API/v1/api.module';

@Module({
    imports: [
        ApiModule,
        GatewayModule,
        ResourceModule,
        JobModule,
        ScheduleModule.forRoot(),
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
        ServeStaticModule.forRoot({
            rootPath: join(__dirname, '..', 'public'),
        }),
    ],
})
export class AppModule {}
