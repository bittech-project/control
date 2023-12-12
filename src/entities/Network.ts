import { AbstractStorageResource, ResourceState, ResourceType, StatePropHealth } from './AbstractStorageResource';

export interface StateNetwork extends ResourceState {
    addresses: string[];
    power: string;
    status: string;
}

export enum NicDriver {
    UNKNOWN = 'UNKNOWN',
    AF_XDP = 'AF_XDP',
    AF_PACKET = 'AF_PACKET',
    ARK = 'ARK',
    ATLANTIC = 'ATLANTIC',
    AVP = 'AVP',
    AXGBE = 'AXGBE',
    BNX2X = 'BNX2X',
    BNXT = 'BNXT',
    CXGBE = 'CXGBE',
    CXGBEVF = 'CXGBEVF',
    DPAA = 'DPAA',
    DPAA2 = 'DPAA2',
    E1000 = 'E1000',
    ENA = 'ENA',
    ENETC = 'ENETC',
    ENIC = 'ENIC',
    FAILSAFE = 'FAILSAFE',
    FM10K = 'FM10K',
    FM10KVF = 'FM10KVF',
    HINIC = 'HINIC',
    HNS3 = 'HNS3',
    HNS3VF = 'HNS3VF',
    I40E = 'I40E',
    I40EVF = 'I40EVF',
    IAVF = 'IAVF',
    ICE = 'ICE',
    ICE_DCF = 'ICE_DCF',
    IGB = 'IGB',
    IGBVF = 'IGBVF',
    IGC = 'IGC',
    IONIC = 'IONIC',
    IPN3KE = 'IPN3KE',
    IXGBE = 'IXGBE',
    IXGBEVF = 'IXGBEVF',
    LIQUIDIO = 'LIQUIDIO',
    MEMIF = 'MEMIF',
    MLX4 = 'MLX4',
    MLX5 = 'MLX5',
    MVNETA = 'MVNETA',
    MVPP2 = 'MVPP2',
    NETVSC = 'NETVSC',
    NFB = 'NFB',
    NFP = 'NFP',
    OCTEONTX = 'OCTEONTX',
    OCTEONTX2 = 'OCTEONTX2',
    OCTEONTX2VEC = 'OCTEONTX2VEC',
    OCTEONTX2VF = 'OCTEONTX2VF',
    OCTEONTX_EP = 'OCTEONTX_EP',
    PCAP = 'PCAP',
    PFE = 'PFE',
    QEDE = 'QEDE',
    QEDEVF = 'QEDEVF',
    SFC_EFX = 'SFC_EFX',
    SZEDATA2 = 'SZEDATA2',
    TAP = 'TAP',
    THUNDERX = 'THUNDERX',
    TXGBE = 'TXGBE',
    VHOST = 'VHOST',
    VIRTIO = 'VIRTIO',
    VMXNET3 = 'VMXNET3',
}

export interface ParamsNetwork {
    driver: NicDriver;
    pciSlot: string;
    numaId: string;
    fullName: string;
    mac: string;
    maxLinkSpeedMbps: string;
}

export class Network extends AbstractStorageResource {
    readonly type = ResourceType.Network;
    params: ParamsNetwork = {
        driver: NicDriver.UNKNOWN,
        pciSlot: null,
        numaId: null,
        fullName: null,
        mac: null,
        maxLinkSpeedMbps: null,
    };
    state: StateNetwork = {
        health: StatePropHealth.UNKNOWN,
        addresses: [],
        status: null,
        power: null,
    };

    setParams(params: ParamsNetwork) {
        this.params = params;
    }
}
