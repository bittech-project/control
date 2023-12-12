import { Module } from '@nestjs/common';
import { ResourceModule } from 'src/resources/resource.module';
import { ResourcesGateway } from './resources.gateway';
import { AgentGateway } from './agent.gateway';
import { JobModule } from '../jobs/module';

@Module({
    imports: [ResourceModule, JobModule],
    providers: [ResourcesGateway, ResourcesGateway, AgentGateway],
})
export class GatewayModule {}
