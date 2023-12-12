import { NestFactory } from '@nestjs/core';
import { AppModule } from './app.module';
import { NestExpressApplication } from '@nestjs/platform-express';
import { readFileSync } from 'fs';
import { ConfigService } from '@nestjs/config';
import session from 'express-session';
import { LogLevel, ValidationPipe } from '@nestjs/common';
import backend from './backend.json';
import { DocumentBuilder, SwaggerModule } from '@nestjs/swagger';
import { useContainer } from 'class-validator';

process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';

async function main(): Promise<void> {
    const certPath = './resources/cert/'; //--FIXME ONLY hardcode
    const httpsOptions = {
        key: readFileSync(`${certPath}private-key.pem`),
        cert: readFileSync(`${certPath}public-certificate.pem`),
    };
    const app = await NestFactory.create<NestExpressApplication>(AppModule, {
        logger: backend.logLevel as LogLevel[],
        httpsOptions,
        cors: { origin: '*' },
    });
    const configService = app.get(ConfigService);

    const sessionSecret: string = configService.get<string>('sessionSecret');
    app.use(
        session({
            secret: readFileSync(sessionSecret, 'utf8'),
            resave: false,
            saveUninitialized: false,
        })
    );

    const port: number = configService.get<number>('TESTLIB_CONTROL_PORT');
    app.useGlobalPipes(
        new ValidationPipe({
            transform: true,
        })
    );
    useContainer(app.select(AppModule), { fallbackOnErrors: true });

    const config = new DocumentBuilder()
        .setTitle('TestLib Control API')
        .setVersion('0.0.1')
        .addCookieAuth('connect.sid')
        .build();
    const document = SwaggerModule.createDocument(app, config);
    SwaggerModule.setup('api', app, document);

    await app.listen(port);
}

main();
