import { Inject, Injectable, Logger } from '@nestjs/common';
import { OnEvent } from '@nestjs/event-emitter';
import { ResourceType, StatePropHealth } from 'src/entities/AbstractStorageResource';
import {
    AbstractBlockExportProtoParams,
    Export,
    ExportProto,
    ExportTransport,
    ParamsExport,
    StateExport,
} from 'src/entities/Export';
import { JobType } from 'src/jobs/types/IJob';
import { ScannedExport, ScannedNetwork } from 'src/jobs/types/scan/JobResult';
import { SCAN } from 'src/jobs/types/scan/SCAN';
import { RESOURCE_REPOSITORY, ResourceRepository } from './resource.repository';
import { ConfigService } from '@nestjs/config';
import { Network, NicDriver, ParamsNetwork, StateNetwork } from 'src/entities/Network';

@Injectable()
export class ResourceService {
    private readonly logger: Logger = new Logger(ResourceService.name);

    constructor(
        readonly config: ConfigService,
        @Inject(RESOURCE_REPOSITORY) private readonly repo: ResourceRepository
    ) {}

    @OnEvent(JobType[JobType.SCAN])
    handleScan(j: SCAN) {
        this.handleScannedNetworks(j.environment_key, ...j.result.resources.net_interfaces);
        this.handleScannedBlockExports(
            j.environment_key,
            ...j.result.resources.scst_exports,
            ...j.result.resources.nvmeof_exports
        );
    }

    handleScannedNetworks(environment: string, ...scanned: ScannedNetwork[]) {
        const domainNetworks: Network[] = this.repo.resources.filter(
            (r) => r.type == ResourceType.Network
        ) as Network[];
        // handle lost networks
        domainNetworks.forEach((dn: Network) => {
            const networkFoundOnScan = scanned.filter((s) => s.ports.find((p) => p.mac === dn.params.mac)).length > 0;
            if (!networkFoundOnScan) dn.markAsLost();
        });

        scanned.forEach((s) => {
            s.ports.forEach((p) => {
                let networkInst: Network = domainNetworks.find((dn) => dn.params.mac === p.mac);

                const state: StateNetwork = {
                    addresses: p.addresses,
                    health: StatePropHealth[p.health],
                    power: p.power,
                    status: p.status,
                };

                if (!networkInst) {
                    this.logger.warn(`Network with mac ${p.mac} not found in ResourceRepository!`);
                    const params: ParamsNetwork = {
                        driver: NicDriver[s.driver],
                        mac: p.mac,
                        pciSlot: s.pciSlot,
                        numaId: s.numaId,
                        fullName: s.fullName,
                        maxLinkSpeedMbps: p.maxLinkSpeedMbps,
                    };
                    networkInst = new Network(this.repo.makeId(), environment, p.name, {});
                    networkInst.setParams(params);
                    this.repo.add(networkInst);
                }

                networkInst.setState(state);
            });
        });
    }

    handleScannedBlockExports(environment: string, ...scanned: ScannedExport[]) {
        const domainExports: Export[] = this.repo.resources
            .filter((r) => r.type == ResourceType.Export)
            .filter((de) => de.environment_key == environment)
            .filter(
                (e: Export) => e.params.proto == ExportProto.iscsi || e.params.proto == ExportProto.nvmeof
            ) as Export[];
        // handle lost volumes
        domainExports.forEach((de: Export) => {
            const exportFoundOnScan =
                scanned.filter((s) => s.exportPath === (de.params.protoParams as AbstractBlockExportProtoParams).qn)
                    .length > 0;
            if (!exportFoundOnScan) de.markAsLost();
        });

        scanned.forEach((s) => {
            let exportInst: Export = domainExports.find(
                (de) => (de.params.protoParams as AbstractBlockExportProtoParams).qn === s.exportPath
            );

            const state: StateExport = {
                clients: s.clients,
                health: StatePropHealth[s.health],
                exportPath: s.exportPath,
            };

            if (!exportInst) {
                this.logger.warn(`Export with name ${s.exportPath} not found in ResourceRepository!`);

                exportInst = new Export(this.repo.makeId(), environment, s.name, s.params);
                (exportInst.params as ParamsExport).proto = ExportProto[s.proto];
                (exportInst.params as ParamsExport).protoParams = {
                    qn: s.exportPath,
                    transport: ExportTransport['tcp'],
                };
                exportInst.setState(state);
                this.repo.add(exportInst);
            }
            exportInst.setState(state);
        });
    }

    // handleScannedFileExports(...scanned: ScannedExport[]) {
    //     const domainExports: Export[] = this.repo.resources.filter(
    //         (r: Export) =>
    //             r.type == ResourceType.Export && r.params.proto == ExportProto.nfs && !(<ExportOzrState>r.state).fsId
    //     ) as Export[];
    //     // handle lost volumes
    //     domainExports.forEach((de: Export) => {
    //         const exportFoundOnScan =
    //             scanned.filter((s) => s.exportPath === (de.params.protoParams as ExportProtoParamsNfs).mountPath)
    //                 .length > 0;
    //         if (!exportFoundOnScan) de.markAsLost();
    //     });

    //     scanned.forEach((s) => {
    //         const exportInst: Export = domainExports.find(
    //             (de) => (de.params.protoParams as ExportProtoParamsNfs).mountPath === s.exportPath
    //         );
    //         if (!exportInst) {
    //             // TODO: create new Export with status UNKNOWN and add to repo
    //             this.logger.warn(`Export with name ${s.exportPath} not found in ResourceRepository!`);
    //             return;
    //         }
    //         const state: StateExport = {
    //             environment_key: env_key,
    //             clients: s.clients,
    //             health: StatePropHealth[s.health],
    //             exportPath: s.exportPath,
    //         };
    //         if (!exportInst.locked) exportInst.setState(state);
    //         exportInst.refresh();
    //     });
    // }
}
