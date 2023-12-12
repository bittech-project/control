import { NestFactory } from '@nestjs/core';
import { AgentModule } from './module';

process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';

async function main(): Promise<void> {
    await NestFactory.createApplicationContext(AgentModule);
}

main();
