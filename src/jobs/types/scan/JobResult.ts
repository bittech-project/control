import { ResourceId, StatePropHealth } from 'src/entities/AbstractStorageResource';
import { ExportProto } from 'src/entities/Export';
import { IError } from 'src/entities/IError';
import { NicDriver } from 'src/entities/Network';

export interface ScannedResource {
    id: ResourceId;
    // nodeId: string;
    health: string | StatePropHealth;
}

export interface ScannedExport extends ScannedResource {
    params: object;
    name: string;
    proto: ExportProto;
    exportPath: string; // target name
    clients: string[];
    port?: string;
}
export interface ScannedNetwork extends ScannedResource {
    driver: NicDriver;
    fullName: string; // Device full name
    pciSlot: string; // PCI slot linux id - 01:00.0
    numaId: string;
    ports: ScannedNetworkPort[];
}

interface ScannedNetworkPort extends ScannedResource {
    addresses: string[];
    mac: string;
    maxLinkSpeedMbps: string;
    name: string;
    power: string;
    status: string;
    params: object;
}

export type JobResult = {
    environment_key: string;
    resources?: {
        nvmeof_exports: ScannedExport[];
        scst_exports: ScannedExport[];
        nfs_exports: ScannedExport[];
        net_interfaces: ScannedNetwork[];
    };
    errors: IError[];
};
