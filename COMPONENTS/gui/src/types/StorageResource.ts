import type { Export } from '@/types/Export';
import type { Network } from '@/types/Network';

export enum ResourceType {
    Export = 'Export',
    Network = 'Network',
}

export type StorageResource = Export | Network;
