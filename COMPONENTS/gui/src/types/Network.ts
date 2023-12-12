import type { BaseResource } from '@/types/BaseResource';

export interface StateNetwork {
    addresses: [];
}

export interface Network extends BaseResource {
    state: StateNetwork;
}
